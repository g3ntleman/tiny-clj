#include "eval_special_forms.h"
#include "eval.h"
#include "common.h"
#include "channel.h"
#include "event_loop.h"
#include "vector.h"
#include "env_stack.h"
#include "exception.h"
#include "environment.h"
#include "runtime.h"
#include "symbol.h"
#include "function.h"
#include "macro.h"
#include "meta.h"
#include "ast.h"
#include "strings.h"
#include "to_string.h"
#include "debug.h"

#include <string.h>

static INLINE bool sym_name_eq(ID obj, const char *name) {
    CLJ_ASSERT(name != NULL && "sym_name_eq: name must not be NULL");
    if (!name) return false;
    if (!obj || TAG(obj) != CLJ_SYMBOL) return false;
    CljSymbol *sym = as_symbol(obj);
    if (!sym || !sym->cname) return false;
    return strcmp(sym->cname, name) == 0;
}

static void eval_finally_clause(CljList *finally_clause,
                                CljMap *env,
                                EvalState *st,
                                const EvalContext *ctx) {
    if (!finally_clause) return;
    CljList *node = list_rest_normalized(finally_clause);
    while (node) {
        ID expr = LIST_FIRST(node);
        if (expr) {
            ID r = eval_body(expr, env, st, ctx);
            RELEASE(r);
        }
        node = list_rest_normalized(node);
    }
}

// Special Form evaluation functions with unified signature (exported for symbol initialization)
ID eval_special_cond(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_cond: list must not be NULL");

    // `cond` expects pairs: test expr test expr ...
    // We validate during iteration (not using list_count upfront) to handle :else correctly
    // even in macro expansion contexts where form counting might be affected.
    // This also avoids traversing the list twice (once for counting, once for processing).
    CljList *node = list_rest_normalized(list);
    if (!node) return NULL;

    // Process pairs: (test expr) or (:else expr)
    // We validate as we go, which is more robust for macro expansion contexts
    while (node) {
        ID test = LIST_FIRST(node);
        if (!test) {
            // nil test - skip to next
            node = list_rest_normalized(node);
            continue;
        }
        
        // Check if we have an expression for this test BEFORE evaluating test
        // This ensures we detect odd number of forms even if first test is truthy
        CljList *expr_node = list_rest_normalized(node);
        if (!expr_node) {
            // No expression - check if test is :else (which would be invalid - :else needs an expr)
            bool is_else = false;
            if (test == SYM_KW_ELSE) {
                is_else = true;
            } else if (IS_KEYWORD(test)) {
                CljSymbol *kw = as_symbol(test);
                if (kw && kw->cname) {
                    // Check name comparison for non-interned keywords (macro expansion context)
                    if (strcmp(kw->cname, ":else") == 0) {
                        is_else = true;
                    }
                }
            }
            
            if (is_else) {
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                                "cond :else clause requires an expression",
                                __FILE__, __LINE__, 0);
                return NULL;
            } else {
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                                "cond requires an even number of forms",
                                __FILE__, __LINE__, 0);
                return NULL;
            }
        }
        
        ID expr = LIST_FIRST(expr_node);
        CljList *next_pair = list_rest_normalized(expr_node);
        
        // `test` and `expr` may legitimately be NULL (nil literal).
        // `eval_body` returns autoreleased objects - no manual cleanup needed.
        
        // Special case: `:else` keyword is always true (Clojure semantics)
        // Check both pointer comparison (for interned keywords) and name comparison
        // (for non-interned keywords in macro expansion contexts)
        bool truthy;
        if (test == SYM_KW_ELSE) {
            truthy = true;
        } else if (IS_KEYWORD(test)) {
            CljSymbol *kw = as_symbol(test);
            if (kw && kw->cname && strcmp(kw->cname, ":else") == 0) {
                truthy = true;
            } else {
                ID test_result = eval_body(test, env, st, ctx);
                truthy = clj_is_truthy(test_result);
                // No RELEASE needed - eval_body returns autoreleased object
            }
        } else {
            ID test_result = eval_body(test, env, st, ctx);
            truthy = clj_is_truthy(test_result);
            // No RELEASE needed - eval_body returns autoreleased object
        }
        
        if (truthy) {
            // Before returning, check if there are more elements that form an incomplete pair
            // If next_pair exists but has no second element (rest), we have an odd number
            // This means: we have (test expr remaining...) where remaining has odd count
            if (next_pair && !list_rest_normalized(next_pair)) {
                // next_pair exists but has no second element - odd number of forms
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                                "cond requires an even number of forms",
                                __FILE__, __LINE__, 0);
                return NULL;
            }
            return eval_body(expr, env, st, ctx);
        }
        
        // Move to next pair
        node = next_pair;
    }

    return NULL;
}

