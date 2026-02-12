#include "platform.h"
#include "common.h"
#include "tiny_clj.h"
#include "repl.h"
#include "parser.h"
#include "eval.h"  // For eval_parsed_value
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "object.h"
#include "exception.h"
#include "builtins.h"
#include "memory_profiler.h"
#include "line_editor.h"
#include "strings.h"
#include "to_string.h"
#include "reader.h"
#include "runtime.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include "event_loop.h"
#include "file_utils.h"
#include "meta.h"
#include "build_info.h"
#include "mini_format.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

// Tracks whether stdout is currently at the start of a new line.
// Best-effort: updated via platform_macos stdout observer hook (for most prints)
// and by REPL's own printing.
static bool g_repl_stdout_at_line_start = true;

// Crash diagnostics for SIGTRAP during core load.
extern volatile sig_atomic_t g_clojure_core_last_form;
#ifndef UNITY_TESTS
static void repl_sigtrap_handler(int signo) {
    fprintf(stderr, "REPL SIGTRAP (signal %d) during core load; last form=%d\n",
            signo, (int)g_clojure_core_last_form);
    exception_print_native_backtrace();
    _exit(128 + signo);
}
#endif

void tinyclj_stdout_observe_bytes(const char *data, size_t n) {
    if (!data || n == 0) return;
    // Update based on the last byte written.
    char last = data[n - 1];
    g_repl_stdout_at_line_start = (last == '\n' || last == '\r');
}

// Maximum number of event loop iterations per REPL cycle
// This limits processing to prevent blocking while still processing pending tasks
#define REPL_EVENT_LOOP_MAX_ITERATIONS 10

// Forward decls for line editor history persistence helpers
extern CljObject* line_editor_history_load_default(void);
extern bool line_editor_history_save_default(CljObject *vec);
extern void set_line_editor(LineEditor *editor);
extern LineEditor* get_line_editor(void);
extern CljPersistentVector* line_editor_get_history_vector(LineEditor *editor);
extern int line_editor_get_history_size(const LineEditor *editor);
extern void line_editor_clear_history(LineEditor *editor);

/** @brief Check the balance of parentheses, brackets, and braces.
 *  @param s String to check for delimiter balance
 *  @param error_pos Output parameter for position of first error (can be NULL)
 *  @return > 0 if incomplete (need more closing), = 0 if balanced, < 0 if invalid (too many closing)
 */
static int form_balance(const char *s, int *error_pos) {
    int p = 0, b = 0, c = 0; // () [] {}
    bool in_str = false; bool esc = false;
    int pos = 0;
    int first_error_pos = -1;

    for (const char *x = s; *x; ++x, ++pos) {
        char ch = *x;
        if (in_str) {
            if (esc) { esc = false; continue; }
            if (ch == '\\') { esc = true; continue; }
            if (ch == '"') { in_str = false; continue; }
            continue;
        }
        if (ch == '"') { in_str = true; continue; }
        if (ch == '(') p++; else if (ch == ')') p--;
        else if (ch == '[') b++; else if (ch == ']') b--;
        else if (ch == '{') c++; else if (ch == '}') c--;

        // Check for negative balance (too many closing)
        if ((p < 0 || b < 0 || c < 0) && first_error_pos == -1) {
            first_error_pos = pos;
        }
    }

    if (error_pos) {
        *error_pos = first_error_pos;
    }

    // Return total imbalance (positive = incomplete, negative = too many closing)
    return p + b + c + (in_str ? 1 : 0);
}

/** @brief Print REPL prompt with namespace and continuation indicator.
 *  @param st Evaluation state containing current namespace
 *  @param balanced Whether the current input is balanced
 */
static void print_prompt(EvalState *st, bool balanced) {
    const char *ns_name = "user";  // Default
    if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname) {
        if (st->current_ns->name->cname[0] != '\0') {
            ns_name = st->current_ns->name->cname;
        }
    }
    char prompt[128];
    (void)mini_snprintf(prompt, sizeof(prompt), "%s%s ", ns_name, balanced ? "=>" : "...");
    // Ensure the prompt starts at column 1, even if previous output did not end with '\n'.
    // This is best-effort: we track stdout line-start via platform stdout hooks.
    bool at_line_start = g_repl_stdout_at_line_start;
