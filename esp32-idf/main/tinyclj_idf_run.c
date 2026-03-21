#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "memory.h"
#include "platform.h"
#include "runtime.h"
#include "builtins.h"
#include "source_resolver.h"
#include "namespace.h"
#include "meta.h"
#include "line_editor.h"
#include "event_loop.h"
#include "mini_format.h"
#include "repl.h"
#include "tiny_clj.h"

#ifndef ESP_REPL_HISTORY_PERSIST_ENABLED
#define ESP_REPL_HISTORY_PERSIST_ENABLED 1
#endif

#if defined(ESP_PLATFORM)
/**
 * @brief Writes a single line to the active platform output stream.
 *
 * @param s Null-terminated string to print, may be NULL.
 */
static void esp_put_line(const char *s) {
    if (s) platform_put_string(NULL, s);
    platform_put_string(NULL, "\n");
}
#endif

// If history handling ever OOMs, disable it for the running session.
static bool g_esp_history_disabled = false;

static bool esp_exception_is_oom(const CLJException *ex) {
    return ex && ex->type[0] && strcmp(ex->type, EXCEPTION_OUT_OF_MEMORY) == 0;
}

// No public header exists for these yet.
extern void init_special_symbols(void);
extern EvalState *get_global_eval_state(void);
extern int load_clojure_core(EvalState *st);

static bool esp_repl_core_loaded(void) {
    CljNamespace *core = ns_find("clojure.core");
    return core && core->loaded;
}

static bool esp_repl_ensure_core_loaded(EvalState *st) {
    if (!st) {
        return false;
    }
    if (esp_repl_core_loaded()) {
        return true;
    }
    if (!load_clojure_core(st)) {
        return false;
    }
    evalstate_set_ns(st, "user");
    return esp_repl_core_loaded();
}

static bool esp_boot_load_root_file(EvalState *st) {
    if (!st) return false;

    static const char *boot_path = "/boot/root.edn";
    ID bytes = resolve_path_to_bytes(boot_path);
    if (!bytes) {
        return true; // no boot expression configured
    }

    bool ok = true;
    TRY {
        (void)eval_string("(load-file \"/boot/root.edn\")", st);
    }
    CATCH(ex) {
        (void)ex;
        ok = false;
    }
    END_TRY

    return ok;
}

static unsigned int esp_repl_idle_sleep_ms(void) {
    if (event_loop_has_pending_tasks()) {
        return 1u;
    }

    int until_next_timer_ms = event_loop_time_until_next_timer_ms();
    if (until_next_timer_ms >= 0 && until_next_timer_ms < 10) {
        return (until_next_timer_ms <= 1) ? 1u : (unsigned int)until_next_timer_ms;
    }

    return 10u;
}

/**
 * @brief Loads serialized REPL history from KV storage.
 *
 * @param st Active evaluator state (unused, kept for symmetric API).
 * @return Retained history vector (empty when no persisted history exists).
 */
static CljPersistentVector *esp_repl_history_load(EvalState *st) {
    (void)st;
#if !ESP_REPL_HISTORY_PERSIST_ENABLED
    return (CljPersistentVector*)RETAIN(empty_vector());
#else
    CljObject *loaded = line_editor_history_load_default();
    if (!loaded || TAG(loaded) != CLJ_VECTOR_PERSISTENT) {
        return (CljPersistentVector*)RETAIN(empty_vector());
    }
    return (CljPersistentVector*)RETAIN(loaded);
#endif
}

/**
 * @brief Serializes and persists line editor history to KV storage.
 *
 * @param ed Line editor instance owning current history.
 * @return true when history is saved successfully.
 */
static bool esp_repl_history_save(LineEditor *ed) {
#if !ESP_REPL_HISTORY_PERSIST_ENABLED
    (void)ed;
    return false;
#else
    if (g_esp_history_disabled) {
        return false;
    }
    if (!ed) {
        return false;
    }
    CljPersistentVector *history = line_editor_get_history_vector(ed);
    if (!history) {
        return false;
    }
    bool saved = false;
    TRY {
        saved = line_editor_history_save_default((CljObject*)history);
    }
    CATCH(ex) {
        if (esp_exception_is_oom(ex)) {
            g_esp_history_disabled = true;
            line_editor_clear_history(ed);
        }
        saved = false;
    }
    END_TRY
    RELEASE(history);
    return saved;
#endif
}

/**
 * @brief Adds an entry to REPL history without letting history failures abort the REPL.
 *
 * History is best-effort on ESP32. Under memory pressure, adding an entry may throw
 * (e.g. while growing the backing vector). Those failures are intentionally swallowed.
 */
