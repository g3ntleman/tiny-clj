#include "platform.h"
#include "common.h"
#include "tiny_clj.h"
#include "repl.h"
#include "parser.h"
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "object.h"
#include "exception.h"
#include "builtins.h"
#include "memory_profiler.h"
#include "line_editor.h"
#include "strings.h"
#include "reader.h"
#include "runtime.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include "event_loop.h"
#include "file_utils.h"
#include "meta.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Maximum number of event loop iterations per REPL cycle
// This limits processing to prevent blocking while still processing pending tasks
#define REPL_EVENT_LOOP_MAX_ITERATIONS 10

// Forward decls for line editor history persistence helpers
extern CljObject* line_editor_history_load_default(void);
extern bool line_editor_history_save_default(CljObject *vec);
extern void set_line_editor(LineEditor *editor);
extern LineEditor* get_line_editor(void);
extern CljVector* line_editor_get_history_vector(LineEditor *editor);
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
    printf("%s%s ", ns_name, balanced ? "=>" : "...");
    fflush(stdout);
}

/** @brief Print a CljObject result to stdout with proper formatting.
 *  @param v Object to print (can be NULL)
 */
static void print_result(CljObject *v) {
    if (!v) {
        printf("nil\n");
        return;
    }
    CljString *s = pr_str(v);
    if (s) {
        printf("%s\n", string_data(s));
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

    // Use WITH_AUTORELEASE_POOL for automatic cleanup
    WITH_AUTORELEASE_POOL({
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

            // Use TRY/CATCH to handle exceptions for each expression
            TRY {
                // Parse one expression using the new parse_from_reader function
                CljValue parsed = parse_from_reader(&reader, st);

                // Evaluate the parsed expression (can be NULL for nil, e.g., () parses to nil)
                ID eval_result = eval_parsed(parsed, st, NULL);

                // Print the result (can be NULL for nil)
                print_result(eval_result);

                // Check for EOF after processing (in case this was the last expression)
                if (reader_is_eof(&reader)) {
                    break; // Normal EOF, exit loop
                }

            } CATCH(ex) {
                // Print exception and continue with next expression
                print_exception((CLJException*)ex);
                result = false; // Mark as failed, but continue processing
                // Note: History is saved after evaluation in run_interactive_repl
                // No need to save here to avoid double-saving and memory issues

                // Skip to next line to avoid infinite loop on same expression
                while (!reader_is_eof(&reader) && reader_current(&reader) != '\n') {
                    reader_next(&reader);
                }
                if (!reader_is_eof(&reader)) {
                    reader_next(&reader); // consume the newline
                }
            } END_TRY
        }
    });

    return result;
}

static char* unescape_eval_arg(const char *raw_code) {
    CLJ_ASSERT(raw_code != NULL);
    size_t len = strlen(raw_code);
    char *buffer = (char*)malloc(len + 1);
    if (!buffer) {
        return NULL;
    }

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
        free(buffer);
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
        free(unescaped);
    }
    return success;
}


// History-Persistenz Funktionen (konsolidiert aus repl_history.c)

/** @brief Trim vector to last N elements
 *  @param vec Vector to trim
 *  @param limit Maximum number of elements to keep
 *  @return New vector with last N elements (or original if smaller)
 */