#if defined(__APPLE__) && !defined(ESP32_BUILD)
    // On macOS terminals, try a real cursor-position query (DSR ESC[6n) to avoid heuristics.
    // If unsupported or not a TTY, we fall back to the stdout observer flag.
    uint16_t row = 0, col = 0;
    if (platform_try_get_cursor_position(&row, &col)) {
        at_line_start = (col == 1);
    }
#endif
    if (!at_line_start) {
        fputs("\r\n", stdout);
        g_repl_stdout_at_line_start = true;
    }
    // Ensure line editor knows the prompt, so multi-line redraw can restore it.
#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
    LineEditor *ed = get_line_editor();
    if (ed) {
        line_editor_set_prompt(ed, prompt);
    }
#endif
    fputs(prompt, stdout);
    fflush(stdout);
    g_repl_stdout_at_line_start = false;
}

/** @brief Print a CljObject result to stdout with proper formatting.
 *  @param v Object to print (can be NULL)
 */
static void print_result(CljObject *v) {
    if (!v) {
        platform_put_string(NULL, "nil");
        platform_put_char(NULL, '\n');
        return;
    }
    CljString *s = pr_str(v);
    if (s) {
        platform_put_string(NULL, string_data(s));
        platform_put_char(NULL, '\n');
    }
}

/** @brief Process pending event loop tasks (up to max iterations).
 *  @param st Evaluation state
 *
 *  This function processes up to REPL_EVENT_LOOP_MAX_ITERATIONS tasks from
 *  the event loop queue, stopping early if the queue becomes empty.
 */
static void repl_process_event_loop(EvalState *st) {
    for (int i = 0; i < REPL_EVENT_LOOP_MAX_ITERATIONS; i++) {
        if (!event_loop_run_next(NULL, st)) break;
    }
}

/** @brief Evaluate multiple forms from a string.
 *  @param code String containing multiple forms/expressions
 *  @param st Evaluation state
 *  @return true if successful, false on parse or evaluation error
 */
bool eval_multiform_string(const char *code, EvalState *st) {
    bool result = true; // Start optimistic
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    // While profiling, disable callsite cache for reparsed REPL inputs.
    uint64_t saved_epoch = g_runtime.resolve_cache_epoch;
    g_runtime.resolve_cache_epoch = 0;
#endif

    Reader reader;
    reader_init(&reader, code);
    reader_set_source_name(&reader, "<repl input>");

    // Loop: Parse and evaluate each expression until EOF
    while (!reader_is_eof(&reader)) {
        // Skip whitespace and comments
        reader_skip_all(&reader);

        // Check if we're at EOF after skipping whitespace
        if (reader_is_eof(&reader)) {
            break;
        }

        bool should_break = false;
        WITH_AUTORELEASE_POOL({
        // Save current namespace in local variable before TRY - preserved by setjmp
        CljNamespace *saved_ns = st ? st->current_ns : NULL;

        TRY {
            CljValue parsed = parse_from_reader(&reader, st);
            ID eval_result = eval_parsed_value(parsed, st);
            print_result(eval_result);
            if (reader_is_eof(&reader)) { should_break = true; }
        } CATCH(ex) {
            if (st && saved_ns) { st->current_ns = saved_ns; }
            print_exception((CLJException*)ex);
            result = false;
            while (!reader_is_eof(&reader) && reader_current(&reader) != '\n')
                reader_next(&reader);
            if (!reader_is_eof(&reader)) { reader_next(&reader); }
        } END_TRY

        });

        if (should_break) { break; }
    }

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    g_runtime.resolve_cache_epoch = saved_epoch;
#endif
    return result;
}

static char* unescape_eval_arg(const char *raw_code) {
    CLJ_ASSERT(raw_code != NULL);
    size_t len = strlen(raw_code);
    char *buffer = (char*)CLJ_MALLOC(len + 1);

    size_t w = 0;
    bool changed = false;
    for (size_t r = 0; r < len; r++) {
        char ch = raw_code[r];
        if (ch == '\\' && r + 1 < len) {
            char next = raw_code[++r];
            char replaced = next;
            switch (next) {
                case 'n': replaced = '\n'; break;
                case 'r': replaced = '\r'; break;
                case 't': replaced = '\t'; break;
                case 'b': replaced = '\b'; break;
                case 'f': replaced = '\f'; break;
                case '\\': replaced = '\\'; break;
                case '"': replaced = '"'; break;
                case '\'': replaced = '\''; break;
                default: replaced = next; break;
            }
            if (replaced != next || next == '\\') {
                changed = true;
            }
            buffer[w++] = replaced;
        } else {
            buffer[w++] = ch;
        }
    }
    buffer[w] = '\0';

    if (!changed) {
        CLJ_FREE(buffer);
        return NULL;
    }
    return buffer;
}

