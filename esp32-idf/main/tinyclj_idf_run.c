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

    for (;;) {
        platform_put_string(NULL, "user=> ");
        line_editor_clear(ed);

        for (;;) {
            int r = line_editor_process_input(ed);
            if (r == LINE_EDITOR_LINE_READY) break;
            if (r == LINE_EDITOR_EOF) return;
            process_event_loop(st);
            platform_sleep_ms(10);
        }

        LineEditorState s;
        (void)memset(&s, 0, sizeof(s));
        if (line_editor_get_state(ed, &s) == 0) {
            if (s.length > 0) {
                // In-memory history only (no filesystem persistence).
                line_editor_add_to_history(ed, s.buffer);
            }
            if (s.length > 0) {
                eval_and_print(s.buffer, st);
            }
        }
    }
}
