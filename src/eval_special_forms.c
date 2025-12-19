#include "eval_special_forms.h"
#include "eval.h"
#include "channel.h"
#include "event_loop.h"
#include "vector.h"
#include "exception.h"
#include "environment.h"
#include "runtime.h"

// Special Form evaluation functions with unified signature (exported for symbol initialization)
ID eval_special_cond(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    int argc = list_count(list);
    if (argc <= 1) return NULL;

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) break;

        ID test = list_get_element(list, i);
        ID expr = list_get_element(list, i + 1);

        if (!test || !expr) continue;

        ID test_result = eval_body(test, env, st, ctx);
        if (clj_is_truthy(test_result)) {
            return eval_body(expr, env, st, ctx);
        }
    }
    return NULL;
}

ID eval_special_if(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    ID cond_val = eval_arg_with_context(list, 1, env, st, ctx);
    bool truthy = clj_is_truthy(cond_val);
    RELEASE(cond_val);
    ID branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
    if (!branch) return NULL;
    return eval_body(branch, env, st, ctx);
}

ID eval_special_when(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    ID cond_val = eval_arg_with_context(list, 1, env, st, ctx);
    bool truthy = cond_val ? clj_is_truthy(cond_val) : false;
    RELEASE(cond_val);
    if (!truthy) return NULL;

    int list_len = list_count(list);
    ID result = NULL;
    for (int i = 2; i < list_len; i++) {
        ID body_expr = list_get_element(list, i);
        if (body_expr) {
            ASSIGN(result, eval_body(body_expr, env, st, ctx));
            if (!result && i < list_len - 1) return NULL;
        }
    }
    return result;
}

ID eval_special_while(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    int list_len = list_count(list);
    while (true) {
        ID cond_val = eval_arg_with_context(list, 1, env, st, ctx);
        if (!cond_val || !clj_is_truthy(cond_val)) {
            RELEASE(cond_val);
            return NULL;
        }
        RELEASE(cond_val);

        ID result = NULL;
        for (int i = 2; i < list_len; i++) {
            ID body_expr = list_get_element(list, i);
            if (body_expr) {
                ASSIGN(result, eval_body(body_expr, env, st, ctx));
                if (!result && i < list_len - 1) return NULL;
            }
        }
        RELEASE(result);
    }
}

ID eval_special_do(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    int list_len = list_count(list);
    ID result = NULL;
    for (int i = 1; i < list_len; i++) {
        ID expr = list_get_element(list, i);
        if (expr) {
            ASSIGN(result, eval_body(expr, env, st, ctx));
        }
    }
    return result;
}

ID eval_special_and(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    int argc = list_count(list);
    if (argc <= 1) return clj_true;
    ID result = clj_true;
    for (int i = 1; i < argc; i++) {
        ID arg = list_get_element(list, i);
        if (!arg) continue;
        result = eval_body(arg, env, st, ctx);
        if (!result || !clj_is_truthy(result)) {
            return result;
        }
    }
    return result;
}

ID eval_special_or(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    int argc = list_count(list);
    if (argc <= 1) return NULL;
    ID result = NULL;
    for (int i = 1; i < argc; i++) {
        ID arg = list_get_element(list, i);
        if (!arg) continue;
        result = eval_body(arg, env, st, ctx);
        if (clj_is_truthy(result)) {
            return result;
        }
    }
    return result;
}

ID eval_special_quote(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)env; (void)st; (void)ctx;  // Unused
    ID quoted_expr = list_get_element(list, 1);
    if (!quoted_expr) return NULL;
    return RETAIN(quoted_expr);
}

