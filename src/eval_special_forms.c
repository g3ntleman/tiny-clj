#include "eval_special_forms.h"
#include "eval.h"
#include "channel.h"
#include "event_loop.h"
#include "vector.h"
#include "exception.h"
#include "environment.h"
#include "runtime.h"
#include "function.h"
#include "macro.h"
#include "meta.h"

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
    // Note: ctx can be NULL during clojure.core loading (e.g., in defmacro)
    // But if ctx is provided and we're creating a closure, it should have frame or env_stack
    // DEBUG: Assert if ctx is provided but has no frame and no env_stack (might be a problem for closures)
    if (ctx) {
        // When ctx is provided, check if it has frame or env_stack for closure support
        // This is important for closures like (constantly x) => (fn [] x)
        if (!ctx->frame && !ctx->env_stack) {
            // ctx is provided but has no frame and no env_stack - this might be a problem for closures
            // This can happen during bootstrap, but it's worth noting
            // Don't assert here because ctx can be NULL during bootstrap
        }
    }
    CljMap *fn_env = env;
    if (!fn_env && st && st->current_ns) {
        fn_env = st->current_ns->mappings;
    }
    return AUTORELEASE(eval_fn_with_context(list, fn_env, st, ctx));
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

ID eval_special_loop(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // (loop [bindings*] body*)
    if (!list || !st) return NULL;
    
    CljObject *bindings_vec = list_get_element(list, 1);
    if (!bindings_vec || TAG(bindings_vec) != CLJ_VECTOR) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires a vector for bindings", NULL, 0, 0);
        return NULL;
    }
    
    CljVector *bindings = as_vector((CljValue)bindings_vec);
    int binding_count = vector_count(bindings);
    if (binding_count % 2 != 0 || binding_count == 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires an even number of forms in binding vector", NULL, 0, 0);
        return NULL;
    }
    
    int pair_count = binding_count / 2;
    CLJ_ASSERT(pair_count <= CALLFRAME_MAX_PARAMS && "Too many loop bindings");
    
    int list_len = list_count(list);
    if (list_len < 3) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires a body expression", NULL, 0, 0);
        return NULL;
    }
    
    // Parse bindings and evaluate initial values
    // eval_body returns AUTORELEASE values - safe to use in this scope and pass to frame_set_bindings
    ID params_stack[16] = {0}, recur_args[16] = {0}, current_values[16] = {0};
    // Explicitly initialize recur_args to NULL (like eval_function_call)
    for (int i = 0; i < 16; i++) recur_args[i] = NULL;
    
    EvalContext init_ctx = ctx ? *ctx : (EvalContext){0};
    init_ctx.env = env;
    init_ctx.st = st;
    
    for (int i = 0; i < pair_count; i++) {
        CljValue sym_val = (CljValue)vector_nth(bindings, i * 2);
        CljValue init_val = (CljValue)vector_nth(bindings, i * 2 + 1);
        
        if (!sym_val || TAG(sym_val) != CLJ_SYMBOL) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop binding must be a symbol", NULL, 0, 0);
            return NULL;
        }
        
        params_stack[i] = sym_val;
        current_values[i] = (!init_val || is_fixnum(init_val) || is_special(init_val)) ? init_val : eval_body(init_val, env, st, &init_ctx);
    }
    
    // TCO Loop
    CallFrame loop_frame_storage;
    CallFrame *loop_frame = &loop_frame_storage;
    frame_init(loop_frame, ctx ? ctx->frame : NULL);
    frame_set_bindings(loop_frame, ctx ? ctx->frame : NULL, params_stack, current_values, pair_count);
    
    ID result = NULL;
    int recur_arg_count = -1;
    
    do {
        recur_arg_count = -1;
        // recur_args sind AUTORELEASE'd und wurden bereits von frame_set_bindings retained
        // Kein manuelles Cleanup nötig (werden von frame_set_bindings verwaltet)
        
        EvalContext loop_ctx = {
            .env = env,
            .env_stack = ctx ? ctx->env_stack : NULL,
            .frame = loop_frame,
            .st = st,
            .recur_args = recur_args,
            .recur_arg_count = &recur_arg_count,
            .recur_param_count = pair_count
        };
        
        ID new_result = NULL;
        for (int i = 2; i < list_len; i++) {
            ID body_expr = list_get_element(list, i);
            if (body_expr) {
                RELEASE(new_result);
                new_result = eval_body_with_params(body_expr, &loop_ctx);
            }
        }
        
        if (recur_arg_count >= 0) {
            RELEASE(new_result);
            CLJ_ASSERT(recur_arg_count == pair_count);
            // recur_args sind AUTORELEASE'd, werden von frame_set_bindings retained
            // frame_set_bindings wird alte frame-Werte freigeben und neue retained
            // Kopiere Werte und setze recur_args auf NULL vor nächster Iteration
            for (int i = 0; i < pair_count; i++) {
                current_values[i] = recur_args[i];
                recur_args[i] = NULL;  // Clear to prevent double-release in next eval_handle_recur
            }
            frame_set_bindings(loop_frame, ctx ? ctx->frame : NULL, params_stack, current_values, pair_count);
            continue;
        }
        
        ASSIGN(result, new_result);
        break;
    } while (true);
    
    // Cleanup
    // recur_args were already cleared (set to NULL) when used, so no cleanup needed
    frame_release(loop_frame);
    // Note: current_values are managed by frame_set_bindings/frame_release
    // No additional cleanup needed here
    
    return result;
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
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies or loop", NULL, 0, 0);
        return NULL;
    }

    // Get expected param count from recur context (set by function call)
    int expected = ctx->recur_param_count;
    int provided = list ? list_count(list) - 1 : 0;
    if (provided < 0) provided = 0;
    if (expected == 0) expected = provided;

    if (provided != expected) {
        throw_exception(EXCEPTION_ARITY, "recur arity mismatch", NULL, 0, 0);
        return NULL;
    }

    // OPTIMIZATION: Use fixed-size stack array to avoid STACK_ALLOC/alloca overhead
    // This eliminates __chkstk_darwin calls in hot path
    CLJ_ASSERT(expected <= CALLFRAME_MAX_PARAMS && "Too many recur arguments");
    ID evaluated_args[CALLFRAME_MAX_PARAMS];
    for (int i = 0; i < expected; i++) {
        evaluated_args[i] = NULL;
    }

    // Create context for evaluating recur arguments (without recur state to prevent nested recur)
    EvalContext arg_ctx = {
        .env = ctx->env,
        .env_stack = ctx->env_stack,
        .frame = ctx->frame,
        .st = ctx->st,
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
                evaluated_args[arg_index] = eval_arg; // AUTORELEASE'd, wird von frame_set_bindings retained
            }
        }
        arg_index++;
        arg_node = arg_node->rest ? as_list(arg_node->rest) : NULL;
    }

    if (arg_node) {
        // evaluated_args sind AUTORELEASE'd, kein manuelles RELEASE nötig
        throw_exception(EXCEPTION_ARITY, "recur arity mismatch", NULL, 0, 0);
        return NULL;
    }

    for (int i = 0; i < expected; i++) {
        // Alte Werte sind nicht mehr retained (wurden bereits von frame_set_bindings released)
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
    CljSpecialSymbol *special = as_special_symbol(op_sym);
    if (!special || !special->eval_fn) return NULL;
    return special->eval_fn(list, env, st, ctx);
}