ID eval_special_if(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // Hot-path: avoid repeated list_get_element/list_nth traversals.
    // Structure: (if cond then else?)
    CLJ_ASSERT(list != NULL && "eval_special_if: list must not be NULL");
    if (!list) return NULL;

    CljList *args = as_list(LIST_REST(list));
    if (!args) return NULL;

    ID cond_expr = LIST_FIRST(args);
    CljList *then_node = as_list(LIST_REST(args));
    if (!then_node) return NULL;
    ID then_expr = LIST_FIRST(then_node);
    CljList *else_node = as_list(LIST_REST(then_node));
    ID else_expr = else_node ? LIST_FIRST(else_node) : NULL;

    ID cond_val = eval_arg_from_expr_with_context(cond_expr, env, st, ctx);
    bool truthy = clj_is_truthy(cond_val);
    RELEASE(cond_val);

    ID branch = truthy ? then_expr : else_expr;
    if (!branch) return NULL;
    return eval_body(branch, env, st, ctx);
}

ID eval_special_when(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_when: list must not be NULL");
    ID cond_val = eval_arg_with_context(list, 1, env, st, ctx);
    bool truthy = cond_val ? clj_is_truthy(cond_val) : false;
    RELEASE(cond_val);
    if (!truthy) return NULL;

    CljList *args = list_rest_normalized(list);
    if (!args) return NULL;

    CljList *body_node = list_rest_normalized(args);

    ID result = NULL;
    for (CljList *node = body_node; node; node = list_rest_normalized(node)) {
        ID body_expr = LIST_FIRST(node);
        CljList *next = list_rest_normalized(node);

        if (body_expr) {
            ASSIGN(result, eval_body(body_expr, env, st, ctx));
            if (!result && next) return NULL;
        }
    }
    return result;
}

ID eval_special_while(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_while: list must not be NULL");

    CljList *args = list_rest_normalized(list);
    if (!args) return NULL;

    ID cond_expr = LIST_FIRST(args);
    CljList *body_node = list_rest_normalized(args);

    while (true) {
        bool should_exit = false;
        bool should_error = false;

        WITH_AUTORELEASE_POOL({
            ID cond_val = eval_arg_from_expr_with_context(cond_expr, env, st, ctx);
            if (!cond_val || !clj_is_truthy(cond_val)) {
                RELEASE(cond_val);
                should_exit = true;
            } else {
                RELEASE(cond_val);

                ID result = NULL;
                for (CljList *node = body_node; node; node = list_rest_normalized(node)) {
                    ID body_expr = LIST_FIRST(node);
                    CljList *next = list_rest_normalized(node);

                    if (body_expr) {
                        ASSIGN(result, eval_body(body_expr, env, st, ctx));
                        if (!result && next) {
                            should_error = true;
                            break;
                        }
                    }
                    node = next;
                }
                RELEASE(result);
            }
        });

        if (should_exit) {
            return NULL;
        }
        if (should_error) {
            return NULL;
        }
    }
}

ID eval_special_do(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_do: list must not be NULL");
    ID result = NULL;
    for (CljList *node = list_rest_normalized(list); node; 
         node = list_rest_normalized(node)) {
        ID expr = LIST_FIRST(node);
        if (expr) {
            ASSIGN(result, eval_body(expr, env, st, ctx));
        }
    }
    return result;
}

