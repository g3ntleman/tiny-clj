#include "eval_special_forms.h"
#include "eval.h"
#include "channel.h"
#include "event_loop.h"
#include "vector.h"
#include "exception.h"

static CljObject* eval_cond(CljList *list, CljMap *env, EvalState *st) {
    int argc = list_count(list);
    if (argc <= 1) return NULL;

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) break;

        ID test = list_get_element(list, i);
        ID expr = list_get_element(list, i + 1);

        if (!test || !expr) continue;

        ID test_result = eval_body(test, env, st, NULL);
        if (clj_is_truthy(test_result)) {
            return eval_body(expr, env, st, NULL);
        }
    }
    return NULL;
}

ID eval_handle_recur(CljList *list, const EvalContext *ctx) {
    if (!ctx || !ctx->recur_args || !ctx->recur_arg_count) {
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
        return NULL;
    }

    ID recur_args[16] = {NULL};
    int arg_count = 0;

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
    for (int i = 0; arg_node && i < 16; i++) {
        ID arg = arg_node->first;
        if (arg) {
            ID eval_arg = eval_body_with_params(arg, &arg_ctx);
            if (eval_arg) {
                recur_args[i] = RETAIN(eval_arg);
            }
        }
        arg_count++;
        arg_node = arg_node->rest ? as_list(arg_node->rest) : NULL;
    }

    CLJ_ASSERT(!arg_node && "recur: maximum 16 arguments supported");

    for (int i = 0; i < arg_count && i < 16; i++) {
        RELEASE(ctx->recur_args[i]);
        ctx->recur_args[i] = recur_args[i];
    }
    *ctx->recur_arg_count = arg_count;

    return NULL;
}

ID eval_special_form_dispatch(CljList *list,
                              CljMap *env,
                              EvalState *st,
                              const EvalContext *ctx,
                              CljSymbol *op_sym) {
    if (op_sym == SYM_IF) {
        ID cond_val = eval_arg_with_context(list, 1, env, st, ctx);
        bool truthy = clj_is_truthy(cond_val);
        RELEASE(cond_val);
        ID branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return NULL;
        return eval_body(branch, env, st, ctx);
    }

    if (op_sym == SYM_WHEN) {
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

    if (op_sym == SYM_WHILE) {
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

    if (op_sym == SYM_COND) {
        return eval_cond(list, env, st);
    }

    if (op_sym == SYM_DO) {
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

    if (op_sym == SYM_AND) {
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

    if (op_sym == SYM_OR) {
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

    if (op_sym == SYM_FN) {
        CljMap *fn_env = env;
        if (!fn_env && st && st->current_ns) {
            fn_env = st->current_ns->mappings;
        }
        return AUTORELEASE(eval_fn_with_context(list, fn_env, st, ctx));
    }

    if (op_sym == SYM_DEFN) {
        return eval_defn(list, env, st);
    }

    if (op_sym == SYM_LET) {
        return eval_let(list, env, st, ctx);
    }

    if (op_sym == SYM_VAR) {
        return eval_var(list, env, st);
    }

    if (op_sym == SYM_QUOTE) {
        ID quoted_expr = list_get_element(list, 1);
        if (!quoted_expr) return NULL;
        return RETAIN(quoted_expr);
    }

    if (op_sym == SYM_RECUR) {
        return eval_handle_recur(list, ctx);
    }

    if (op_sym == SYM_GO) {
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

    if (op_sym == SYM_TIME) {
        CljMap *time_env = env;
        if (!time_env && st && st->current_ns) {
            time_env = st->current_ns->mappings;
        }
        return eval_time(list, time_env, st);
    }

    if (op_sym == SYM_DOTIMES) {
        return eval_dotimes(list, env, st);
    }

    return NULL;
}