// ============================================================================
// Quasiquote Special Form - delegates to Clojure quasiquote-fn after bootstrap
// ============================================================================

// Cached Clojure quasiquote-fn (resolved after bootstrap)
static CljFunction *g_quasiquote_fn = NULL;

ID eval_special_quasiquote(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)env;  // Unused - quasiquote doesn't evaluate in current env
    (void)ctx;
    
    // Get the expression to quasiquote: (quasiquote expr)
    ID expr = list_get_element(list, 1);
    if (!expr) return NULL;
    
    // Resolve quasiquote-fn from clojure.core (lazy initialization)
    if (!g_quasiquote_fn) {
        CljSymbol *sym = intern_symbol_global("quasiquote-fn");
        CljObject *resolved = sym ? ns_resolve(st, sym) : NULL;
        if (resolved && TAG(resolved) == CLJ_CLOSURE) {
            g_quasiquote_fn = as_function(resolved);
        }
    }
    
    // If quasiquote-fn not available (bootstrap mode), throw error
    if (!g_quasiquote_fn) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
            "quasiquote requires clojure.core to be fully loaded", 
            __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Delegate to Clojure quasiquote-fn
    // CRITICAL: Don't evaluate expr - pass it directly to quasiquote-fn
    // quasiquote-fn needs to process unquote/unquote-splice forms before evaluation
    ID args[] = { expr };
    ID result = eval_function_call((CljObject*)g_quasiquote_fn, args, 1, NULL, st);
    
    // The result is the expanded form - evaluate it
    if (result) {
        return eval_body(result, env, st, ctx);
    }
    return NULL;
}

// ============================================================================
// defmacro Special Form - defines a macro in the current namespace
// ============================================================================

ID eval_special_defmacro(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;
    
    // Parse: (defmacro name [params] body) or (defmacro name docstring [params] body)
    int argc = list_count(list);
    if (argc < 3) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro requires at least a name and body",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Get macro name
    ID name_obj = list_get_element(list, 1);
    if (!name_obj || TAG(name_obj) != CLJ_SYMBOL) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro name must be a symbol",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    CljSymbol *name = as_symbol(name_obj);
    
    // Skip docstring if present (string as second element)
    int params_index = 2;
    ID params_obj = list_get_element(list, params_index);
    unsigned char params_tag = params_obj ? TAG(params_obj) : 0;
    if (params_tag == CLJ_STRING) {
        params_index = 3;
        params_obj = list_get_element(list, params_index);
        params_tag = params_obj ? TAG(params_obj) : 0;
    }
    
    // Get params vector
    if (params_tag != CLJ_VECTOR) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro params must be a vector",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Build fn form: (fn [params] body...)
    // Collect body expressions in reverse order and prepend params
    CljList *fn_body = NULL;
    for (int i = argc - 1; i > params_index; i--) {
        ID body_expr = list_get_element(list, i);
        fn_body = make_ast_list(body_expr, fn_body);
    }
    
    // Create (fn [params] body...) list: fn -> [params] -> body1 -> body2 -> ...
    CljList *params_and_body = make_ast_list(params_obj, fn_body);
    CljList *fn_form = make_ast_list(SYM_FN, params_and_body);
    
    // Evaluate fn to get CljFunction (CLJ_CLOSURE type)
    ID fn_result = eval_list(fn_form, env, st, ctx);
    if (!fn_result || TAG(fn_result) != CLJ_CLOSURE) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro failed to create function",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljFunction *macro_fn = as_function(fn_result);
    
    // Set :macro true in metadata
    CljMap *meta = make_map(4);
    CljSymbol *kw_macro = intern_symbol_global(":macro");
    ASSIGN(meta, map_assoc(meta, kw_macro, clj_true));
    meta_set((CljObject*)macro_fn, (CljObject*)meta);
    RELEASE(meta);
    
    // Register macro in current namespace
    if (st->current_ns) {
        register_macro(st->current_ns, name, macro_fn);
    }
    
    // Also define as var (for (var macro-name) to work)
    ns_define(st->current_ns, name, fn_result);
    
    return fn_result;
}