ID eval_special_and(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_and: list must not be NULL");

    CljList *node = list_rest_normalized(list);
    if (!node) return clj_true;

    ID result = clj_true;
    for (; node; node = list_rest_normalized(node)) {
        ID arg = LIST_FIRST(node);
        if (arg) {
            result = eval_body(arg, env, st, ctx);
            if (!result || !clj_is_truthy(result)) {
                return result;
            }
        }
    }
    return result;
}

ID eval_special_or(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_or: list must not be NULL");

    CljList *node = list_rest_normalized(list);
    if (!node) return NULL;

    ID result = NULL;
    for (; node; node = list_rest_normalized(node)) {
        ID arg = LIST_FIRST(node);
        if (arg) {
            result = eval_body(arg, env, st, ctx);
            if (clj_is_truthy(result)) {
                return result;
            }
        }
    }
    return result;
}

ID eval_special_quote(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_quote: list must not be NULL");
    (void)env; (void)st; (void)ctx;  // Unused
    ID quoted_expr = list_get_element(list, 1);
    if (!quoted_expr) return NULL;
    return RETAIN(quoted_expr);
}

ID eval_special_throw(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_throw: list must not be NULL");

    // Shape: (throw expr)
    ID expr = list_get_element(list, 1);
    if (!expr || list_get_element(list, 2) != NULL) {
        throw_exception(EXCEPTION_ARITY, "throw requires 1 argument", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Evaluate the thrown expression in the current environment.
    ID thrown = eval_body(expr, eval_env_or_ns_mappings(env, st), st, ctx);

    // Rethrow exception objects directly.
    if (thrown && TAG(thrown) == CLJ_EXCEPTION) {
        throw_exception_object((CLJException*)thrown);
        return NULL;
    }

    // Otherwise throw a RuntimeException with a readable message.
    const char *msg = "nil";
    if (thrown) {
        msg = "throw";
        CljString *s = pr_str(thrown);
        if (s) {
            msg = string_data(s);
        }
    }
    throw_exception(EXCEPTION_RUNTIME, msg, __FILE__, __LINE__, 0);
    return NULL;
}

ID eval_special_go(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_go: list must not be NULL");
    (void)ctx;  // Unused
    CljList *do_list = NULL;
    CljList *args = list_rest_normalized(list);
    if (args) {
        do_list = make_list((CljObject*)SYM_DO, NULL);
        CljList *tail = do_list;
        for (CljList *node = args; node; node = list_rest_normalized(node)) {
            ID expr_i = LIST_FIRST(node);
            CljList *new_node = make_list(expr_i, NULL);
            if (tail) {
                tail->rest = (CljObject*)new_node;
                tail = new_node;
            }
        }
    }
    CljVector* empty_params_vec = make_vector(0);
    CljList *fn_list = make_list((CljObject*)SYM_FN, NULL);
    if (!fn_list) return NULL;
    fn_list->rest = (CljObject*)make_list(empty_params_vec, NULL);
    CljList *fn_rest = as_list(fn_list->rest);
    if (fn_rest) {
        ID body_expr = do_list;
        fn_rest->rest = (CljObject*)make_list(body_expr, NULL);
    }
    ID fn_obj = eval_fn(fn_list, env, st, NULL);
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
    CLJ_ASSERT(list != NULL && "eval_special_fn: list must not be NULL");
    return AUTORELEASE(eval_fn(list, eval_env_or_ns_mappings(env, st), st, ctx));
}

ID eval_special_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_let: list must not be NULL");
    return eval_let(list, env, st, ctx);
}

ID eval_special_var(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_var: list must not be NULL");
    (void)ctx;  // Unused
    return eval_var(list, env, st);
}

ID eval_special_recur(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_recur: list must not be NULL");
    (void)env; (void)st;  // Unused
    return eval_handle_recur(list, ctx);
}

ID eval_special_loop(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_loop: list must not be NULL");
    CLJ_ASSERT(st != NULL && "eval_special_loop: st must not be NULL");
    if (!list || !st) return NULL;

    // Shape: (loop [sym1 init1 sym2 init2 ...] body...)
    ID bindings_vec = list_get_element(list, 1);
    if (!bindings_vec || TAG(bindings_vec) != CLJ_VECTOR_PERSISTENT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires a vector for bindings", __FILE__, __LINE__, 0);
        return NULL;
    }

    CljVector *bindings = as_vector(bindings_vec);
    int binding_count = bindings ? (int)vector_count(bindings) : 0;
    if (!bindings || (binding_count % 2) != 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires an even number of forms in binding vector",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int pair_count = binding_count / 2;
    if (pair_count > CALLFRAME_MAX_PARAMS) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop has too many bindings", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Start from captured env_stack (if any), but do NOT mutate it.
    CljVector *loop_stack = (ctx && ctx->env_stack) ? (CljVector*)RETAIN(ctx->env_stack) : NULL;

    // Frame for fast local lookups.
    CallFrame loop_frame_storage;
    CallFrame *loop_frame = (pair_count > 0) ? &loop_frame_storage : NULL;
    if (loop_frame) {
        frame_init(loop_frame, ctx ? ctx->frame : NULL);
    }

    // Symbol/value arrays for frame_set_bindings.
    ID binding_slots[CALLFRAME_MAX_PARAMS * 2];
    ID *binding_params = binding_slots;
    ID *binding_values = binding_slots + pair_count;

    // Let locals map stored as top frame in env_stack.
    CljMap *loop_env_map = NULL;
    if (pair_count > 0) {
        loop_env_map = make_map(pair_count);
        env_stack_push_inplace(&loop_stack, loop_env_map);
        RELEASE(loop_env_map); // env_stack retains
    }

    EvalContext loop_ctx = ctx ? *ctx : (EvalContext){0};
    loop_ctx.frame = ctx ? ctx->frame : NULL;
    loop_ctx.env_stack = loop_stack;
    if (!loop_ctx.env) loop_ctx.env = env;
    if (!loop_ctx.st) loop_ctx.st = st;

    // Evaluate initial bindings sequentially (later inits can see earlier binds).
    int binding_index = 0;
    for (int i = 0; i < binding_count; i += 2) {
        ID sym = vector_nth(bindings, i);
        ID init_expr = vector_nth(bindings, i + 1);
        if (!sym || TAG(sym) != CLJ_SYMBOL) {
            if (loop_stack) RELEASE(loop_stack);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop binding must be a symbol", __FILE__, __LINE__, 0);
            return NULL;
        }

        ID value = NULL;
        if (!init_expr) {
            value = NULL;
        } else if (is_fixnum(init_expr) || is_special(init_expr)) {
            value = init_expr;
        } else {
            value = eval_body(init_expr, env, st, &loop_ctx);
        }

        binding_params[binding_index] = sym;
        binding_values[binding_index] = value;
        if (value && !IS_IMMEDIATE(value)) {
            RETAIN(value);
        }

        frame_set_bindings(loop_frame, ctx ? ctx->frame : NULL,
                           binding_params, binding_values, binding_index + 1);
        loop_ctx.frame = loop_frame;

        // Expose bindings via the top env_stack map for closures and symbol resolution.
        if (loop_env_map) {
            CljMap *updated = map_assoc(loop_env_map, sym, value);
            if (updated && updated != loop_env_map && loop_ctx.env_stack) {
                unsigned int top_idx = vector_count(loop_ctx.env_stack) - 1;
                vector_assoc_inplace(&loop_ctx.env_stack, top_idx, (ID)updated);
                loop_env_map = updated;
            }
        }

        if (value && !IS_IMMEDIATE(value)) {
            RELEASE(value);
        }
        binding_index++;
    }

    // Set up recur storage for loop: recur updates these values.
    ID recur_args[CALLFRAME_MAX_PARAMS];
    for (int i = 0; i < pair_count; i++) {
        recur_args[i] = binding_values[i];
        if (recur_args[i] && !IS_IMMEDIATE(recur_args[i])) {
            RETAIN(recur_args[i]);
        }
    }
    int recur_arg_count = 0;
    loop_ctx.recur_args = recur_args;
    loop_ctx.recur_arg_count = &recur_arg_count;
    loop_ctx.recur_param_count = pair_count;

    // Evaluate body until no recur happens.
    ID result = NULL;
    CljList *args = list_or_null(as_list(LIST_REST(list)));
    CljList *body_node = args ? list_or_null(as_list(LIST_REST(args))) : NULL;
    for (;;) {
        recur_arg_count = 0;

        for (CljList *node = body_node; node; node = list_or_null(as_list(LIST_REST(node)))) {
            ID body_expr = LIST_FIRST(node);
            if (!body_expr) continue;
            RELEASE(result);
            if (is_fixnum((CljValue)body_expr) || is_special((CljValue)body_expr)) {
                result = body_expr;
                RETAIN(result);
            } else {
                result = eval_body(body_expr, env, st, &loop_ctx);
            }
        }

        if (recur_arg_count <= 0) {
            break;
        }

        // Apply recur updates to bindings (frame + env_map).
        frame_set_bindings(loop_frame, ctx ? ctx->frame : NULL,
                           binding_params, recur_args, pair_count);
        loop_ctx.frame = loop_frame;

        if (loop_env_map) {
            for (int i = 0; i < pair_count; i++) {
                CljMap *updated = map_assoc(loop_env_map, binding_params[i], recur_args[i]);
                if (updated && updated != loop_env_map && loop_ctx.env_stack) {
                    unsigned int top_idx = vector_count(loop_ctx.env_stack) - 1;
                    vector_assoc_inplace(&loop_ctx.env_stack, top_idx, (ID)updated);
                    loop_env_map = updated;
                }
            }
        }
    }

    // Cleanup
    for (int i = 0; i < pair_count; i++) {
        if (binding_values[i] && !IS_IMMEDIATE(binding_values[i])) {
            RELEASE(binding_values[i]);
        }
        if (recur_args[i] && !IS_IMMEDIATE(recur_args[i])) {
            RELEASE(recur_args[i]);
        }
    }
    if (loop_stack) RELEASE(loop_stack);

    return result;
}

ID eval_special_time(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_time: list must not be NULL");
    return eval_time(list, eval_env_or_ns_mappings(env, st), st, ctx);
}

ID eval_special_dotimes(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_dotimes: list must not be NULL");
    (void)ctx;  // Unused
        return eval_dotimes(list, env, st, ctx);
}

ID eval_special_try(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_try: list must not be NULL");
    CljList *args = list_rest_normalized(list);
    if (!args) return NULL;

    // Establish base env (match other wrappers: fall back to current namespace mappings).
    CljMap *base_env = eval_env_or_ns_mappings(env, st);

    // Split body expressions from catch/finally clauses.
    // Avoid list_nth in a loop (linked lists would make this O(n^2)).
    CljList *clause_node = NULL;
    for (CljList *node = args; node; node = list_rest_normalized(node)) {
        ID elem = LIST_FIRST(node);
        if (!elem || !is_list_type(TAG(elem))) continue;
        CljList *clause = as_list(elem);
        ID first = clause ? LIST_FIRST(clause) : NULL;
        if (first == (ID)SYM_CATCH || first == (ID)SYM_FINALLY ||
            sym_name_eq(first, "catch") || sym_name_eq(first, "finally")) {
            clause_node = node;
            break;
        }
    }

    // Find optional finally clause.
    CljList *finally_clause = NULL;
    for (CljList *node = clause_node; node; node = list_rest_normalized(node)) {
        ID elem = LIST_FIRST(node);
        if (!elem || !is_list_type(TAG(elem))) continue;
        CljList *clause = as_list(elem);
        ID first = clause ? LIST_FIRST(clause) : NULL;
        if (first == (ID)SYM_FINALLY || sym_name_eq(first, "finally")) {
            finally_clause = clause;
            break;
        }
    }

    ID result = NULL;
    TRY {
        for (CljList *node = args; node && node != clause_node; node = list_rest_normalized(node)) {
            ID expr = LIST_FIRST(node);
            if (!expr) continue;
            ASSIGN(result, eval_body(expr, base_env, st, ctx));
        }
        eval_finally_clause(finally_clause, base_env, st, ctx);
        return result;
    } CATCH(ex) {
        // Exception value is a first-class CLJ_EXCEPTION object.
        ID ex_obj = RETAIN((ID)ex);

        ID handler_result = NULL;
        bool handled = false;

        for (CljList *node = clause_node; node; node = list_rest_normalized(node)) {
            ID elem = LIST_FIRST(node);
            if (!elem || !is_list_type(TAG(elem))) continue;

            CljList *clause = as_list(elem);
            ID first = clause ? LIST_FIRST(clause) : NULL;
            if (first != (ID)SYM_CATCH && !sym_name_eq(first, "catch")) continue;

            // Supported catch clause shapes:
            // - (catch sym body...)
            // - (catch Type sym body...)
            ID binding_sym = NULL;
            CljList *body_node = NULL;
            CljList *cargs = list_rest_normalized(clause);
            if (!cargs) continue;

            ID arg1 = LIST_FIRST(cargs);
            CljList *after1 = list_rest_normalized(cargs);
            if (after1) {
                ID arg2 = LIST_FIRST(after1);
                CljList *after2 = list_rest_normalized(after1);

                // If there are >= 3 args after `catch`, treat as (catch Type sym body...).
                // Otherwise treat as (catch sym body...).
                if (after2) {
                    if (is_symbol(arg2)) {
                        binding_sym = arg2;
                        body_node = after2;
                    }
                } else {
                    if (is_symbol(arg1)) {
                        binding_sym = arg1;
                        body_node = after1;
                    }
                }
            }

            // Require at least one body form (even if it evaluates to nil).
            if (!binding_sym || !body_node) {
                continue;
            }

            CljMap *catch_env = NULL;
            if (is_map(base_env)) {
                catch_env = RETAIN(map_assoc(base_env, binding_sym, ex_obj));
            } else {
                catch_env = (CljMap*)make_map(4);
                if (catch_env) {
                    ASSIGN(catch_env, map_assoc(catch_env, binding_sym, ex_obj));
                }
            }

            if (!catch_env) {
                continue;
            }

            // When ctx is provided, eval_body() uses eval_body_with_params(ctx) and ignores the
            // explicit env argument. Make the catch binding visible by extending env_stack.
            EvalContext catch_ctx_storage;
            const EvalContext *catch_ctx = ctx;
            CljVector *catch_stack = NULL;
            if (ctx) {
                catch_ctx_storage = *ctx;
                catch_stack = ctx->env_stack ? (CljVector*)RETAIN(ctx->env_stack) : NULL;
                env_stack_push_inplace(&catch_stack, catch_env);
                catch_ctx_storage.env_stack = catch_stack;
                catch_ctx = &catch_ctx_storage;
            }

            for (CljList *b = body_node; b; b = list_rest_normalized(b)) {
                ID body_expr = LIST_FIRST(b);
                if (!body_expr) continue;
                ASSIGN(handler_result, eval_body(body_expr, catch_env, st, catch_ctx));
            }

            RELEASE(catch_stack);
            RELEASE(catch_env);
            handled = true;
            break;
        }

        eval_finally_clause(finally_clause, base_env, st, ctx);

        RELEASE(ex_obj);

        if (!handled) {
            CLJException *exc = (CLJException*)ex;
            throw_exception(exc->type[0] != '\0' ? exc->type : EXCEPTION_RUNTIME,
                            exc->message[0] != '\0' ? exc->message : "Unknown error",
                            exc->file, exc->line, exc->col);
            return NULL;
        }

        return handler_result;
    } END_TRY

    return result;
}

ID eval_special_binding(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_binding: list must not be NULL");

    CljList *args = list_rest_normalized(list);
    if (!args) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding expects a bindings vector", __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!st || !st->dynamic_bindings) {
        throw_exception(EXCEPTION_RUNTIME, "binding requires an evaluation state with dynamic bindings", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Base env for evaluating init forms and body (match other wrappers).
    CljMap *base_env = eval_env_or_ns_mappings(env, st);

    ID bindings_obj = LIST_FIRST(args);
    if (!is_vector(bindings_obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding expects a vector of bindings", __FILE__, __LINE__, 0);
        return NULL;
    }

    CljVector *bindings_vec = as_vector(bindings_obj);
    unsigned int bind_count = vector_count(bindings_vec);
    if ((bind_count % 2) != 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding vector must contain an even number of forms", __FILE__, __LINE__, 0);
        return NULL;
    }

    unsigned int base_depth = vector_count(st->dynamic_bindings);
    CljNamespace *saved_ns = st->current_ns;

    // Build a single frame map: Symbol -> value.
    // NOTE: nil values are stored as DYNAMIC_BINDING_NIL so they remain distinguishable from missing.
    CljMap *frame = make_map((int)(bind_count / 2));
    if (!frame) {
        return NULL;
    }

    CljNamespace *bound_ns = NULL;

    // Evaluate init forms in the *current* dynamic context (before pushing the new frame).
    for (unsigned int i = 0; i < bind_count; i += 2) {
        ID sym_id = vector_nth(bindings_vec, i);
        ID expr_id = vector_nth(bindings_vec, i + 1);

        if (!is_symbol(sym_id)) {
            RELEASE(frame);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding keys must be symbols", __FILE__, __LINE__, 0);
            return NULL;
        }

        CljSymbol *sym = as_symbol(sym_id);
        if (!is_earmuffed_dynamic_symbol(sym)) {
            RELEASE(frame);
#if defined(STRING_FORMATTING_ENABLED) && !STRING_FORMATTING_ENABLED
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "binding requires dynamic vars (earmuffed symbols)",
                            __FILE__, __LINE__, 0);
#else
            const char *name = sym && sym->cname ? sym->cname : "<unknown>";
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "binding requires dynamic vars (earmuffed symbols), got %s", name);
#endif
            return NULL;
        }

        ID value = expr_id ? eval_body(expr_id, base_env, st, ctx) : NULL;

        // If binding *ns*, accept namespace object (preferred) or resolve symbol/string to namespace.
        if (sym == SYM_NS_STAR) {
            if (!value) {
                RELEASE(frame);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "*ns* cannot be bound to nil", __FILE__, __LINE__, 0);
                return NULL;
            }
            int tag = TAG(value);
            if (tag == CLJ_NAMESPACE) {
                bound_ns = (CljNamespace*)value;
            } else if (tag == CLJ_SYMBOL) {
                bound_ns = ns_find_by_symbol(as_symbol(value));
            } else if (tag == CLJ_STRING) {
                bound_ns = ns_find(string_data(value));
            } else {
                RELEASE(frame);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                                "*ns* must be a namespace, symbol, or string",
                                __FILE__, __LINE__, 0);
                return NULL;
            }
            if (!bound_ns) {
                RELEASE(frame);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Namespace not found", __FILE__, __LINE__, 0);
                return NULL;
            }
            value = (ID)bound_ns;
        }

        map_assoc_inplace(&frame, sym_id, value);
        RELEASE(value);
    }

    // Push the new frame and run body; unwind stack even if an exception escapes.
    vector_conj_inplace(&st->dynamic_bindings, frame);
    RELEASE(frame);

    if (bound_ns) {
        st->current_ns = bound_ns;
    }

    ID result = NULL;
    TRY {
        for (CljList *node = list_rest_normalized(args); node; node = list_rest_normalized(node)) {
            ID expr = LIST_FIRST(node);
            if (!expr) {
                ASSIGN(result, NULL);
                continue;
            }
            ASSIGN(result, eval_body(expr, base_env, st, ctx));
        }
        evalstate_pop_dynamic_bindings_to(st, base_depth);
        st->current_ns = saved_ns;
        return result;
    } CATCH(ex) {
        evalstate_pop_dynamic_bindings_to(st, base_depth);
        st->current_ns = saved_ns;
        // Re-throw after cleanup.
        throw_exception(ex->type[0] != '\0' ? ex->type : "Error",
                        ex->message[0] != '\0' ? ex->message : "Unknown error",
                        ex->file, ex->line, ex->col);
    } END_TRY

    return NULL;
}