bool repl_eval_arg(const char *raw_code, EvalState *st) {
    CLJ_ASSERT(raw_code != NULL);
    CLJ_ASSERT(st != NULL);
    char *unescaped = unescape_eval_arg(raw_code);
    const char *code = unescaped ? unescaped : raw_code;
    bool success = eval_multiform_string(code, st);
    if (unescaped) {
        CLJ_FREE(unescaped);
    }
    return success;
}


// History-Persistenz Funktionen (konsolidiert aus repl_history.c)

/** @brief Trim vector to last N elements
 *  @param vec Vector to trim
 *  @param limit Maximum number of elements to keep
 *  @return New vector with last N elements (or original if smaller)
 */
CljObject* history_trim_last_n(CljPersistentVector *vec, int limit) {
    if (!vec || limit <= 0) return (CljObject*)empty_vector();
    CljPersistentVector *v = vec;
    int count = vector_count(v);
    if (count <= limit) return RETAIN(vec);
    int start = count - limit;
    CljPersistentVector* out = make_vector(limit, STRONG);
    ID nth_args[2];
    nth_args[0] = v;
    for (int i = 0; i < limit; i++) {
        nth_args[1] = fixnum(start + i);
        ID elem = nth2(nth_args, 2);
        if (elem) {
            // nth2 returns element with lifetime tied to vector - no release needed
            out = vector_conj(out, elem);
        }
    }
    return (CljObject*)out;
}

/** @brief Save vector to file as EDN
 *  @param vec Vector to save
 *  @param path File path
 *  @return true if successful
 */
bool history_save_to_file(CljPersistentVector *vec, const char *path) {
    if (!path || !vec) return false;

    CljObject *trimmed = history_trim_last_n(vec, 50);
    if (!trimmed) return false;

    CljString *s = pr_str(trimmed);
    RELEASE(trimmed);
    if (!s) return false;

    FILE *fp = fopen(path, "w");
    if (!fp) {
        return false;
    }

    size_t len = string_length(s);
    size_t n = fwrite(string_data(s), 1, len, fp);
    if (n > 0) fputc('\n', fp);
    fflush(fp);
    // Don't use fsync - it can block and cause REPL to hang
    // fsync(fileno(fp));
    int close_result = fclose(fp);

    return (n == len && close_result == 0);
}

/** @brief Load vector from file (EDN format)
 *  @param path File path
 *  @return Vector loaded from file, or empty vector on error
 */
CljPersistentVector* history_load_from_file(const char *path) {
    if (!path) return empty_vector();

    EvalState *st = get_global_eval_state();
    if (!st) return empty_vector();

    CljPersistentVector *string_history = NULL;

    WITH_AUTORELEASE_POOL({
        TRY {
            // Use file_slurp utility function directly (no eval_string needed)
            // file_slurp throws exceptions on errors
            CljString *content = file_slurp(path);

            if (content) {
                // Read directly from the CljString buffer
                // The Reader only reads from the buffer, it doesn't store the pointer
                const char *buf = clj_string_data(content);
                Reader rd; reader_init(&rd, buf);
                reader_set_source_name(&rd, path);
                ID parsed = value_by_parsing_expr(&rd, st);

                // Validate it's a vector
                if (parsed && TAG(parsed) == CLJ_VECTOR_PERSISTENT) {
                    string_history = as_persistent_vector((CljObject*)parsed);

                    // RETAIN before pool pop to keep it alive
                    RETAIN(string_history);
                }
            }
        } CATCH(ex) {
            // Exception during file_slurp or parsing - return empty vector
            string_history = NULL;
        } END_TRY
    }); // Pool popped, but string_history is retained (rc > 0), so it survives

    evalstate_free(st);

    // Return retained object - caller must release or autorelease it
    return string_history ? string_history : empty_vector();
}


/** @brief Print command-line usage information.
 *  @param prog Program name for usage display
 */
