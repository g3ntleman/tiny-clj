#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "memory.h"
#include "platform.h"
#include "runtime.h"
#include "builtins.h"
#include "namespace.h"
#include "parser.h"
#include "eval.h"
#include "reader.h"
#include "exception.h"
#include "meta.h"
#include "to_string.h"
#include "line_editor.h"
#include "event_loop.h"
#include "mini_format.h"
#include "tiny_clj.h"
#include "fs_layer.h"

#define ESP_REPL_HISTORY_KV_KEY "repl.history"
#define ESP_REPL_HISTORY_PAGE_PAYLOAD_LIMIT 4080u
#define ESP_REPL_HISTORY_MAX_READ_BYTES (64u * 1024u)
#define ESP_REPL_HISTORY_TRIM_NUM 8
#define ESP_REPL_HISTORY_TRIM_DEN 10

#if defined(ESP_PLATFORM)
static void esp_put_line(const char *s) {
    if (s) platform_put_string(NULL, s);
    platform_put_string(NULL, "\n");
}
static void print_build_info_esp32(void) {
    char buf[80];
    esp_put_line("=== Build Information ===");
#if defined(DEBUG) && DEBUG
    esp_put_line("Build Type: Debug");
#else
    esp_put_line("Build Type: Release");
#endif
    (void)mini_snprintf(buf, sizeof(buf), "Build Date: %s %s", __DATE__, __TIME__);
    esp_put_line(buf);
    esp_put_line("Compiler: GCC (xtensa-esp-elf)");
    esp_put_line("Features:");
#if defined(DEBUG) && DEBUG
    esp_put_line("  - Debug: Enabled");
#else
    esp_put_line("  - Debug: Disabled");
#endif
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    esp_put_line("  - Memory Profiling: Enabled");
#else
    esp_put_line("  - Memory Profiling: Disabled");
#endif
#if defined(ZOMBIE_ENABLED) && ZOMBIE_ENABLED
    esp_put_line("  - Zombie Mode: Enabled");
#else
    esp_put_line("  - Zombie Mode: Disabled");
#endif
#if defined(META_ENABLED) && META_ENABLED
    esp_put_line("  - Meta: Enabled");
#else
    esp_put_line("  - Meta: Disabled");
#endif
    esp_put_line("=========================");  /* 25 '=' to match "=== Build Information ===" */
}
#endif

// No public header exists for these yet (they are used by repl.c and tests too).
extern void init_special_symbols(void);
extern EvalState *get_global_eval_state(void);
extern int load_clojure_core(EvalState *st);

static void print_result(CljObject *v) {
    if (!v) {
        platform_put_string(NULL, "nil\n");
        return;
    }
    CljString *s = pr_str(v);
    if (s) {
        platform_put_string(NULL, string_data(s));
        platform_put_string(NULL, "\n");
    }
}

static void eval_and_print(const char *code, EvalState *st) {
    if (!code || !st) return;

    Reader reader;
    reader_init(&reader, code);
    reader_set_source_name(&reader, "<esp32 repl>");

    while (!reader_is_eof(&reader)) {
        reader_skip_all(&reader);
        if (reader_is_eof(&reader)) break;

        WITH_AUTORELEASE_POOL(
            TRY {
                CljValue parsed = parse_from_reader(&reader, st);
                ID res = eval_parsed_value(parsed, st);
                print_result(res);
            } CATCH(ex) {
                print_exception((CLJException*)ex);
            } END_TRY
        );
    }
}

static void process_event_loop(EvalState *st) {
    for (int i = 0; i < 10; i++) {
        if (!event_loop_run_next(NULL, st)) break;
    }
}

static CljPersistentVector *esp_repl_history_take_last(CljPersistentVector *vec, int keep_count) {
    if (!vec || keep_count <= 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    int count = vector_count(vec);
    if (count <= keep_count) {
        return (CljPersistentVector*)RETAIN(vec);
    }

    int start = count - keep_count;
    CljPersistentVector *out = make_vector(keep_count, STRONG);
    if (!out) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }
    for (int i = start; i < count; i++) {
        vector_conj_inplace(&out, vector_nth(vec, i));
    }
    return out;
}