ID eval_handle_recur(CljList *list, const EvalContext *ctx) {
    if (!ctx || !ctx->recur_args || !ctx->recur_arg_count) {
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
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
    CLJ_ASSERT(op_sym != NULL && "eval_special_form_dispatch: op_sym must not be NULL");
    if (!op_sym) return NULL;
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
    CLJ_ASSERT(list != NULL && "eval_special_quasiquote: list must not be NULL");
    CLJ_ASSERT(st != NULL && "eval_special_quasiquote: st must not be NULL");
    if (!st) return NULL;
    // Get the expression to quasiquote: (quasiquote expr)
    ID expr = list_get_element(list, 1);
    if (!expr) return NULL;
    
    // Resolve quasiquote-fn from clojure.core (lazy initialization)
    if (!g_quasiquote_fn) {
        CljSymbol *sym = intern_symbol_global("quasiquote-fn");
        CljObject *resolved = sym ? ns_resolve(st, sym) : NULL;
        if (resolved != NOT_FOUND && is_closure(resolved)) {
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
    
    // Delegate to Clojure quasiquote-fn to get an expansion form.
    // Then evaluate that expansion in the *current* env/ctx so unquote and
    // unquote-splice can see lexical bindings (Clojure-compatible behavior).
    // Finally, return (quote <value>) so callers doing (eval (quasiquote ...))
    // still get the intended data structure.
    ID args[] = { expr };
    ID expansion = eval_function_call((CljObject*)g_quasiquote_fn, args, 1, NULL, st);
    if (!expansion) {
        return NULL;
    }

    ID value = eval_body(expansion, env, st, ctx);
    if (value == SYM_NIL) {
        value = NULL;
    }

    CljList *quoted_arg = make_ast_list(value, NULL);
    CljList *quoted_form = make_ast_list(SYM_QUOTE, quoted_arg);
    return AUTORELEASE(quoted_form);
}

// ============================================================================
// defmacro Special Form - defines a macro in the current namespace
// ============================================================================

ID eval_special_defmacro(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL && "eval_special_defmacro: list must not be NULL");
    CLJ_ASSERT(st != NULL && "eval_special_defmacro: st must not be NULL");
    if (!st) return NULL;
    (void)ctx;
    
    // Parse: (defmacro name [params] body) or (defmacro name docstring [params] body)
    // Avoid list_count() + indexed access: lists are O(n) per index.
    CljList *args = list_rest_normalized(list);
    if (!args) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro requires at least a name and body",
            __FILE__, __LINE__, 0);
        return NULL;
    }

    // Get macro name
    ID name_obj = LIST_FIRST(args);
    if (!is_symbol(name_obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro name must be a symbol",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    CljSymbol *name = as_symbol(name_obj);

    // Position after name
    CljList *node = list_rest_normalized(args);
    if (!node) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro requires at least a name and body",
            __FILE__, __LINE__, 0);
        return NULL;
    }

    // Skip docstring if present (string as second element)
    ID params_obj = LIST_FIRST(node);
    if (is_string((CljObject*)params_obj)) {
        node = list_rest_normalized(node);
        params_obj = node ? LIST_FIRST(node) : NULL;
    }

    // Get params vector
    if (!is_vector(params_obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro params must be a vector",
            __FILE__, __LINE__, 0);
        return NULL;
    }

    // Build fn form: (fn [params] body...)
    // Single-pass traversal over body forms.
    CljList *body_node = list_rest_normalized(node);
    CljList *fn_body = NULL;
    CljList *fn_body_tail = NULL;
    for (CljList *b = body_node; b; b = list_rest_normalized(b)) {
        ID body_expr = LIST_FIRST(b);
        CljList *new_node = make_ast_list(body_expr, NULL);
        if (!new_node) {
            return NULL;
        }

        if (!fn_body) {
            fn_body = new_node;
            fn_body_tail = new_node;
        } else {
            ASSIGN(fn_body_tail->rest, new_node);
            RELEASE(new_node);
            fn_body_tail = as_list(fn_body_tail->rest);
        }
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
