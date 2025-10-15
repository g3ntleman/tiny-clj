#include "platform.h"
#include "tiny_clj.h"
#include "parser.h"
#include "namespace.h"
#include "object.h"
#include "function_call.h"
#include "exception.h"
#include "builtins.h"
#include "memory_profiler.h"
#include "line_editor.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

/** @brief Check if a string has balanced parentheses, brackets, and braces.
 *  @param s String to check for balanced delimiters
 *  @return true if balanced, false otherwise
 */
static bool is_balanced_form(const char *s) {
    int p = 0, b = 0, c = 0; // () [] {}
    bool in_str = false; bool esc = false;
    for (const char *x = s; *x; ++x) {
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
        if (p < 0 || b < 0 || c < 0) return false;
    }
    return (p == 0 && b == 0 && c == 0 && !in_str);
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
    if (!v) return;
    
    // For symbols, print their name directly (not as code)
    if (is_type(v, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol(v);
        if (sym && sym->name[0] != '\0') {
            printf("%s\n", sym->name);
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
    if (!ex) return;
    
    // Use safer string handling to prevent corruption
    const char *type = ex->type ? ex->type : "Error";
    const char *message = ex->message ? ex->message : "Unknown error";
    const char *file = ex->file ? ex->file : "?";
    
    // Ensure strings are null-terminated and not corrupted
    if (strlen(type) > 100) type = "Error";
    if (strlen(message) > 200) message = "Unknown error";
    if (strlen(file) > 100) file = "?";
    
    fprintf(stderr, "EXCEPTION: %s: %s at %s:%d:%d\n",
        type, message, file, ex->line, ex->col);
}

/** @brief Evaluate a string expression in the REPL context.
 *  @param code Expression string to evaluate
 *  @param st Evaluation state
 *  @return true if successful, false on parse or evaluation error
 */
static bool eval_string_repl(const char *code, EvalState *st) {
    const char *p = code;
    CljObject *ast = parse(p, st);
    if (!ast) return false;
    
    CljObject *res = NULL;
    if (is_type(ast, CLJ_LIST)) {
        CljObject *env = (st && st->current_ns) ? st->current_ns->mappings : NULL;
        res = eval_list(ast, env, st);
    } else {
        res = eval_expr_simple(ast, st);
    }
    if (!res) return false;
    print_result(res);
    return true;
}

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
    // Print memory profiling stats in debug mode
#ifdef DEBUG
    printf("\n🔍 === REPL Memory Profiling Stats ===\n");
    MEMORY_PROFILER_PRINT_STATS("REPL Session");
    printf("📊 Final memory state before exit\n");
#endif
    
    if (eval_args) free(eval_args);
    exit(exit_code);
}

/** @brief Run the interactive REPL loop with input handling and evaluation.
 *  @param st Evaluation state for the REPL session
 *  @return true on successful completion
 */
static bool run_interactive_repl(EvalState *st) {
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
#endif

    for (;;) {
        // Print prompt only once per input cycle to avoid flooding
        if (!prompt_shown) {
            print_prompt(st, is_balanced_form(acc));
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

        if (!is_balanced_form(acc)) {
            // need more lines
            prompt_shown = false; // show continuation prompt once
            continue;
        }

        // Evaluate accumulated form with TRY/CATCH
        TRY {
            AUTORELEASE_POOL_SCOPE(pool) {
                const char *p = acc;
                CljObject *ast = parse(p, st);
                if (ast) {
                    CljObject *res = NULL;
                    if (is_type(ast, CLJ_LIST)) {
                        CljObject *env = (st && st->current_ns) ? st->current_ns->mappings : NULL;
                        res = eval_list(ast, env, st);
                    } else {
                        res = eval_expr_simple(ast, st);
                    }
                    if (res) print_result(res);
                }
            }
        } CATCH(ex) {
            print_exception(ex);
        } END_TRY
        
        acc[0] = '\0';
        prompt_shown = false; // show fresh prompt after evaluation
    }

    // Print memory profiling stats before exiting REPL
#ifdef DEBUG
    printf("\n🔍 === REPL Memory Profiling Stats (EOF) ===\n");
    MEMORY_PROFILER_PRINT_STATS("REPL Session");
    printf("📊 Final memory state before exit\n");
#endif

    return true;
}

int main(int argc, char **argv) {
    platform_init();
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

    if (!no_core) {
        load_clojure_core(st);
        // Enable memory profiling after clojure.core is loaded
        MEMORY_PROFILER_INIT();
        // Initialize memory profiling hooks for detailed tracking
        memory_profiling_init_with_hooks();
    }
    
    // Register builtin functions
    register_builtins();

    if (ns_arg) {
        evalstate_set_ns(st, ns_arg);
    }

    if (file_arg) {
        TRY {
            AUTORELEASE_POOL_SCOPE(pool) {
                FILE *fp = fopen(file_arg, "r");
                if (!fp) {
                    throw_exception_formatted("IOError", __FILE__, __LINE__, 0,
                            "Failed to open file '%s': %s", file_arg, strerror(errno));
                } else {
                    char line[1024];
                    char acc[8192]; acc[0] = '\0';
                    while (fgets(line, sizeof(line), fp)) {
                        strncat(acc, line, sizeof(acc) - strlen(acc) - 1);
                        if (!is_balanced_form(acc)) continue;
                        TRY {
                            bool success = eval_string_repl(acc, st);
                            if (!success) {
                                // Parse error or evaluation failed
                                fclose(fp);
                                cleanup_and_exit(eval_args, 1);
                            }
                        } CATCH(ex) {
                            print_exception(ex);
                            fclose(fp);
                            cleanup_and_exit(eval_args, 1);
                        } END_TRY
                        acc[0] = '\0';
                    }
                    fclose(fp);
                }
            }
        } CATCH(ex) {
            print_result((CljObject*)ex);
            cleanup_and_exit(eval_args, 1);
        } END_TRY
        if (!start_repl && eval_count == 0) {
            cleanup_and_exit(eval_args, 0);
        }
    }

    cleanup_line_editor();

    // Execute all -e arguments in order
    for (int i = 0; i < eval_count; i++) {
        TRY {
            AUTORELEASE_POOL_SCOPE(pool) {
                bool success = eval_string_repl(eval_args[i], st);
                if (!success) {
                    // Parse error or evaluation failed
                    cleanup_and_exit(eval_args, 1);
                }
            }
        } CATCH(ex) {
            print_exception(ex);
            cleanup_and_exit(eval_args, 1);
        } END_TRY
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


