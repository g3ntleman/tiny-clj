#include "common.h"
#include "platform.h"
#include "tiny_clj.h"
#include "clj_parser.h"
#include "namespace.h"
#include "CljObject.h"
#include "function_call.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int is_balanced_form(const char *s) {
    int p = 0, b = 0, c = 0; // () [] {}
    int in_str = 0; int esc = 0;
    for (const char *x = s; *x; ++x) {
        char ch = *x;
        if (in_str) {
            if (esc) { esc = 0; continue; }
            if (ch == '\\') { esc = 1; continue; }
            if (ch == '"') { in_str = 0; continue; }
            continue;
        }
        if (ch == '"') { in_str = 1; continue; }
        if (ch == '(') p++; else if (ch == ')') p--;
        else if (ch == '[') b++; else if (ch == ']') b--;
        else if (ch == '{') c++; else if (ch == '}') c--;
        if (p < 0 || b < 0 || c < 0) return 0;
    }
    return (p == 0 && b == 0 && c == 0 && !in_str);
}

static void print_result(CljObject *v) {
    char *s = pr_str(v);
    if (s) { printf("%s\n", s); free(s); }
}

static int eval_string(const char *code, EvalState *st) {
    const char *p = code;
    CljObject *ast = parse(p, st);
    if (!ast) return 0;
    CljObject *res = NULL;
    if (ast->type == CLJ_LIST) {
        CljObject *env = (st && st->current_ns) ? st->current_ns->mappings : NULL;
        res = eval_list(ast, env);
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
    int no_core = 0;
    if (argc > 1) clojure_core_set_quiet(1);

    const char *ns_arg = NULL;
    const char *eval_arg = NULL;
    const char *file_arg = NULL;
    int start_repl = 0;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--ns") == 0) && i + 1 < argc) {
            ns_arg = argv[++i];
        } else if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eval") == 0) && i + 1 < argc) {
            eval_arg = argv[++i];
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            file_arg = argv[++i];
        } else if (strcmp(argv[i], "--no-core") == 0) {
            no_core = 1;
        } else if (strcmp(argv[i], "--repl") == 0) {
            start_repl = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!no_core) {
        load_clojure_core();
    }

    if (ns_arg) {
        evalstate_set_ns(st, ns_arg);
    }

    if (file_arg) {
        CLJVALUE_POOL_SCOPE(pool) {
            if (setjmp(st->jmp_env) == 0) {
                FILE *fp = fopen(file_arg, "r");
                if (!fp) {
                    throw_exception("IOError", "Failed to open file", file_arg, 0, 0);
                } else {
                    char line[1024];
                    char acc[8192]; acc[0] = '\0';
                    while (fgets(line, sizeof(line), fp)) {
                        strncat(acc, line, sizeof(acc) - strlen(acc) - 1);
                        if (!is_balanced_form(acc)) continue;
                        (void)eval_string(acc, st);
                        acc[0] = '\0';
                    }
                    fclose(fp);
                }
            } else {
                if (st->last_error) {
                    print_result(st->last_error);
                    release_exception((CLJException*)st->last_error);
                    st->last_error = NULL;
                }
                return 1;
            }
        }
        if (!start_repl && !eval_arg) return 0;
    }

    if (eval_arg) {
        CLJVALUE_POOL_SCOPE(pool) {
            if (setjmp(st->jmp_env) == 0) {
                (void)eval_string(eval_arg, st);
            } else {
                if (st->last_error) {
                    print_result(st->last_error);
                    release_exception((CLJException*)st->last_error);
                    st->last_error = NULL;
                }
                return 1;
            }
        }
        if (!start_repl) return 0;
    }

    // Interactive REPL
    printf("tiny-clj %s REPL (platform=%s). Ctrl-D to exit.\n", "0.1", platform_name());
    platform_set_stdin_nonblocking(1);

    char acc[4096]; acc[0] = '\0';
    for (;;) {
        // Prompt indicates balance state
        printf(is_balanced_form(acc) ? "> " : "…> ");
        fflush(stdout);

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
            continue;
        }

        // Evaluate accumulated form
        CLJVALUE_POOL_SCOPE(pool) {
            if (setjmp(st->jmp_env) == 0) {
                const char *p = acc;
                CljObject *ast = parse(p, st);
                if (ast) {
                    CljObject *res = eval_expr_simple(ast, st);
                    if (res) print_result(res);
                }
            } else {
                if (st->last_error) {
                    print_result(st->last_error);
                    release_exception((CLJException*)st->last_error);
                    st->last_error = NULL;
                }
            }
        }
        acc[0] = '\0';
    }

    return 0;
}


