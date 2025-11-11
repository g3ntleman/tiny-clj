#include "platform.h"
#include "tiny_clj.h"
#include "parser.h"
#include "namespace.h"
#include "object.h"
#include "exception.h"
#include "builtins.h"
#include "memory_profiler.h"
#include "line_editor.h"
#include "symbol.h"
#include "clj_strings.h"
#include "strings.h"
#include "reader.h"
#include "runtime.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include "builtins.h"
#include "event_loop.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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
    if (st && st->current_ns && st->current_ns->name) {
        CljSymbol *sym = as_symbol(st->current_ns->name);
        if (sym && sym->name && sym->name[0] != '\0') {
            ns_name = sym->name;
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
    const char *s = pr_str(v);
    if (s) {
        printf("%s\n", s);
        free((void*)s);
    }
}

/** @brief Evaluate multiple expressions from a multiline string.
 *  @param code Multiline string containing multiple expressions
 *  @param st Evaluation state
 *  @return true if successful, false on parse or evaluation error
 */
static bool eval_multiline_string(const char *code, EvalState *st) {
    bool result = true; // Start optimistic
    
    // Use WITH_AUTORELEASE_POOL for automatic cleanup
    WITH_AUTORELEASE_POOL({
        Reader reader;
        reader_init(&reader, code);
        
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
                
                // Check if parsing failed (NULL could mean EOF or parsing error)
                if (parsed == NULL) {
                    // Check if we're at EOF
                    if (reader_is_eof(&reader)) {
                        break; // Normal EOF, exit loop
                    } else {
                        // Parsing error - this should have thrown an exception
                        // If we get here, it's unexpected
                        result = false;
                        break;
                    }
                }
                
                // Evaluate the parsed expression
                ID eval_result = eval_parsed(parsed, st, NULL);
                
                // Print the result (can be NULL for nil)
                print_result(eval_result);
                
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


// History-Persistenz Funktionen (konsolidiert aus repl_history.c)

/** @brief Trim vector to last N elements
 *  @param vec Vector to trim
 *  @param limit Maximum number of elements to keep
 *  @return New vector with last N elements (or original if smaller)
 */
CljObject* history_trim_last_n(CljObject *vec, int limit) {
    if (!vec || TAG(vec) != CLJ_VECTOR || limit <= 0) return (CljObject*)empty_vector();
    CljPersistentVector *v = as_vector(vec);
    int count = vector_count(v);
    if (count <= limit) return RETAIN(vec);
    int start = count - limit;
    CljPersistentVector* out = make_vector(limit, false);
    ID nth_args[2];
    nth_args[0] = (ID)v;
    for (int i = 0; i < limit; i++) {
        nth_args[1] = fixnum(start + i);
        ID elem = nth2(nth_args, 2);
        if (elem) {
            out = vector_conj(out, elem);
            RELEASE(elem);
        }
    }
    return (CljObject*)out;
}

/** @brief Save vector to file as EDN
 *  @param vec Vector to save
 *  @param path File path
 *  @return true if successful
 */
bool history_save_to_file(CljObject *vec, const char *path) {
    if (!path || !vec) return false;
    
    CljObject *persistent_vec = vec;
    if (TAG(vec) == CLJ_TRANSIENT_VECTOR) {
        persistent_vec = (CljObject*)persistent((CljValue)vec);
        if (!persistent_vec || TAG(persistent_vec) != CLJ_VECTOR) {
            if (persistent_vec != vec) RELEASE(persistent_vec);
            return false;
        }
    }
    
    if (TAG(persistent_vec) != CLJ_VECTOR) {
        if (persistent_vec != vec) RELEASE(persistent_vec);
        return false;
    }
    
    CljObject *trimmed = history_trim_last_n(persistent_vec, 50);
    if (persistent_vec != vec) RELEASE(persistent_vec);
    if (!trimmed) return false;
    
    const char *s = pr_str(trimmed);
    RELEASE(trimmed);
    if (!s) return false;
    
    FILE *fp = fopen(path, "w");
    if (!fp) {
        free((void*)s);
        return false;
    }
    
    size_t len = strlen(s);
    size_t n = fwrite(s, 1, len, fp);
    if (n > 0) fputc('\n', fp);
    fflush(fp);
    fsync(fileno(fp));
    int close_result = fclose(fp);
    free((void*)s);
    
    return (n == len && close_result == 0);
}

/** @brief Load vector from file (EDN format)
 *  @param path File path
 *  @return Vector loaded from file, or empty vector on error
 */
ID history_load_from_file(const char *path) {
    if (!path) return empty_vector();
    
    EvalState *st = evalstate_new(false);
    if (!st) return empty_vector();
    
    ID result = NULL;
    ID form = NULL;
    char *buf_copy = NULL;
    
    WITH_AUTORELEASE_POOL({
        TRY {
            char expr[512];
            snprintf(expr, sizeof(expr), "(slurp \"%s\")", path);
            ID slurp_result = eval_string(expr, st);
            
            if (!slurp_result || TAG(slurp_result) != CLJ_STRING) {
                // slurp returned nil or non-string - file doesn't exist or error
                result = NULL;
            } else {
                // Copy string buffer to avoid dependency on inner pool
                // The string from slurp is in the inner pool (from eval_string)
                // We need to copy the buffer before the inner pool is popped
                CljString *content = as_clj_string((CljObject*)slurp_result);
                const char *buf = clj_string_data(content);
                size_t buf_len = strlen(buf);
                buf_copy = (char*)malloc(buf_len + 1);
                if (!buf_copy) {
                    result = NULL;
                } else {
                    memcpy(buf_copy, buf, buf_len + 1);
                    
                    // Parse in our own pool to avoid dependency on inner pool
                    // The parsed objects will be in our pool, not the inner pool
                    Reader rd; reader_init(&rd, buf_copy);
                    form = value_by_parsing_expr(&rd, st);
                    
                    // RETAIN form to keep it alive after pool is popped
                    // The form contains strings that need to survive the pool pop
                    if (form && !IS_IMMEDIATE(form)) {
                        RETAIN((CljObject*)form);
                    }
                }
            }
        } CATCH(ex) {
            // Exception during slurp or parsing - return empty vector
            result = NULL;
        } END_TRY
    }); // Pool is popped here, but form is retained
    
    // Free buffer copy after parsing (outside pool)
    if (buf_copy) {
        free(buf_copy);
    }
    
    // Process form after pool is popped
    if (form && !IS_IMMEDIATE(form) && TAG(form) == CLJ_VECTOR) {
        CljPersistentVector *v = as_vector((CljObject*)form);
        int count = vector_count(v);
        bool all_strings = count > 0;
        ID nth_args[2];
        nth_args[0] = form;
        for (int i = 0; i < count && all_strings; i++) {
            nth_args[1] = fixnum(i);
            ID elem = nth2(nth_args, 2);
            if (elem && TAG(elem) != CLJ_STRING) {
                all_strings = false;
            }
            if (elem) RELEASE(elem);
        }
        if (all_strings) {
            CljPersistentVector* new_vec = make_vector(count, false);
            for (int i = 0; i < count; i++) {
                nth_args[1] = fixnum(i);
                ID elem = nth2(nth_args, 2);
                if (elem) {
                    ASSIGN(new_vec, vector_conj(new_vec, elem));
                    RELEASE(elem);
                }
            }
            result = new_vec;
        }
        
        // RELEASE form after we've copied its contents to the new vector
        RELEASE((CljObject*)form);
    } else if (form && !IS_IMMEDIATE(form)) {
        // RELEASE form if it's not a vector
        RELEASE((CljObject*)form);
    }
    
    evalstate_free(st);
    return result ? AUTORELEASE(result) : empty_vector();
}



/** @brief Print command-line usage information.
 *  @param prog Program name for usage display
 */
__attribute__((unused)) static void usage(const char *prog) {
    printf("Usage: %s [-n NS] [-e EXPR] [-f FILE] [--no-core] [--repl]\n", prog);
}

/** @brief Clean up resources and exit with specified code.
 *  @param eval_args Array to free before exit
 *  @param exit_code Exit code to use
 */
__attribute__((unused)) static void cleanup_and_exit(const char **eval_args, int exit_code) {
    if (eval_args) free(eval_args);
    exit(exit_code);
}

/** @brief Run the interactive REPL loop with input handling and evaluation.
 *  @param st Evaluation state for the REPL session
 *  @return true on successful completion
 */
__attribute__((unused)) static bool run_interactive_repl(EvalState *st) {
    // Initialize memory profiling DIRECTLY before the first prompt
#ifdef ENABLE_MEMORY_PROFILING
    MEMORY_PROFILER_INIT();
    enable_memory_profiling(true);
    
    // Disable verbose memory mode for REPL (memory logging disabled)
    g_memory_verbose_mode = false;
#endif

#ifdef DEBUG
    // Enable zombie mode for debugging use-after-free errors
    enable_zombie_mode();
    // Enable verbose memory mode for debugging
    set_memory_verbose_mode(true);
    enable_memory_debug_output();
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
#ifdef ENABLE_LINE_EDITING
    // Initialize line editor
    LineEditor *editor = line_editor_new(platform_get_char, platform_put_char, platform_put_string);
    if (!editor) {
        fprintf(stderr, "Failed to initialize line editor\n");
        return false;
    }
    set_line_editor(editor);
    // Lade History aus Default-Datei und fülle Editor-History (mit Autorelease-Pool)
    CljObject *history_vec = NULL;
    WITH_AUTORELEASE_POOL({
        TRY {
            CljObject *loaded = line_editor_history_load_default();
            if (loaded && TAG(loaded) == CLJ_VECTOR) {
                // RETAIN to keep history alive when pool is popped
                ASSIGN(history_vec, RETAIN(loaded));
            }
            } CATCH(ex) {
                fprintf(stderr, "Warning: Failed to load history file. History has been reset.\n");
                history_vec = NULL;
            } END_TRY
    });  // Pool is popped here, but history_vec is retained, so it's not freed
    // Now use the retained history vector
    if (history_vec && TAG(history_vec) == CLJ_VECTOR) {
        line_editor_set_history_from_vector(editor, (CljPersistentVector*)history_vec);
        RELEASE(history_vec);  // Release after use
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
                for (int i = 0; i < 10; i++) {
                    if (!event_loop_run_next(NULL, st)) break;
                }
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
            continue;
        }
        // balance == 0: evaluate form

        // (Entfernt) REPL interne History-Kommandos

        bool success = eval_multiline_string(acc, st);
        
        for (int i = 0; i < 10; i++) {
            if (!event_loop_run_next(NULL, st)) break;
        }
        if (acc[0] != '\0') {
#ifdef ENABLE_LINE_EDITING
            LineEditor *editor = get_line_editor();
            if (editor) {
                line_editor_add_to_history(editor, acc);
                // Save history after each expression evaluation
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
#ifdef ENABLE_LINE_EDITING
    WITH_AUTORELEASE_POOL({
        LineEditor *ed = get_line_editor();
        if (ed) {
            CljPersistentVector *vec = line_editor_get_history_vector(ed);
            if (vec) { line_editor_history_save_default((CljObject*)vec); RELEASE(vec); }
        }
    });
#endif


    return true;
}

#ifndef UNITY_TESTS
int main(int argc, char **argv) {
    platform_init();
    runtime_init();
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
        }
    });

    if (ns_arg) {
        evalstate_set_ns(st, ns_arg);
    } else {
        // Nach dem Laden von clojure.core explizit zurück in den user-Namespace
        evalstate_set_ns(st, "user");
    }

    if (file_arg) {
        // Simple file evaluation without TRY/CATCH
        FILE *fp = fopen(file_arg, "r");
        if (!fp) {
            printf("Error: Cannot open file '%s': %s\n", file_arg, strerror(errno));
            cleanup_and_exit(eval_args, 1);
        }
        
        char line[1024];
        char acc[8192]; acc[0] = '\0';
        while (fgets(line, sizeof(line), fp)) {
            strncat(acc, line, sizeof(acc) - strlen(acc) - 1);
            if (form_balance(acc, NULL) != 0) continue;
            
            bool success = eval_multiline_string(acc, st);
            if (!success) {
                // Parse error or evaluation failed
                fclose(fp);
                cleanup_and_exit(eval_args, 1);
            }
            acc[0] = '\0';
        }
        fclose(fp);
        if (!start_repl && eval_count == 0) {
            cleanup_and_exit(eval_args, 0);
        }
    }

    cleanup_line_editor();

    // Execute all -e arguments in order
    int i = 0;
    while (i < eval_count) {
        // Simple eval-args without TRY/CATCH
        bool success = eval_multiline_string(eval_args[i], st);
        if (!success) {
            // Parse error or evaluation failed
            cleanup_and_exit(eval_args, 1);
        }
        i++;
    }
    
    if (eval_count > 0 && !start_repl) {
        cleanup_and_exit(eval_args, 0);
    }

    // Interactive REPL
    run_interactive_repl(st);
    
#ifdef ENABLE_LINE_EDITING
    // Restore terminal settings
    platform_set_raw_mode(0);
#endif
    
    // Free EvalState before exit (no memory leaks)
    evalstate_free(st);
    
    return 0;
}
#endif // UNITY_TESTS