static void __attribute__((unused)) usage(const char *prog) {
    printf("Usage: %s [-n NS] [-e EXPR] [-f FILE] [--no-core] [--repl] [--zombie] [--memory-debug]\n", prog);
    printf("\nOptions:\n");
    printf("  -n, --ns NS          Set namespace\n");
    printf("  -e, --eval EXPR      Evaluate expression (can be used multiple times)\n");
    printf("  -f, --file FILE      Evaluate file\n");
    printf("  --no-core            Don't load clojure.core\n");
    printf("  --repl               Start REPL after evaluating files/expressions\n");
    printf("  --zombie             Enable zombie mode for memory debugging\n");
    printf("  --memory-debug       Enable verbose memory debugging\n");
    printf("  -h, --help           Show this help message\n");
}

/** @brief Clean up resources and exit with specified code.
 *  @param eval_args Array to free before exit
 *  @param exit_code Exit code to use
 */
static void __attribute__((unused)) cleanup_and_exit(const char **eval_args, int exit_code) {
    if (eval_args) CLJ_FREE(eval_args);
    exit(exit_code);
}

/** @brief Run the interactive REPL loop with input handling and evaluation.
 *  @param st Evaluation state for the REPL session
 *  @param zombie_mode Enable zombie mode for memory debugging
 *  @param memory_debug Enable verbose memory debugging
 *  @return true on successful completion
 */