CljObject* history_trim_last_n(CljObject *vec, int limit) {
    if (!vec || TAG(vec) != CLJ_VECTOR || limit <= 0) return (CljObject*)empty_vector();
    CljVector *v = as_vector(vec);
    int count = vector_count(v);
    if (count <= limit) return RETAIN(vec);
    int start = count - limit;
    CljVector* out = make_vector(limit, CLJ_VECTOR);
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
bool history_save_to_file(CljVector *vec, const char *path) {
    if (!path || !vec) return false;

    CljObject *persistent_vec = (CljObject*)vec;
    if (TAG((CljObject*)vec) == CLJ_VECTOR_TRANSIENT) {
        persistent_vec = (CljObject*)vector_persistent(vec);
        if (!persistent_vec || TAG(persistent_vec) != CLJ_VECTOR) {
            if (persistent_vec != (CljObject*)vec) RELEASE(persistent_vec);
            return false;
        }
    }

    if (TAG(persistent_vec) != CLJ_VECTOR) {
        if (persistent_vec != (CljObject*)vec) RELEASE(persistent_vec);
        return false;
    }

    CljObject *trimmed = history_trim_last_n(persistent_vec, 50);
    if (persistent_vec != (CljObject*)vec) RELEASE(persistent_vec);
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
CljVector* history_load_from_file(const char *path) {
    if (!path) return empty_vector();

    EvalState *st = evalstate_new(false);
    if (!st) return empty_vector();

    CljVector *string_history = NULL;

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
                if (parsed && TAG(parsed) == CLJ_VECTOR) {
                    string_history = as_vector((CljObject*)parsed);

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
    if (eval_args) free(eval_args);
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
#if !defined(DEBUG) && !defined(ENABLE_MEMORY_PROFILING)
    CLJ_UNUSED(memory_debug);
#endif
    // Initialize memory profiling DIRECTLY before the first prompt
#ifdef ENABLE_MEMORY_PROFILING
    MEMORY_PROFILER_INIT();
    enable_memory_profiling(true);

    // Set verbose memory mode based on command line argument
    g_memory_verbose_mode = memory_debug;
#endif

#ifdef DEBUG
    // Enable zombie mode if requested via command line
    if (zombie_mode) {
        enable_zombie_mode();
    }
    // Enable verbose memory debugging if requested
    if (memory_debug) {
        set_memory_verbose_mode(true);
        enable_memory_debug_output();
    }
#endif

    printf("tiny-clj %s REPL (platform = %s). Ctrl-D to exit. \n", "0.1", platform_name());
#ifdef ENABLE_LINE_EDITING
    // Line editor needs blocking input for proper character handling
    platform_set_stdin_nonblocking(0);
    // Enable raw mode for proper escape sequence handling
    platform_set_raw_mode(1);
#else
    platform_set_stdin_nonblocking(1);
#endif

    char acc[4096]; acc[0] = '\0';
    bool prompt_shown = false;

    // Print initial prompt immediately (before history loading to ensure it's visible)
    print_prompt(st, true);
    prompt_shown = true;

#ifdef ENABLE_LINE_EDITING
    // Initialize line editor
    LineEditor *editor = line_editor_new(platform_get_char, platform_put_char, platform_put_string);
    if (!editor) {
        fprintf(stderr, "Failed to initialize line editor\n");
        return false;
    }
    set_line_editor(editor);
    // Lade History aus Default-Datei und fülle Editor-History (mit Exception-Handling)
    CljObject *history_vec = NULL;
    WITH_AUTORELEASE_POOL({
        TRY {
            CljObject *loaded = line_editor_history_load_default();
            // Only use loaded history if it has content
            if (loaded && TAG(loaded) == CLJ_VECTOR && vector_count((CljVector*)loaded) > 0) {
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
    if (history_vec && TAG(history_vec) == CLJ_VECTOR) {
        line_editor_set_history_from_vector(editor, (CljVector*)history_vec);
        RELEASE(history_vec);  // Release nach Verwendung
    } else {
        line_editor_clear_history(editor);
    }
#endif

    while (true) {
        // Print prompt only once per input cycle to avoid flooding
        if (!prompt_shown) {
            print_prompt(st, form_balance(acc, NULL) == 0);
            prompt_shown = true;
        }

        // Unified input processing
        bool got_input = false;
#ifdef ENABLE_LINE_EDITING
        LineEditor *editor = get_line_editor();
        if (editor) {
            int result = line_editor_process_input(editor);
            if (result == LINE_EDITOR_EOF) break;
            if (result == LINE_EDITOR_LINE_READY) {
                LineEditorState state;
                if (line_editor_get_state(editor, &state) == LINE_EDITOR_SUCCESS &&
                    state.length > 0) {
                    if (acc[0] != '\0') strncat(acc, "\n", sizeof(acc) - strlen(acc) - 1);
                    strncat(acc, state.buffer, sizeof(acc) - strlen(acc) - 1);
                    line_editor_reset(editor);
                    got_input = true;
                }
            }
            if (!got_input) {
                repl_process_event_loop(st);
                usleep(1000);
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
            printf("Error: Too many closing parentheses\n");
            // Add to history before clearing
            if (acc[0] != '\0') {
#ifdef ENABLE_LINE_EDITING
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
#ifdef ENABLE_LINE_EDITING
            LineEditor *editor = get_line_editor();
            if (editor) {
                line_editor_add_to_history(editor, acc);
                // Save history after each expression (fsync removed to avoid blocking)
                WITH_AUTORELEASE_POOL({
                    CljVector *vec = line_editor_get_history_vector(editor);
                    if (vec) {
                        // RETAIN before passing to save function (it may convert transient to persistent)
                        RETAIN((CljObject*)vec);
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
#ifdef ENABLE_LINE_EDITING
    WITH_AUTORELEASE_POOL({
        LineEditor *ed = get_line_editor();
        if (ed) {
            CljVector *vec = line_editor_get_history_vector(ed);
            if (vec) {
                // RETAIN before passing to save function (it may convert transient to persistent)
                RETAIN((CljObject*)vec);
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
    platform_init();
    runtime_init(&g_runtime);
    meta_registry_init();  // Initialize metadata registry
    init_special_symbols();  // Initialize special symbols like SYM_DEF
    EvalState *st = evalstate_new(false);
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
        eval_args = malloc(sizeof(char*) * eval_count);
        if (!eval_args) return 1;
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
        // Register builtin functions first (they may be used during core loading)
        register_builtins();

        if (!no_core) {
            // Load clojure.core in autorelease pool to handle AUTORELEASE calls
            load_clojure_core(st);
            // Load clojure.repl namespace for REPL helper functions
            load_clojure_repl(st);
            // Require clojure.repl with :refer :all to make functions available in user namespace
            // This ensures functions like doc, source, dir, etc. are available without namespace prefix
            evalstate_set_ns(st, "user");
            const char *require_code = "(require '[clojure.repl :refer :all])";
            eval_multiform_string(require_code, st);
        }
    });

    if (ns_arg) {
        evalstate_set_ns(st, ns_arg);
    } else {
        // Nach dem Laden von clojure.core explizit zurück in den user-Namespace
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
        char *buffer = (char*)malloc((size_t)sz + 1);
        if (!buffer) {
            fclose(fp);
            printf("Error: Out of memory\n");
            cleanup_and_exit(eval_args, 1);
        }

        // Read entire file
        size_t n = fread(buffer, 1, (size_t)sz, fp);
        buffer[n] = '\0';
        fclose(fp);

        // Evaluate entire file content
        bool success = eval_multiform_string(buffer, st);
        free(buffer);
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
        char *buffer = (char*)malloc(capacity);
        if (!buffer) {
            fprintf(stderr, "Error: Out of memory while reading stdin\n");
            cleanup_and_exit(eval_args, 1);
        }

        int ch;
        while ((ch = fgetc(stdin)) != EOF) {
            if (len + 1 >= capacity) {
                size_t new_cap = capacity * 2;
                char *tmp = (char*)realloc(buffer, new_cap);
                if (!tmp) {
                    free(buffer);
                    fprintf(stderr, "Error: Out of memory while reading stdin\n");
                    cleanup_and_exit(eval_args, 1);
                }
                buffer = tmp;
                capacity = new_cap;
            }
            buffer[len++] = (char)ch;
        }
        buffer[len] = '\0';

        if (len == 0) {
            free(buffer);
            cleanup_and_exit(eval_args, 0);
        }

        bool success = eval_multiform_string(buffer, st);
        free(buffer);
        cleanup_and_exit(eval_args, success ? 0 : 1);
    }

    // Interactive REPL
    run_interactive_repl(st, zombie_mode, memory_debug);

#ifdef ENABLE_LINE_EDITING
    // Restore terminal settings
    platform_set_raw_mode(0);
#endif

    // Free EvalState before exit (no memory leaks)
    evalstate_free(st);

    return 0;
}
#endif // UNITY_TESTS