ID eval_special_go(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    int argc = list_count(list);
    CljList *do_list = NULL;
    if (argc > 1) {
        do_list = make_list((CljObject*)SYM_DO, NULL);
        CljList *tail = do_list;
        for (int i = 1; i < argc; i++) {
            ID expr_i = list_get_element(list, i);
            CljList *new_node = make_list(expr_i, NULL);
            if (tail) {
                tail->rest = (CljObject*)new_node;
                tail = new_node;
            }
        }
    }
    CljVector* empty_params_vec = make_vector(0, CLJ_VECTOR);
    CljList *fn_list = make_list((CljObject*)SYM_FN, NULL);
    if (!fn_list) return NULL;
    fn_list->rest = (CljObject*)make_list(empty_params_vec, NULL);
    CljList *fn_rest = as_list(fn_list->rest);
    if (fn_rest) {
        ID body_expr = do_list;
        fn_rest->rest = (CljObject*)make_list(body_expr, NULL);
    }
    ID fn_obj = eval_fn(fn_list, env, st);
    if (!fn_obj) {
        RELEASE(fn_list);
        return NULL;
    }
    CljMap *chan = make_result_channel();
    event_loop_enqueue(fn_obj, chan);
    RELEASE(fn_list);
    RELEASE(do_list);
    return (CljObject*)chan;
}

// Wrapper functions for existing special form evaluators
ID eval_special_fn(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CljMap *fn_env = env;
    if (!fn_env && st && st->current_ns) {
        fn_env = st->current_ns->mappings;
    }
    return AUTORELEASE(eval_fn_with_context(list, fn_env, st, ctx));
}

ID eval_special_defn(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    return eval_defn(list, env, st);
}

ID eval_special_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    return eval_let(list, env, st, ctx);
}

ID eval_special_var(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    return eval_var(list, env, st);
}

ID eval_special_recur(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)env; (void)st;  // Unused
    return eval_handle_recur(list, ctx);
}

ID eval_special_time(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    CljMap *time_env = env;
    if (!time_env && st && st->current_ns) {
        time_env = st->current_ns->mappings;
    }
    return eval_time(list, time_env, st);
}

ID eval_special_dotimes(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    return eval_dotimes(list, env, st);
}

ID eval_handle_recur(CljList *list, const EvalContext *ctx) {
    if (!ctx || !ctx->recur_args || !ctx->recur_arg_count) {
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
        return NULL;
    }

    int expected = ctx->param_count > 0 ? ctx->param_count : 0;
    int provided = list ? list_count(list) - 1 : 0;
    if (provided < 0) provided = 0;
    if (expected == 0) expected = provided;

    if (provided != expected) {
        throw_exception(EXCEPTION_ARITY, "recur arity mismatch", NULL, 0, 0);
        return NULL;
    }

    ID *evaluated_args = STACK_ALLOC(ID, expected > 0 ? expected : 1);
    for (int i = 0; i < expected; i++) {
        evaluated_args[i] = NULL;
    }

    EvalContext arg_ctx = {
        .env = ctx->env,
        .env_stack = ctx->env_stack,
        .st = ctx->st,
        .params = ctx->params,
        .param_values = ctx->param_values,
        .param_count = ctx->param_count,
        .recur_args = NULL,
        .recur_arg_count = NULL
    };
    CljList *arg_node = list ? as_list(list->rest) : NULL;
    int arg_index = 0;
    while (arg_node && arg_index < expected) {
        ID arg = arg_node->first;
        if (arg) {
            ID eval_arg = eval_body_with_params(arg, &arg_ctx);
            if (eval_arg) {
                evaluated_args[arg_index] = RETAIN(eval_arg);
            }
        }
        arg_index++;
        arg_node = arg_node->rest ? as_list(arg_node->rest) : NULL;
    }

    if (arg_node) {
        for (int i = 0; i < expected; i++) {
            RELEASE(evaluated_args[i]);
        }
        throw_exception(EXCEPTION_ARITY, "recur arity mismatch", NULL, 0, 0);
        return NULL;
    }

    for (int i = 0; i < expected; i++) {
        RELEASE(ctx->recur_args[i]);
        ctx->recur_args[i] = evaluated_args[i];
    }
    *ctx->recur_arg_count = expected;

    return NULL;
}

// Legacy dispatch function - kept for backward compatibility but deprecated
// New code should use direct function pointer access via CljSpecialSymbol
ID eval_special_form_dispatch(CljList *list,
                              CljMap *env,
                              EvalState *st,
                              const EvalContext *ctx,
                              CljSymbol *op_sym) {
    CljSpecialSymbol *special = as_special_symbol((ID)op_sym);
    if (!special || !special->eval_fn) return NULL;
    return special->eval_fn(list, env, st, ctx);
}