__attribute__((unused)) static bool run_interactive_repl(EvalState *st, bool zombie_mode, bool memory_debug) {
#if !defined(DEBUG)
    CLJ_UNUSED(zombie_mode);
#endif
#if !defined(DEBUG) && !(defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED)
    CLJ_UNUSED(memory_debug);
#endif
    // Initialize memory profiling DIRECTLY before the first prompt
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    // Profiling is enabled at process startup when hooks are compiled in.
    // Only adjust verbosity here.
    set_memory_verbose_mode(memory_debug);
#endif

#ifdef DEBUG
    // Zombie mode is controlled by ZOMBIE_ENABLED macro at compile time
    // Command line flag is ignored - use compile-time flag instead
    (void)zombie_mode; // Suppress unused parameter warning
    // Enable verbose memory debugging if requested
    if (memory_debug) {
        set_memory_verbose_mode(true);
        memory_set_debug_output_enabled(true);
    }
#endif

    printf("tiny-clj %s REPL (platform = %s). Ctrl-D to exit. \n", "0.3", platform_name());
    print_build_info();
#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
    // Line editor needs blocking input for proper character handling
    platform_set_stdin_nonblocking(0);
    // Enable raw mode for proper escape sequence handling
    platform_set_raw_mode(1);
#else
    platform_set_stdin_nonblocking(1);
#endif

    char acc[4096]; acc[0] = '\0';
    bool prompt_shown = false;

#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
    // Initialize line editor
    LineEditor *editor = line_editor_new(platform_get_char, platform_put_char, platform_put_string, NULL);
    if (!editor) {
        fprintf(stderr, "Failed to initialize line editor\n");
        return false;
    }
    set_line_editor(editor);
    // Load history from default file and populate editor history (with exception handling)
    CljObject *history_vec = NULL;
    WITH_AUTORELEASE_POOL({
        TRY {
            CljObject *loaded = line_editor_history_load_default();
            // Only use loaded history if it has content
            if (loaded && TAG(loaded) == CLJ_VECTOR_PERSISTENT && vector_count((CljPersistentVector*)loaded) > 0) {
                // loaded is already retained from history_load_from_file, transfer to outer pool
                ASSIGN(history_vec, AUTORELEASE(loaded));
            }
        } CATCH(ex) {
            // Exception beim History-Laden - starte mit leerer History
            // Exception wird automatisch freigegeben durch CATCH-Macro
            history_vec = NULL;
        } END_TRY
    });
    // Verwende die geladene History
    // line_editor_set_history_from_vector ruft clj_conj auf, das AUTORELEASE verwendet
    if (history_vec && TAG(history_vec) == CLJ_VECTOR_PERSISTENT) {
        WITH_AUTORELEASE_POOL({
            line_editor_set_history_from_vector(editor, (CljPersistentVector*)history_vec);
        });
        RELEASE(history_vec);  // Release nach Verwendung
    } else {
        line_editor_clear_history(editor);
    }
#endif

    // Print initial prompt after line editor init so the editor can track it for redraws.
    print_prompt(st, true);
    prompt_shown = true;

    while (true) {
        // Print prompt only once per input cycle to avoid flooding
        if (!prompt_shown) {
            print_prompt(st, form_balance(acc, NULL) == 0);
            prompt_shown = true;
        }

        // Unified input processing
        bool got_input = false;
#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
        LineEditor *editor = get_line_editor();
        if (editor) {
            int result = line_editor_process_input(editor);
            if (result == LINE_EDITOR_EOF) break;
            if (result == LINE_EDITOR_LINE_READY) {
                size_t line_len = 0;
                const char *line = line_editor_get_buffer_cstr(editor, &line_len);
                if (line && line_len > 0) {
                    if (acc[0] != '\0') strncat(acc, "\n", sizeof(acc) - strlen(acc) - 1);
                    strncat(acc, line, sizeof(acc) - strlen(acc) - 1);
                    line_editor_clear(editor);
                    got_input = true;
                } else {
                    line_editor_clear(editor);
                    prompt_shown = false;
                }
            }
            if (!got_input) {
                repl_process_event_loop(st);
                // macOS stdin is delivered via CFRunLoop callbacks (platform_macos.c).
                // Use the platform runloop as our idle wait to avoid busy-polling.
                platform_runloop_run_once(1);
                continue;
            }
        }
#else
        int once = 200;
        bool should_exit = false;
        while (once--) {
            char buf[512];
            int n = platform_readline_nb(buf, sizeof(buf));
            if (n < 0) { should_exit = true; break; }
            if (n == 0) {
                event_loop_run_next(NULL, st);
                usleep(1000);
                continue;
            }
            if (n > 0) {
                if (acc[0] != '\0') strncat(acc, "\n", sizeof(acc) - strlen(acc) - 1);
                for (int i = 0; i < n; i++) if (buf[i] == '\r') buf[i] = '\n';
                strncat(acc, buf, sizeof(acc) - strlen(acc) - 1);
                got_input = true; break;
            }
        }
        if (should_exit) break;
        if (!got_input) continue;
#endif

        // Check for EOF on stdin (Ctrl+D) - exit immediately, even with unbalanced forms
        if (feof(stdin)) {
            break;
        }

        int balance = form_balance(acc, NULL);
        if (balance > 0) {
            // Incomplete - need more input
            prompt_shown = false; // show continuation prompt once
            continue;
        } else if (balance < 0) {
            // Too many closing parens - syntax error
            platform_put_string(NULL, "Error: Too many closing parentheses\n");
            // Add to history before clearing
            if (acc[0] != '\0') {
#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
                LineEditor *editor = get_line_editor();
                if (editor) {
                    line_editor_add_to_history(editor, acc);
                }
#endif
            }
            acc[0] = '\0';
            prompt_shown = false;
            // Run event loop to process any pending tasks before continuing
            repl_process_event_loop(st);
            continue;
        }
        // balance == 0: evaluate form

        // (Entfernt) REPL interne History-Kommandos

        bool success = eval_multiform_string(acc, st);

        repl_process_event_loop(st);

        // Add to history and save after each expression evaluation (success or failure)
        if (acc[0] != '\0') {
#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
            LineEditor *editor = get_line_editor();
            if (editor) {
                line_editor_add_to_history(editor, acc);
                // Save history after each expression (fsync removed to avoid blocking)
                WITH_AUTORELEASE_POOL({
                    CljPersistentVector *vec = line_editor_get_history_vector(editor);
                    if (vec) {
                        line_editor_history_save_default((CljObject*)vec);
                        RELEASE(vec);
                    }
                });
            }
#endif
        }

        if (!success) {
            // Error already printed by eval_string_repl
        }

        acc[0] = '\0';
        prompt_shown = false; // show fresh prompt after evaluation
    }

    // Auto-Save History on REPL exit
#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
    WITH_AUTORELEASE_POOL({
        LineEditor *ed = get_line_editor();
        if (ed) {
            CljPersistentVector *vec = line_editor_get_history_vector(ed);
            if (vec) {
                line_editor_history_save_default((CljObject*)vec);
                RELEASE(vec);
            }
        }
    });
#endif


    return true;
}

