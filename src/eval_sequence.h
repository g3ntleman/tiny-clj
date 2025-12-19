#ifndef EVAL_SEQUENCE_H
#define EVAL_SEQUENCE_H

#include "eval.h"
#include "builtins.h"

#ifdef __cplusplus
extern "C" {
#endif

// Helper function for calling native functions with context (inline for performance)
static inline ID eval_and_call_native_with_context(CljList *list,
                                       CljMap *env,
                                                    ID (*native_func)(ID*, unsigned int),
                                                    int max_args,
                                                    const EvalContext *ctx) {
    int total_count = list_count(list);
    int argc = total_count - 1;
    if (argc > max_args) {
        return throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
            "Wrong number of args (%d) passed to: %s", argc, native_func ? "sequence-op" : "unknown");
    }

    ID args_stack[16];
    ID *args = alloc_obj_array(argc, args_stack);
    if (!args) return NULL;

    int i = 0;
    LIST_FOR_EACH(LIST_REST(list), elem) {
        if (i >= argc) break;
        args[i] = eval_arg_from_expr_with_context(elem, env, NULL, ctx);
        if (!args[i]) {
            free_obj_array(args, args_stack);
            return NULL;
        }
        i++;
    }

    ID result = native_func ? native_func(args, argc) : NULL;
    free_obj_array(args, args_stack);
    return result;
}

ID eval_map_lookup(CljList *list,
                   CljMap *env,
                   EvalState *st,
                   const EvalContext *ctx,
                   ID map);

#ifdef __cplusplus
}
#endif

#endif // EVAL_SEQUENCE_H
