#include "eval_sequence.h"
#include "eval.h"
#include "builtins.h"
#include <subjective-c/exception.h>
#include "event_loop.h"
#include "error_messages.h"
#include <subjective-c/map.h>
#include "seq.h"
#include <subjective-c/strings.h>

ID eval_and_call_native_with_context(CljList *list,
                                     CljMap *env,
                                     ID (*native_func)(ID*, unsigned int),
                                     unsigned int max_args,
                                     const EvalContext *ctx) {
    int total_count = list_count(list);
    unsigned int argc = total_count > 0 ? (unsigned int)(total_count - 1) : 0;
    if (argc > max_args) {
        return throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
            "Wrong number of args (%u) passed to: %s", argc, native_func ? "sequence-op" : "unknown");
    }

    ID args_stack[16];
    ID *args = alloc_obj_array((int)argc, args_stack);
    if (!args) return NULL;

    unsigned int i = 0;
    LIST_FOR_EACH(LIST_REST(list), elem) {
        if (i >= argc) break;
        // Note: args[i] can be NULL (nil), which is a valid argument.
        // Errors throw exceptions in eval_arg_from_expr_with_context.
        args[i] = eval_arg_from_expr_with_context(elem, env, NULL, ctx);
        i++;
    }

    ID result = native_func ? native_func(args, argc) : NULL;
    free_obj_array(args, args_stack);
    return result;
}

ID eval_map_lookup(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx, ID map) {
    int total_count = list_count(list);
    int argc = total_count - 1;

    if (argc != 1) {
        return throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
            "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
    }

    ID key = eval_arg_with_context(list, 1, env, st, ctx);
    if (!key) return NULL;

    ID result = map_get_sentinel((CljValue)map, (CljValue)key, NULL);
    RELEASE(key);
    return result;
}