static size_t esp_repl_history_effective_limit(FsKvStore *store) {
    size_t limit = ESP_REPL_HISTORY_PAGE_PAYLOAD_LIMIT;
    size_t kv_max = 0;
    if (store &&
        fs_kv_max_val_len_status(store, ESP_REPL_HISTORY_KV_KEY, &kv_max) == TDB_OK &&
        kv_max > 0 && kv_max < limit) {
        limit = kv_max;
    }
    return limit;
}

static CljPersistentVector *esp_repl_history_trim_to_limit(CljPersistentVector *vec, size_t byte_limit) {
    if (!vec || byte_limit == 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    int keep_count = vector_count(vec);
    if (keep_count <= 0) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    while (keep_count > 0) {
        CljPersistentVector *candidate = esp_repl_history_take_last(vec, keep_count);
        CljString *repr = candidate ? pr_str(candidate) : NULL;
        size_t repr_len = repr ? string_length(repr) : (size_t)-1;
        if (repr && repr_len <= byte_limit) {
            return candidate;
        }
        RELEASE(candidate);

        if (keep_count == 1) break;
        int reduced = (keep_count * ESP_REPL_HISTORY_TRIM_NUM) / ESP_REPL_HISTORY_TRIM_DEN;
        if (reduced >= keep_count) reduced = keep_count - 1;
        if (reduced < 1) reduced = 1;
        keep_count = reduced;
    }

    // A single oversized entry does not block REPL operation; persist empty history.
    return (CljPersistentVector*)RETAIN(empty_vector());
}

static CljPersistentVector *esp_repl_history_parse_vector(const char *edn, EvalState *st) {
    if (!edn || !st) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    CljPersistentVector *parsed_vec = NULL;
    WITH_AUTORELEASE_POOL({
        TRY {
            Reader rd;
            reader_init(&rd, edn);
            reader_set_source_name(&rd, "<esp32 repl history>");
            ID parsed = value_by_parsing_expr(&rd, st);
            if (parsed && TAG(parsed) == CLJ_VECTOR_PERSISTENT) {
                parsed_vec = (CljPersistentVector*)RETAIN(parsed);
            }
        } CATCH(ex) {
            (void)ex;
            parsed_vec = NULL;
        } END_TRY
    });

    return parsed_vec ? parsed_vec : (CljPersistentVector*)RETAIN(empty_vector());
}

static CljPersistentVector *esp_repl_history_load(EvalState *st) {
    FsKvStore *store = fs_global_store_if_initialized();
    if (!store) store = fs_global_store();
    if (!store) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    size_t saved_len = 0;
    tdb_status_t stc = fs_kv_get_status(store, ESP_REPL_HISTORY_KV_KEY, NULL, 0, &saved_len);
    if (stc != TDB_OK || saved_len == 0 || saved_len > ESP_REPL_HISTORY_MAX_READ_BYTES) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    char *buf = CLJ_MALLOC(saved_len + 1u);
    if (!buf) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }

    size_t loaded_len = 0;
    stc = fs_kv_get_status(store, ESP_REPL_HISTORY_KV_KEY, (uint8_t*)buf, saved_len, &loaded_len);
    if (stc != TDB_OK || loaded_len == 0) {
        CLJ_FREE(buf);
        return (CljPersistentVector*)RETAIN(empty_vector());
    }
    if (loaded_len > saved_len) loaded_len = saved_len;
    buf[loaded_len] = '\0';

    CljPersistentVector *vec = esp_repl_history_parse_vector(buf, st);
    CLJ_FREE(buf);
    return vec;
}

