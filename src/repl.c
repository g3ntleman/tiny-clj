#include "common.h"
#include "platform.h"
#include "tiny_clj.h"
#include "clj_parser.h"
#include "namespace.h"
#include "object.h"
#include "function_call.h"
#include "exception.h"
#include "builtins.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

static int is_balanced_form(const char *s) {
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
        if (p < 0 || b < 0 || c < 0) return 0;
    }
    return (p == 0 && b == 0 && c == 0 && !in_str);
}

static void print_prompt(EvalState *st, bool balanced) {
    const char *ns_name = "user";  // Default
    if (st && st->current_ns && st->current_ns->name) {
        CljSymbol *sym = as_symbol(st->current_ns->name);
        if (sym && sym->name) {
            ns_name = sym->name;
        }
    }
    printf("%s%s ", ns_name, balanced ? "=>" : "...");
    fflush(stdout);
}

static void print_result(CljObject *v) {
    if (!v) return;
    
    // For symbols, print their name directly (not as code)
    if (is_type(v, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol(v);
        if (sym && sym->name) {
            printf("%s\n", sym->name);
            return;
        }
    }
    
    char *s = pr_str(v);
    if (s) { printf("%s\n", s); free(s); }
}

static void print_exception(CLJException *ex) {
    if (!ex) return;
    fprintf(stderr, "EXCEPTION: %s: %s at %s:%d:%d\n",
        ex->type ? ex->type : "Error",
        ex->message ? ex->message : "Unknown error",
        ex->file ? ex->file : "?",
        ex->line, ex->col);
}

static int eval_string_repl(const char *code, EvalState *st) {
    const char *p = code;
    CljObject *ast = parse(p, st);
    if (!ast) return 0;
    
    CljObject *res = NULL;
    if (is_type(ast, CLJ_LIST)) {
        CljObject *env = (st && st->current_ns) ? st->current_ns->mappings : NULL;
        res = eval_list(ast, env, st);
    } else {
        res = eval_expr_simple(ast, st);
    }
    if (!res) return 0;
    print_result(res);
    return 1;
}

static void usage(const char *prog) {
    printf("Usage: %s [-n NS] [-e EXPR] [-f FILE] [--no-core] [--repl]\n", prog);
}

int main(int argc, char **argv) {
    platform_init();
    EvalState *st = evalstate_new();
    if (!st) return 1;
    set_global_eval_state(st);
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
            if (eval_args) free(eval_args);
            return 0;
        } else {
            usage(argv[0]);
            if (eval_args) free(eval_args);
            return 1;
        }
    }

    if (!no_core) {
        load_clojure_core(st);
    }
    
    // Register builtin functions
    register_builtins();

    if (ns_arg) {
        evalstate_set_ns(st, ns_arg);
    }

    if (file_arg) {
        TRY {
            CLJVALUE_POOL_SCOPE(pool) {
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
                            int result = eval_string_repl(acc, st);
                            if (result == 0) {
                                // Parse error or evaluation failed
                                fclose(fp);
                                if (eval_args) free(eval_args);
                                return 1;
                            }
                        } CATCH(ex) {
                            print_exception(ex);
                            fclose(fp);
                            if (eval_args) free(eval_args);
                            return 1;
                        } END_TRY
                        acc[0] = '\0';
                    }
                    fclose(fp);
                }
            }
        } CATCH(ex) {
            print_result((CljObject*)ex);
            if (eval_args) free(eval_args);
            return 1;
        } END_TRY
        if (!start_repl && eval_count == 0) {
            if (eval_args) free(eval_args);
            return 0;
        }
    }

    // Execute all -e arguments in order
    for (int i = 0; i < eval_count; i++) {
        TRY {
            CLJVALUE_POOL_SCOPE(pool) {
                int result = eval_string_repl(eval_args[i], st);
                if (result == 0) {
                    // Parse error or evaluation failed
                    if (eval_args) free(eval_args);
                    return 1;
                }
            }
        } CATCH(ex) {
            print_exception(ex);
            if (eval_args) free(eval_args);
            return 1;
        } END_TRY
    }
    
    if (eval_args) free(eval_args);
    if (eval_count > 0 && !start_repl) return 0;

    // Interactive REPL
    printf("tiny-clj %s REPL (platform=%s). Ctrl-D to exit. \n", "0.1", platform_name());
    platform_set_stdin_nonblocking(1);

    char acc[4096]; acc[0] = '\0';
    bool prompt_shown = false;
    for (;;) {
        // Print prompt only once per input cycle to avoid flooding
        if (!prompt_shown) {
            print_prompt(st, is_balanced_form(acc));
            prompt_shown = true;
        }

        int once = 200; // poll iterations per loop for cooperative multitasking
        int got = 0;
        while (once--) {
            char buf[512];
            int n = platform_readline_nb(buf, sizeof(buf));
            if (n < 0) { got = -1; break; }
            if (n == 0) { usleep(1000); continue; }
            // append to accumulator (strip CR)
            if (n > 0) {
                if (acc[0] != '\0') strncat(acc, "\n", sizeof(acc) - strlen(acc) - 1);
                // trim CR
                for (int i = 0; i < n; i++) if (buf[i] == '\r') buf[i] = '\n';
                strncat(acc, buf, sizeof(acc) - strlen(acc) - 1);
                got = 1; break;
            }
        }
        if (got < 0) break;
        if (!got) continue;

        if (!is_balanced_form(acc)) {
            // need more lines
            prompt_shown = false; // show continuation prompt once
            continue;
        }

        // Evaluate accumulated form with TRY/CATCH
        TRY {
            CLJVALUE_POOL_SCOPE(pool) {
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
            // Exception caught - print and continue REPL
            print_exception(ex);
        } END_TRY
        
        acc[0] = '\0';
        prompt_shown = false; // show fresh prompt after evaluation
    }

    return 0;
}