static void esp_repl_history_add_best_effort(LineEditor *ed, const char *line) {
    if (g_esp_history_disabled) {
        return;
    }
    if (!ed || !line) {
        return;
    }
    TRY {
        line_editor_add_to_history(ed, line);
    }
    CATCH(ex) {
        if (esp_exception_is_oom(ex)) {
            g_esp_history_disabled = true;
            line_editor_clear_history(ed);
        }
    }
    END_TRY
}

/**
 * @brief Boots tiny-clj runtime and serves the ESP32 UART REPL loop.
 */
void tinyclj_idf_start(void) {
    platform_init();

    // Runtime bootstrap (same order as repl.c, minus host-specific CLI handling).
    runtime_init(&g_runtime);
    meta_registry_init();
    init_special_symbols();
    event_loop_init();

    EvalState *st = get_global_eval_state();
    evalstate_set_ns(st, "user");

    bool boot_root_loaded = true;
    bool boot_root_present = (resolve_path_to_bytes("/boot/root.edn") != NULL);
    WITH_AUTORELEASE_POOL({
        register_builtins();
        // Keep startup heap low: only load full clojure.core eagerly when a boot script exists.
        if (boot_root_present) {
            boot_root_loaded = esp_repl_ensure_core_loaded(st);
            if (boot_root_loaded) {
                boot_root_loaded = esp_boot_load_root_file(st);
            }
        }
        evalstate_set_ns(st, "user");
    });

    if (!boot_root_loaded) {
        platform_put_string(NULL, "Warning: failed to evaluate boot root expression from /boot/root.edn");
        platform_put_string(NULL, "\n");
    }

#if defined(ESP_PLATFORM)
    {
        char title[80];
        (void)mini_snprintf(title, sizeof(title), "**** ESP 32 TINY-CLJ V%s ****", TINY_CLJ_VERSION);
        ReplStartupBannerConfig banner_cfg = {
            .title_line = title,
            .center_title = true,
            .center_width = 40u,
            .include_ram_line = true,
            .include_exit_hint = true
        };
        repl_print_startup_banner_with_emitter(&banner_cfg, esp_put_line);
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

    char acc[REPL_ACC_CAP_DEFAULT];
    acc[0] = '\0';

    for (;;) {
        char prompt[128];
        bool balanced = (repl_form_state(acc, NULL) == REPL_FORM_BALANCED);
        repl_format_prompt(st, balanced, prompt, sizeof(prompt));
        line_editor_set_prompt(ed, prompt);
        platform_put_string(NULL, prompt);
        line_editor_clear(ed);

        for (;;) {
            int r = line_editor_process_input(ed);
            if (r == LINE_EDITOR_LINE_READY) {
                break;
            }
            if (r == LINE_EDITOR_INTERRUPT) {
                acc[0] = '\0';
                break;
            }
            if (r == LINE_EDITOR_EOF) {
                (void)esp_repl_history_save(ed);  // best-effort
                return;
            }
            repl_process_event_loop(st);
            platform_sleep_ms(esp_repl_idle_sleep_ms());
        }

        LineEditorState s;
        (void)memset(&s, 0, sizeof(s));
        int st_rc = line_editor_get_state(ed, &s);
        if (st_rc == 0) {
            if (s.length == 0) {
                if (acc[0] != '\0' && repl_form_state(acc, NULL) == REPL_FORM_INCOMPLETE) {
                    acc[0] = '\0';
                }
                continue;
            }

            if (!repl_acc_append_line(acc, sizeof(acc), s.buffer)) {
                platform_put_string(NULL, "Error: Input too long (resetting form)\n");
                acc[0] = '\0';
                continue;
            }

            ReplFormState form_state = repl_form_state(acc, NULL);
            if (form_state == REPL_FORM_INCOMPLETE) {
                continue; // Incomplete form: continue reading with continuation prompt.
            }
            if (form_state == REPL_FORM_INVALID) {
                platform_put_string(NULL, "Error: Too many closing parentheses\n");
                esp_repl_history_add_best_effort(ed, acc);
                (void)esp_repl_history_save(ed); // best-effort
                acc[0] = '\0';
                continue;
            }

            esp_repl_history_add_best_effort(ed, acc);
            (void)esp_repl_history_save(ed);  // best-effort
            if (!esp_repl_ensure_core_loaded(st)) {
                platform_put_string(NULL, "Error: failed to load clojure.core\n");
                acc[0] = '\0';
                continue;
            }
            (void)eval_multiform_string(acc, st);
            acc[0] = '\0';
        }
    }
}
