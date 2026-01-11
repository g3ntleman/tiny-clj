#include "eval_compiled.h"

#include "eval.h"
#include "eval_special_forms.h"
#include "exception.h"
#include "function.h"
#include "list.h"
#include <stdlib.h>

static inline CljList make_pseudo_list(ID head, ID rest) {
    CljList l;
    l.base.type = CLJ_LIST;
    l.base.rc = 1;
    l.first = head;
    l.rest = rest;
    return l;
}

ID eval_compiled_if(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_IF, node->rest);
    return eval_special_if(&pseudo, env, st, ctx);
}

ID eval_compiled_do(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_DO, node->rest);
    return eval_special_do(&pseudo, env, st, ctx);
}

ID eval_compiled_let(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_LET, node->rest);
    return eval_special_let(&pseudo, env, st, ctx);
}

ID eval_compiled_fn(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljList pseudo = make_pseudo_list((ID)SYM_FN, node->rest);
    return eval_special_fn(&pseudo, env, st, ctx);
}

ID eval_compiled_call(CljASTNode *node, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    // Operator lives in node->first; arguments are in node->rest (plain list).
    ID fn = eval_arg_from_expr_with_context(node->first, env, st, ctx);
    if (!fn) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                        "Cannot call nil as a function");
    }
    if (!is_callable(fn)) {
        const char *type_name = is_fixed((CljValue)fn) ? "number"
                              : (is_bool((CljValue)fn) ? "boolean" : "value");
        RELEASE(fn);
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                        "Cannot call %s as a function", type_name);
    }

    // Evaluate arguments (arity is typically small in hot code like fib).
    ID stack_args[16];
    ID *args = stack_args;
    unsigned int argc = 0;

    CljList *arg_nodes = (node->rest && list_type_matches(TAG(node->rest))) ? as_list(node->rest) : NULL;
    for (CljList *cur = arg_nodes; cur; cur = cur->rest ? as_list(cur->rest) : NULL) {
        if (argc == 16) {
            // Grow to heap once if needed (rare).
            args = (ID*)malloc(sizeof(ID) * 32);
            if (!args) {
                RELEASE(fn);
                throw_oom();
                return NULL;
            }
            for (unsigned int i = 0; i < 16; i++) args[i] = stack_args[i];
        }
        ID v = eval_arg_from_expr_with_context(cur->first, env, st, ctx);
        args[argc++] = v; // v may be NULL (nil) - valid argument
    }

    ID result = eval_function_call(fn, args, argc, env, st);

    // Clean up evaluated arguments (eval_arg_from_expr_with_context returns retained).
    for (unsigned int i = 0; i < argc; i++) {
        RELEASE(args[i]);
    }
    if (args != stack_args) {
        free(args);
    }
    RELEASE(fn);
    return result;
}

