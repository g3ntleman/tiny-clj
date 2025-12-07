#include "eval_sequence.h"
#include "eval.h"
#include "builtins.h"
#include "exception.h"
#include "event_loop.h"
#include "error_messages.h"
#include "map.h"
#include "seq.h"
#include "strings.h"

static ID eval_and_call_native_with_context(CljList *list,
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

    for (int i = 0; i < argc; i++) {
        args[i] = eval_arg_with_context(list, i + 1, env, NULL, ctx);
        if (!args[i]) {
            for (int j = 0; j < i; j++) {
                RELEASE(args[j]);
            }
            free_obj_array(args, args_stack);
            return NULL;
        }
    }

    ID result = native_func ? native_func(args, argc) : NULL;

    for (int i = 0; i < argc; i++) {
        RELEASE(args[i]);
    }
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

    ID result = map_get((CljValue)map, (CljValue)key, NULL);
    RELEASE(key);
    return AUTORELEASE(RETAIN(result));
}

ID eval_sequence_dispatch_with_context(CljList *list,
                                       CljMap *env,
                                       ID op,
                                       const EvalContext *ctx) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_FIRST) return eval_and_call_native_with_context(list, env, native_first, 1, ctx);
    if (op_sym == SYM_REST) {
        return eval_and_call_native_with_context(list, env, native_rest, 1, ctx);
    }
    if (op_sym == SYM_CONS) return eval_and_call_native_with_context(list, env, native_cons, 2, ctx);
    if (op_sym == SYM_SEQ) return eval_seq(list, env);
    if (op_sym == SYM_NEXT) return eval_and_call_native_with_context(list, env, native_next, 1, ctx);
    if (op_sym == SYM_COUNT) return eval_and_call_native_with_context(list, env, native_count, 1, ctx);
    return NULL;
}

ID eval_loop_dispatch(CljList *list, CljMap *env, ID op, EvalState *st) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_FOR) return AUTORELEASE(eval_for(list, env));
    if (op_sym == SYM_DOSEQ) return AUTORELEASE(eval_doseq(list, env));
    if (op_sym == SYM_DOTIMES) {
        EvalState *eval_st = st;
        bool created_st = false;
        if (!eval_st) {
            eval_st = evalstate_new(false);
            created_st = true;
        }
        ID result = eval_dotimes(list, env, eval_st);
        if (created_st && eval_st) {
            evalstate_free(eval_st);
        }
        return AUTORELEASE(result);
    }
    return NULL;
}