static bool esp_repl_history_save(LineEditor *ed) {
    if (!ed) return false;

    FsKvStore *store = fs_global_store_if_initialized();
    if (!store) store = fs_global_store();
    if (!store) return false;

    size_t limit = esp_repl_history_effective_limit(store);
    if (limit == 0) return false;

    CljPersistentVector *history = line_editor_get_history_vector(ed);
    if (!history) return false;

    CljPersistentVector *trimmed = esp_repl_history_trim_to_limit(history, limit);
    RELEASE(history);
    if (!trimmed) return false;

    CljString *repr = pr_str(trimmed);
    if (!repr) {
        RELEASE(trimmed);
        return false;
    }

    size_t len = string_length(repr);
    if (len > limit) {
        RELEASE(trimmed);
        return false;
    }

    tdb_status_t stc = fs_kv_put_status(store, ESP_REPL_HISTORY_KV_KEY, (const uint8_t*)string_data(repr), len);
    RELEASE(trimmed);
    return stc == TDB_OK;
}

void tinyclj_idf_start(void) {
    platform_init();

    // Runtime bootstrap (same order as repl.c, minus host-specific CLI handling).
    runtime_init(&g_runtime);
    meta_registry_init();
    init_special_symbols();
    event_loop_init();

    EvalState *st = get_global_eval_state();
    evalstate_set_ns(st, "user");

    WITH_AUTORELEASE_POOL({
        register_builtins();
        (void)load_clojure_core(st);
        evalstate_set_ns(st, "user");
    });

#if defined(ESP_PLATFORM)
    {
        char buf[80];
        (void)mini_snprintf(buf, sizeof(buf), "**** ESP 32 TINY-CLJ V%s ****", TINY_CLJ_VERSION);
        size_t len = strlen(buf);
        unsigned pad = (len < 40u) ? (40u - (unsigned)len) / 2u : 0u;
        if (pad > 0) {
            char pad_buf[20];
            memset(pad_buf, ' ', pad);
            pad_buf[pad] = '\0';
            platform_put_string(NULL, pad_buf);
        }
        esp_put_line(buf);
        (void)mini_snprintf(buf, sizeof(buf), "tiny-clj %s REPL (platform = %s).", TINY_CLJ_VERSION, platform_name());
        esp_put_line(buf);
        print_build_info_esp32();
        size_t ram_total = platform_ram_bytes_total();
        size_t free_bytes = platform_heap_bytes_free();
        unsigned ram_k = (ram_total == (size_t)-1) ? 0u : (unsigned)(ram_total / 1024);
        (void)mini_snprintf(buf, sizeof(buf), "%uK RAM SYSTEM  %zu CLOJURE BYTES FREE", ram_k, free_bytes);
        esp_put_line(buf);
        esp_put_line("Ctrl-D to exit.");
    }
#endif

    // NOTE: baseline stats disabled for now (heap corruption during early eval).

    LineEditor *ed = line_editor_new(platform_get_char, platform_put_char, platform_put_string, NULL);
    set_line_editor(ed);
    if (ed) {
        CljPersistentVector *loaded = esp_repl_history_load(st);
        if (loaded) {
            if (vector_count(loaded) > 0) {
                line_editor_set_history_from_vector(ed, loaded);
            }
            RELEASE(loaded);
        }
    }

    for (;;) {
        platform_put_string(NULL, "user=> ");
        line_editor_clear(ed);

        for (;;) {
            int r = line_editor_process_input(ed);
            if (r == LINE_EDITOR_LINE_READY) break;
            if (r == LINE_EDITOR_EOF) {
                (void)esp_repl_history_save(ed);  // best-effort
                return;
            }
            process_event_loop(st);
            platform_sleep_ms(10);
        }

        LineEditorState s;
        (void)memset(&s, 0, sizeof(s));
        if (line_editor_get_state(ed, &s) == 0) {
            if (s.length > 0) {
                line_editor_add_to_history(ed, s.buffer);
                (void)esp_repl_history_save(ed);  // best-effort
            }
            if (s.length > 0) {
                eval_and_print(s.buffer, st);
            }
        }
    }
}