#ifndef UNITY_TESTS
int main(int argc, char **argv) {
#ifdef PROFILE_STARTUP
    #include <time.h>
    clock_t t0 = clock();
#endif
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    // Build-time switch: if memory profiling hooks are compiled in, enable them
    // for the whole process so we can inspect current/peak after core loading.
    // Keep stats cumulative from process start (no reset here).
    MEMORY_PROFILER_INIT();
    g_memory_profiling_enabled = true;
    set_memory_leak_reporting_enabled(false);
    set_memory_verbose_mode(false);
    memory_set_debug_output_enabled(memory_get_debug_output_enabled());
#endif
    // Install SIGTRAP handler for startup diagnostics.
    signal(SIGTRAP, repl_sigtrap_handler);
    platform_init();
    runtime_init(&g_runtime);
    meta_registry_init();  // Initialize metadata registry
    init_special_symbols();  // Initialize special symbols like SYM_DEF
#ifdef PROFILE_STARTUP
    clock_t t1 = clock();
    fprintf(stderr, "[PROFILE] init: %.2f ms\n", (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);
#endif
    EvalState *st = get_global_eval_state();
    // Note: set_global_eval_state() removed - Exception handling now independent
    evalstate_set_ns(st, "user");
    // Quiet mode for CLI eval (no banner)
    bool no_core = false;
    if (argc > 1) clojure_core_set_quiet(1);

    const char *ns_arg = NULL;
    const char **eval_args = NULL;
    int eval_count = 0;
    const char *file_arg = NULL;
    bool start_repl = false;
    bool zombie_mode = false;
    bool memory_debug = false;

    // First pass: count -e arguments
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eval") == 0) && i + 1 < argc) {
            eval_count++;
            i++; // skip the argument value
        }
    }

    // Allocate array for eval arguments
    if (eval_count > 0) {
        eval_args = CLJ_MALLOC(sizeof(char*) * eval_count);
    }

    // Second pass: collect all arguments
    int eval_idx = 0;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--ns") == 0) && i + 1 < argc) {
            ns_arg = argv[++i];
        } else if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eval") == 0) && i + 1 < argc) {
            eval_args[eval_idx++] = argv[++i];
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            file_arg = argv[++i];
        } else if (strcmp(argv[i], "--no-core") == 0) {
            no_core = true;
        } else if (strcmp(argv[i], "--repl") == 0) {
            start_repl = true;
        } else if (strcmp(argv[i], "--zombie") == 0) {
            zombie_mode = true;
        } else if (strcmp(argv[i], "--memory-debug") == 0) {
            memory_debug = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            cleanup_and_exit(eval_args, 0);
        } else {
            usage(argv[0]);
            cleanup_and_exit(eval_args, 1);
        }
    }

    // Register builtin functions and load clojure.core in autorelease pool
    // Both operations may use AUTORELEASE calls (LIST_FIRST/LIST_REST macros)
    WITH_AUTORELEASE_POOL({
#ifdef PROFILE_STARTUP
        clock_t t2 = clock();
#endif
        // Register builtin functions first (they may be used during core loading)
        register_builtins();
#ifdef PROFILE_STARTUP
        clock_t t3 = clock();
        fprintf(stderr, "[PROFILE] register_builtins: %.2f ms\n", (double)(t3 - t2) * 1000.0 / CLOCKS_PER_SEC);
#endif

        if (!no_core) {
#ifdef PROFILE_STARTUP
            clock_t t4 = clock();
#endif
            // Load clojure.core in autorelease pool to handle AUTORELEASE calls
#ifdef DEBUG
            if (memory_debug) {
                autorelease_pool_peak_reset();
            }
#endif
            load_clojure_core(st);
#ifdef DEBUG
            if (memory_debug) {
                fprintf(stderr, "[DEBUG] autorelease_pool peak during load_clojure_core: %u\n",
                        (unsigned)autorelease_pool_peak_count());
            }
#endif
#ifdef PROFILE_STARTUP
            clock_t t5 = clock();
            fprintf(stderr, "[PROFILE] load_clojure_core: %.2f ms\n", (double)(t5 - t4) * 1000.0 / CLOCKS_PER_SEC);
#endif
            // Load clojure.repl namespace for REPL helper functions
            load_clojure_repl(st);
#ifdef PROFILE_STARTUP
            clock_t t6 = clock();
            fprintf(stderr, "[PROFILE] load_clojure_repl: %.2f ms\n", (double)(t6 - t5) * 1000.0 / CLOCKS_PER_SEC);
#endif
            // Require clojure.repl with :refer :all to make functions available in user namespace
            // This ensures functions like doc, source, dir, etc. are available without namespace prefix
            evalstate_set_ns(st, "user");
            const char *require_code = "(require '[clojure.repl :refer :all])";
            eval_multiform_string(require_code, st);
#ifdef PROFILE_STARTUP
            clock_t t7 = clock();
            fprintf(stderr, "[PROFILE] require clojure.repl: %.2f ms\n", (double)(t7 - t6) * 1000.0 / CLOCKS_PER_SEC);
            fprintf(stderr, "[PROFILE] TOTAL startup: %.2f ms\n", (double)(t7 - t0) * 1000.0 / CLOCKS_PER_SEC);
#endif
        }
    });

    if (ns_arg) {
        evalstate_set_ns(st, ns_arg);
    } else {
        // After loading clojure.core, explicitly return to user namespace
        evalstate_set_ns(st, "user");
    }

    if (file_arg) {
        // Load entire file into memory for proper parsing (handles metadata across lines)
        FILE *fp = fopen(file_arg, "r");
        if (!fp) {
            printf("Error: Cannot open file '%s': %s\n", file_arg, strerror(errno));
            cleanup_and_exit(eval_args, 1);
        }

        // Get file size
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            printf("Error: Cannot seek in file '%s': %s\n", file_arg, strerror(errno));
            cleanup_and_exit(eval_args, 1);
        }
        long sz = ftell(fp);
        if (sz < 0) {
            fclose(fp);
            printf("Error: Cannot get file size for '%s': %s\n", file_arg, strerror(errno));
            cleanup_and_exit(eval_args, 1);
        }
        rewind(fp);

        // Allocate buffer (sz + 1 for null terminator)
        char *buffer = (char*)CLJ_MALLOC((size_t)sz + 1);

        // Read entire file
        size_t n = fread(buffer, 1, (size_t)sz, fp);
        buffer[n] = '\0';
        fclose(fp);

        // Evaluate entire file content
        bool success = eval_multiform_string(buffer, st);
        CLJ_FREE(buffer);
        if (!success) {
            cleanup_and_exit(eval_args, 1);
        }
        if (!start_repl && eval_count == 0) {
            cleanup_and_exit(eval_args, 0);
        }
    }

    cleanup_line_editor();

    // Execute all -e arguments in order
    int i = 0;
    while (i < eval_count) {
        // Simple eval-args without TRY/CATCH
        bool success = repl_eval_arg(eval_args[i], st);
        if (!success) {
            // Parse error or evaluation failed
            cleanup_and_exit(eval_args, 1);
        }
        i++;
    }

    if (eval_count > 0 && !start_repl) {
        cleanup_and_exit(eval_args, 0);
    }

    bool stdin_is_tty = isatty(STDIN_FILENO) != 0;
    if (!stdin_is_tty) {
        size_t capacity = 4096;
        size_t len = 0;
        char *buffer = (char*)CLJ_MALLOC(capacity);

        int ch;
        while ((ch = fgetc(stdin)) != EOF) {
            if (len + 1 >= capacity) {
                size_t new_cap = capacity * 2;
                char *tmp = (char*)CLJ_REALLOC(buffer, new_cap);
                buffer = tmp;
                capacity = new_cap;
            }
            buffer[len++] = (char)ch;
        }
        buffer[len] = '\0';

        if (len == 0) {
            CLJ_FREE(buffer);
            cleanup_and_exit(eval_args, 0);
        }

        bool success = eval_multiform_string(buffer, st);
        CLJ_FREE(buffer);
        cleanup_and_exit(eval_args, success ? 0 : 1);
    }

    // Interactive REPL
    run_interactive_repl(st, zombie_mode, memory_debug);

#if defined(LINE_EDITING_ENABLED) && LINE_EDITING_ENABLED
    // Restore terminal settings
    platform_set_raw_mode(0);
#endif

    // Free EvalState before exit (no memory leaks)
    evalstate_free(st);

    return 0;
}
#endif // UNITY_TESTS
