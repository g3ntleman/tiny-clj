#include "eval_compiled.h"

#include "eval.h"
#include "eval_special_forms.h"
#include "exception.h"
#include "function.h"
#include <stdlib.h>

static CljPersistentVector *vector_from_rest(ID rest) {
    CljList *rest_list = (rest && is_list_type(TAG(rest))) ? as_list(rest) : NULL;
    unsigned int count = 0;
    LIST_FOR_EACH(rest_list, elem) { (void)elem; count++; }
    CljPersistentVector *args = make_vector((int)count, STRONG);
    LIST_FOR_EACH(rest_list, elem) {
        vector_conj_inplace(&args, elem);
    }
    return args;
}

ID eval_compiled_if(CljASTNode *node, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljPersistentVector *args = vector_from_rest(node->rest);
    ID result = eval_special_if(args, env, st, ctx);
    RELEASE(args);
    return result;
}

ID eval_compiled_do(CljASTNode *node, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljPersistentVector *args = vector_from_rest(node->rest);
    ID result = eval_special_do(args, env, st, ctx);
    RELEASE(args);
    return result;
}

ID eval_compiled_let(CljASTNode *node, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljPersistentVector *args = vector_from_rest(node->rest);
    ID result = eval_special_let(args, env, st, ctx);
    RELEASE(args);
    return result;
}

ID eval_compiled_fn(CljASTNode *node, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    CljPersistentVector *args = vector_from_rest(node->rest);
    ID result = eval_special_fn(args, env, st, ctx);
    RELEASE(args);
    return result;
}

ID eval_compiled_call(CljASTNode *node, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    if (!node) return NULL;
    // Operator lives in node->first; arguments are in node->rest (plain list).
    ID fn = eval_arg_from_expr_with_context(node->first, env, st, ctx);
    if (!fn) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                        "Cannot call nil as a function"); return NULL;
    }
    if (!is_callable(fn)) {
        const char *type_name = is_fixed((CljValue)fn) ? "number"
                              : (is_bool((CljValue)fn) ? "boolean" : "value");
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                        "Cannot call %s as a function", type_name); return NULL;
    }

    // Evaluate arguments (arity is typically small in hot code like fib).
    ID stack_args[16];
    ID *args = stack_args;
    unsigned int argc = 0;

    CljList *arg_nodes = (node->rest && is_list_type(TAG(node->rest))) ? as_list(node->rest) : NULL;
    for (CljList *cur = arg_nodes; cur; cur = cur->rest ? as_list(cur->rest) : NULL) {
        if (argc == 16) {
            // Grow to heap once if needed (rare).
            args = (ID*)CLJ_MALLOC(sizeof(ID) * 32);
            if (!args) {
                throw_oom();
                return NULL;
            }
            for (unsigned int i = 0; i < 16; i++) args[i] = stack_args[i];
        }
        ID v = eval_arg_from_expr_with_context(cur->first, env, st, ctx);
        args[argc++] = v; // v may be NULL (nil) - valid argument
    }

    ID result = eval_function_call(fn, args, argc, env, st);

    // eval_arg_from_expr_with_context returns pool-managed (AUTORELEASE'd) values;
    // do not RELEASE args or fn – the pool owns them; frame_set_bindings_init RETAINs args for the frame.
    if (args != stack_args) {
        CLJ_FREE(args);
    }
    return result;
}
