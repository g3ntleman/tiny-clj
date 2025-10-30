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
#include "reader.h"
#include "runtime.h"
#include "vector.h"
#include "memory.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// Forward decls for line editor history persistence helpers
extern CljObject* line_editor_history_load_default(void);
extern bool line_editor_history_save_default(CljObject *vec);
extern void set_line_editor(LineEditor *editor);
extern LineEditor* get_line_editor(void);
extern CljObject* line_editor_get_history_vector(LineEditor *editor);
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
        if (sym && sym->name[0] != '\0') {
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
    
    // For symbols, print their name with namespace if present
    if (is_type(v, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol(v);
        if (sym && sym->name[0] != '\0') {
            // Delegate to pr_str for correct formatting (handles :: and ns/)
            char *s = pr_str(v);
            if (s) {
                printf("%s\n", s);
                free(s);
            }
            return;
        }
    }
    
    char *s = pr_str(v);
    if (s) { printf("%s\n", s); free(s); }
}

/** @brief Print exception details to stderr with location information.
 *  @param ex Exception to print (can be NULL)
 */
static void print_exception(CLJException *ex) {
    if (!ex) {
        fprintf(stderr, "EXCEPTION: NULL exception\n");
        return;
    }
    
    // Safely print exception with bounds checking
    fprintf(stderr, "EXCEPTION: %.127s: %.255s at %.127s:%d:%d\n",
        ex->type, ex->message, ex->file, ex->line, ex->col);
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
                CljObject *eval_result = eval_parsed(parsed, st);
                
                // Print the result (can be NULL for nil)
                print_result(eval_result);
                
            } CATCH(ex) {
                // Print exception and continue with next expression
                print_exception((CLJException*)ex);
                result = false; // Mark as failed, but continue processing
                // Auto-Save History on exception
#ifdef ENABLE_LINE_EDITING
                WITH_AUTORELEASE_POOL({
                    LineEditor *ed = get_line_editor();
                    if (ed) {
                        CljObject *vec = line_editor_get_history_vector(ed);
                        if (vec) { line_editor_history_save_default(vec); RELEASE(vec); }
                    }
                });
#endif
                
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

/** @brief Evaluate a string expression in the REPL context.
 *  @param code Expression string to evaluate
 *  @param st Evaluation state
 *  @return true if successful, false on parse or evaluation error
 */
static bool eval_string_repl(const char *code, EvalState *st) {
    // Use the new multiline evaluation function
    return eval_multiline_string(code, st);
}

// History-Persistenz Default-Helfer aus line_editor.c
extern CljObject* history_load_from_file(const char *path);
extern bool history_save_to_file(CljObject *vec, const char *path);

// (Entfernt) REPL-History-Sonderkommandos



/** @brief Print command-line usage information.
 *  @param prog Program name for usage display
 */
static void usage(const char *prog) {
    printf("Usage: %s [-n NS] [-e EXPR] [-f FILE] [--no-core] [--repl]\n", prog);
}

/** @brief Clean up resources and exit with specified code.
 *  @param eval_args Array to free before exit
 *  @param exit_code Exit code to use
 */
static void cleanup_and_exit(const char **eval_args, int exit_code) {
    // Print memory profiling stats
#ifdef ENABLE_MEMORY_PROFILING
    printf("\n🔍 === REPL Memory Profiling Stats ===\n");
    MEMORY_PROFILER_PRINT_STATS("REPL Session");
    
#ifdef DEBUG
    // Also call the function directly to ensure it works
    memory_profiler_print_stats("REPL Session Direct");
    
    // Force print some debug information
    printf("🔍 Debug: Total allocs=%zu, deallocs=%zu, retains=%zu, releases=%zu, autoreleases=%zu\n", 
           g_memory_stats.total_allocations, g_memory_stats.total_deallocations, 
           g_memory_stats.retain_calls, g_memory_stats.release_calls, g_memory_stats.autorelease_calls);
#endif
    
    printf("📊 Final memory state before exit\n");
#else
    printf("\n🔍 Memory profiling disabled - no stats available\n");
#endif
    
    if (eval_args) free(eval_args);
    exit(exit_code);
}

/** @brief Run the interactive REPL loop with input handling and evaluation.
 *  @param st Evaluation state for the REPL session
 *  @return true on successful completion
 */
static bool run_interactive_repl(EvalState *st) {
    // Initialize memory profiling DIRECTLY before the first prompt
#ifdef ENABLE_MEMORY_PROFILING
    MEMORY_PROFILER_INIT();
    enable_memory_profiling(true);
    
    // Enable verbose memory mode for REPL
    g_memory_verbose_mode = true;
    
    // Memory profiling is now initialized and ready
    printf("🔍 Memory profiling initialized for REPL (Debug Build)\n");
#else
    printf("🔍 Memory profiling disabled for REPL (Release Build)\n");
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
    WITH_AUTORELEASE_POOL({
        TRY {
            CljObject *loaded = line_editor_history_load_default();
            if (loaded && is_type(loaded, CLJ_VECTOR)) {
                line_editor_set_history_from_vector(editor, loaded);
            } else {
                line_editor_clear_history(editor);
            }
        } CATCH(ex) {
            // Ignoriere Ladefehler: starte mit leerer History
            line_editor_clear_history(editor);
        } END_TRY
    });
#endif

    while (true) {
        // Print prompt only once per input cycle to avoid flooding
        if (!prompt_shown) {
#ifdef ENABLE_MEMORY_PROFILING
            // Enable debug output AFTER the first prompt is about to be shown
            if (!prompt_shown) {
                enable_memory_debug_output();
            }
#endif
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
            if (!got_input) continue;
        }
#else
        int once = 200;
        bool should_exit = false;
        while (once--) {
            char buf[512];
            int n = platform_readline_nb(buf, sizeof(buf));
            if (n < 0) { should_exit = true; break; }
            if (n == 0) { usleep(1000); continue; }
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

        // Use eval_string_repl for proper exception handling
        bool success = eval_string_repl(acc, st);
        
        // Show memory stats after each evaluation (if enabled)
#ifdef ENABLE_MEMORY_PROFILING
        if (g_memory_verbose_mode) {
#ifdef DEBUG
            printf("🔍 Memory: %zu allocs, %zu deallocs, %zu bytes\n", 
                   g_memory_stats.total_allocations,
                   g_memory_stats.total_deallocations,
                   g_memory_stats.current_memory_usage);
            
            // Show detailed type breakdown - one line per type
            if (g_memory_stats.allocations_by_type[CLJ_SYMBOL] > 0 || g_memory_stats.deallocations_by_type[CLJ_SYMBOL] > 0) {
                printf("📋 Symbol: A:%zu/%zu R:%zu AR:%zu\n", 
                       g_memory_stats.allocations_by_type[CLJ_SYMBOL], g_memory_stats.deallocations_by_type[CLJ_SYMBOL],
                       g_memory_stats.retains_by_type[CLJ_SYMBOL], g_memory_stats.autoreleases_by_type[CLJ_SYMBOL]);
            }
            if (g_memory_stats.allocations_by_type[CLJ_STRING] > 0 || g_memory_stats.deallocations_by_type[CLJ_STRING] > 0) {
                printf("📋 String: A:%zu/%zu R:%zu Rel:%zu AR:%zu\n", 
                       g_memory_stats.allocations_by_type[CLJ_STRING], g_memory_stats.deallocations_by_type[CLJ_STRING],
                       g_memory_stats.retains_by_type[CLJ_STRING], g_memory_stats.releases_by_type[CLJ_STRING], g_memory_stats.autoreleases_by_type[CLJ_STRING]);
            }
            if (g_memory_stats.allocations_by_type[CLJ_VECTOR] > 0 || g_memory_stats.deallocations_by_type[CLJ_VECTOR] > 0) {
                printf("📋 Vector: A:%zu/%zu Rel:%zu\n", 
                       g_memory_stats.allocations_by_type[CLJ_VECTOR], g_memory_stats.deallocations_by_type[CLJ_VECTOR],
                       g_memory_stats.releases_by_type[CLJ_VECTOR]);
            }
            if (g_memory_stats.allocations_by_type[CLJ_LIST] > 0 || g_memory_stats.deallocations_by_type[CLJ_LIST] > 0) {
                printf("📋 List: A:%zu/%zu R:%zu AR:%zu\n", 
                       g_memory_stats.allocations_by_type[CLJ_LIST], g_memory_stats.deallocations_by_type[CLJ_LIST],
                       g_memory_stats.retains_by_type[CLJ_LIST], g_memory_stats.autoreleases_by_type[CLJ_LIST]);
            }
#else
            // In release builds, only show if there are actual allocations
            if (g_memory_stats.total_allocations > 0) {
                printf("🔍 Memory: %zu allocs, %zu deallocs, %zu bytes\n", 
                       g_memory_stats.total_allocations,
                       g_memory_stats.total_deallocations,
                       g_memory_stats.current_memory_usage);
            }
#endif
        }
#endif
        
        // Always add command to history (successful or failed) so user can correct it
        if (acc[0] != '\0') {
#ifdef ENABLE_LINE_EDITING
            LineEditor *editor = get_line_editor();
            if (editor) {
                line_editor_add_to_history(editor, acc);
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
            CljObject *vec = line_editor_get_history_vector(ed);
            if (vec) { line_editor_history_save_default(vec); RELEASE(vec); }
        }
    });
#endif

    // Print memory profiling stats before exiting REPL
#ifdef ENABLE_MEMORY_PROFILING
    printf("\n🔍 === REPL Memory Profiling Stats (EOF) ===\n");
    MEMORY_PROFILER_PRINT_STATS("REPL Session");
    
#ifdef DEBUG
    // Also call the function directly to ensure it works
    memory_profiler_print_stats("REPL Session Direct");
    
    // Force print some debug information
    printf("🔍 Debug: Total allocs=%zu, deallocs=%zu, retains=%zu, releases=%zu, autoreleases=%zu\n", 
           g_memory_stats.total_allocations, g_memory_stats.total_deallocations, 
           g_memory_stats.retain_calls, g_memory_stats.release_calls, g_memory_stats.autorelease_calls);
#endif
    
    printf("📊 Final memory state before exit\n");
#else
    printf("\n🔍 Memory profiling disabled - no stats available\n");
#endif

    return true;
}

int main(int argc, char **argv) {
    platform_init();
    runtime_init();
    init_special_symbols();  // Initialize special symbols like SYM_DEF
    EvalState *st = evalstate_new();
    if (!st) return 1;
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
            
            bool success = eval_string_repl(acc, st);
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
        bool success = eval_string_repl(eval_args[i], st);
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
    
    return 0;
}


