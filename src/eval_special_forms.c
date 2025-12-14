#include "eval_special_forms.h"
#include "eval.h"
#include "channel.h"
#include "event_loop.h"
#include "vector.h"
#include "exception.h"
#include "environment.h"
#include "runtime.h"

static ID eval_cond(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
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

static ID eval_if_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    int list_len = list_count(list);
    if (list_len < 3) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "if-let requires bindings and a then body", __FILE__, __LINE__, 0);
        return NULL;
    }

    ID bindings_obj = list_get_element(list, 1);
    if (!bindings_obj || TAG(bindings_obj) != CLJ_VECTOR) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "if-let requires a vector for bindings", __FILE__, __LINE__, 0);
        return NULL;
    }

    CljVector *bindings = as_vector(bindings_obj);
    if (!bindings || vector_count(bindings) < 2) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "if-let bindings require a symbol and expression", __FILE__, __LINE__, 0);
        return NULL;
    }

    ID binding_sym = vector_nth(bindings, 0);
    ID binding_expr = vector_nth(bindings, 1);
    if (!binding_sym || TAG(binding_sym) != CLJ_SYMBOL) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "if-let binding must be a symbol", __FILE__, __LINE__, 0);
        return NULL;
    }

    ID binding_value = eval_body(binding_expr, env, st, ctx);
    bool truthy = clj_is_truthy(binding_value);
    ID result = NULL;

    if (truthy) {
        size_t frame_bytes = frame_allocation_size(1);
        CallFrame *frame = (CallFrame*)STACK_ALLOC(char, frame_bytes);
        frame_init(frame, ctx ? ctx->frame : NULL);

        ID sym_array[1] = {binding_sym};
        ID val_array[1] = {binding_value};
        frame_set_bindings(frame, ctx ? ctx->frame : NULL, sym_array, val_array, 1);
        if (binding_value && !IS_IMMEDIATE(binding_value)) {
            RELEASE(binding_value);
        }
        binding_value = NULL; // Ownership moved to frame

        CljList *parent_stack = NULL;
        bool parent_owned = false;
        if (ctx && ctx->env_stack) {
            parent_stack = ctx->env_stack;
        } else if (env) {
            parent_stack = make_list((CljObject*)env, NULL);
            parent_owned = true;
        }

        EvalContext then_ctx = ctx ? *ctx : (EvalContext){0};
        then_ctx.frame = frame;
        then_ctx.env_stack = parent_stack;
        if (!then_ctx.env) {
            then_ctx.env = env;
        }

        ID then_expr = list_get_element(list, 2);
        result = then_expr ? eval_body(then_expr, env, st, &then_ctx) : NULL;

        frame_release(frame);
        if (parent_owned && parent_stack) {
            RELEASE(parent_stack);
        }
    } else {
        if (binding_value) {
            RELEASE(binding_value);
            binding_value = NULL;
        }
        ID else_expr = list_len > 3 ? list_get_element(list, 3) : NULL;
        result = else_expr ? eval_body(else_expr, env, st, ctx) : NULL;
    }

    if (binding_value) {
        RELEASE(binding_value);
    }

    return result;
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

    // Note: if-let is handled as a macro expansion in the parser, so it never reaches here as SYM_IF_LET
    // eval_if_let is only called directly from macro-expanded code

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
        return eval_cond(list, env, st, ctx);
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




