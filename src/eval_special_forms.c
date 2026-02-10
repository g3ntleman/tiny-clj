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
#include "memory.h"
#include "parser.h"
#include "seq.h"
#include "list.h"
#include "ast.h"
#include "ast_canon.h"
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

static INLINE unsigned int args_count(CljPersistentVector *args) {
    return args ? vector_count(args) : 0;
}

static INLINE ID args_nth(CljPersistentVector *args, unsigned int idx) {
    if (!args) return NULL;
    unsigned int count = vector_count(args);
    if (idx >= count) return NULL;
    return vector_nth(args, idx);
}

// Ensure list-like forms are canonicalized into CLJ_AST_CALL on demand.
static INLINE ID ensure_ast_call(ID form, EvalState *st) {
    if (!form || IS_IMMEDIATE(form)) return form;
    CljType tag = TAG(form);
    if (tag == CLJ_AST_CALL) return form;
    if (is_list_type(tag)) {
        CLJ_ASSERT(st != NULL && "ensure_ast_call requires EvalState");
#ifdef DEBUG
        const char *ast_before = print_ast(form);
        if (ast_before) {
            DEBUG_PRINT("try: non-canonical clause before canonicalize: %s", ast_before);
            CLJ_FREE((void*)ast_before);
        } else {
            DEBUG_PRINT("try: non-canonical clause before canonicalize: <null>");
        }
#endif
        ID canon = canonicalize_ast(form, st);
#ifdef DEBUG
        if (canon && is_list_type(TAG(canon))) {
            const char *ast_after = print_ast(canon);
            if (ast_after) {
                DEBUG_PRINT("try: clause still non-canonical after canonicalize: %s", ast_after);
                CLJ_FREE((void*)ast_after);
            } else {
                DEBUG_PRINT("try: clause still non-canonical after canonicalize: <null>");
            }
        }
#endif
        return canon ? canon : form;
    }
    return form;
}

static ID seq_to_list_value(ID seq_obj) {
    if (!seq_obj) return NULL;
    if (is_list_type(TAG(seq_obj))) return RETAIN(seq_obj);
    SeqIterator iter;
    if (!seq_iter_init(&iter, seq_obj)) return NULL;
    if (seq_iter_empty(&iter)) return (ID)empty_list();

    CljPersistentVector *elems = make_vector(8, STRONG);
    if (!elems) return NULL;

    while (!seq_iter_empty(&iter)) {
        ID elem = seq_iter_first(&iter);
        vector_conj_inplace(&elems, elem);
        seq_iter_next(&iter);
    }

    int count = vector_count(elems);
    CljList *list = empty_list();
    for (int i = count - 1; i >= 0; i--) {
        ID elem = vector_nth(elems, i);
        list = make_list(elem, list);
    }
    RELEASE(elems);
    return (ID)list;
}

static void eval_finally_clause(ID finally_clause,
                                CljPersistentMap *env,
                                EvalState *st,
                                const EvalContext *ctx) {
    if (!finally_clause) return;

    CljType tag = TAG(finally_clause);
    if (is_list_type(tag)) {
        CljList *node = list_rest_normalized(as_list(finally_clause));
        while (node) {
            ID expr = LIST_FIRST(node);
            if (expr) {
                (void)eval_body(expr, env, st, ctx);
            }
            node = list_rest_normalized(node);
        }
        return;
    }

    if (tag == CLJ_AST_CALL) {
        CljASTCall *call = as_ast_call(finally_clause);
        CljPersistentVector *args = call ? call->args : NULL;
        unsigned int count = args ? vector_count(args) : 0;
        for (unsigned int i = 0; i < count; i++) {
            ID expr = vector_nth(args, i);
            if (!expr) continue;
            (void)eval_body(expr, env, st, ctx);
        }
    }
}

// Special Form evaluation functions with unified signature (exported for symbol initialization)
ID eval_special_cond(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    // `cond` expects pairs: test expr test expr ...
    // We validate during iteration (not using vector_count upfront) to handle :else correctly
    // even in macro expansion contexts where form counting might be affected.
    unsigned int argc = args_count(args);
    if (argc == 0) return NULL;

    unsigned int i = 0;
    while (i < argc) {
        ID test = args_nth(args, i);
        if (!test) {
            // nil test - skip to next element (preserve legacy list semantics)
            i++;
            continue;
        }

        if (i + 1 >= argc) {
            // No expression - check if test is :else (which would be invalid - :else needs an expr)
            bool is_else = false;
            if (test == SYM_KW_ELSE) {
                is_else = true;
            } else if (IS_KEYWORD(test)) {
                CljSymbol *kw = as_symbol(test);
                if (kw && kw->cname && strcmp(kw->cname, ":else") == 0) {
                    is_else = true;
                }
            }

            if (is_else) {
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                                "cond :else clause requires an expression",
                                __FILE__, __LINE__, 0);
                return NULL;
            }

            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "cond requires an even number of forms",
                            __FILE__, __LINE__, 0);
            return NULL;
        }

        ID expr = args_nth(args, i + 1);
        unsigned int next_index = i + 2;

        // `test` and `expr` may legitimately be NULL (nil literal).
        // `eval_body` returns autoreleased objects - no manual cleanup needed.
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
            }
        } else {
            ID test_result = eval_body(test, env, st, ctx);
            truthy = clj_is_truthy(test_result);
        }

        if (truthy) {
            // Before returning, check if there are more elements that form an incomplete pair
            unsigned int remaining = (argc > next_index) ? (argc - next_index) : 0;
            if ((remaining % 2) != 0) {
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                                "cond requires an even number of forms",
                                __FILE__, __LINE__, 0);
                return NULL;
            }
            return eval_body(expr, env, st, ctx);
        }

        // Move to next pair
        i = next_index;
    }

    return NULL;
}

ID eval_special_if(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    // Hot-path: avoid repeated vector_nth traversals.
    // Structure: (if cond then else?)
    unsigned int argc = args_count(args);
    if (argc < 2) return NULL;

    ID cond_expr = args_nth(args, 0);
    ID then_expr = args_nth(args, 1);
    ID else_expr = (argc >= 3) ? args_nth(args, 2) : NULL;

    ID cond_val = eval_arg_from_expr_with_context(cond_expr, env, st, ctx);
    bool truthy = clj_is_truthy(cond_val);

    ID branch = truthy ? then_expr : else_expr;
    if (!branch) return NULL;
    // Bypass eval_body wrapper when ctx is set: saves ~304 bytes of stack per if-level
    // (eval_body allocates its full frame even when it just forwards to eval_body_with_params)
    return ctx ? eval_body_with_params(branch, ctx) : eval_body(branch, env, st, NULL);
}

ID eval_special_when(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    ID cond_expr = (argc >= 1) ? args_nth(args, 0) : NULL;
    ID cond_val = eval_arg_from_expr_with_context(cond_expr, env, st, ctx);
    bool truthy = cond_val ? clj_is_truthy(cond_val) : false;
    if (!truthy) return NULL;

    ID result = NULL;
    for (unsigned int i = 1; i < argc; i++) {
        ID body_expr = args_nth(args, i);
        bool has_next = (i + 1) < argc;

        if (body_expr) {
            // Bypass eval_body wrapper when ctx is set: saves ~304 bytes of stack
            ASSIGN(result, ctx ? eval_body_with_params(body_expr, ctx)
                               : eval_body(body_expr, env, st, NULL));
            if (!result && has_next) return NULL;
        }
    }
    return result;
}

ID eval_special_while(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    if (argc == 0) return NULL;

    ID cond_expr = args_nth(args, 0);

    while (true) {
        bool should_exit = false;
        bool should_error = false;

        WITH_AUTORELEASE_POOL({
            ID cond_val = eval_arg_from_expr_with_context(cond_expr, env, st, ctx);
            if (!cond_val || !clj_is_truthy(cond_val)) {
                should_exit = true;
            } else {
                ID result = NULL;
                for (unsigned int i = 1; i < argc; i++) {
                    ID body_expr = args_nth(args, i);
                    bool has_next = (i + 1) < argc;

                    if (body_expr) {
                        ASSIGN(result, eval_body(body_expr, env, st, ctx));
                        if (!result && has_next) {
                            should_error = true;
                            break;
                        }
                    }
                }
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

ID eval_special_do(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    ID result = NULL;
    for (unsigned int i = 0; i < argc; i++) {
        ID expr = args_nth(args, i);
        if (expr) {
            ASSIGN(result, eval_body(expr, env, st, ctx));
        }
    }
    return result;
}

ID eval_special_and(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    if (argc == 0) return clj_true;

    ID result = clj_true;
    for (unsigned int i = 0; i < argc; i++) {
        ID arg = args_nth(args, i);
        if (arg) {
            result = eval_body(arg, env, st, ctx);
            if (!result || !clj_is_truthy(result)) {
                return result;
            }
        }
    }
    return result;
}

ID eval_special_or(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    if (argc == 0) return NULL;

    ID result = NULL;
    for (unsigned int i = 0; i < argc; i++) {
        ID arg = args_nth(args, i);
        if (arg) {
            result = eval_body(arg, env, st, ctx);
            if (clj_is_truthy(result)) {
                return result;
            }
        }
    }
    return result;
}

ID eval_special_quote(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    (void)env; (void)st; (void)ctx;  // Unused
    ID quoted_expr = args_nth(args, 0);
    if (!quoted_expr) return NULL;
    if (IS_IMMEDIATE(quoted_expr)) return quoted_expr;
    // Otherwise retain to decouple from AST lifetime, then autorelease for caller ownership.
    return AUTORELEASE(RETAIN(quoted_expr));
}

ID eval_special_throw(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    // Shape: (throw expr)
    unsigned int argc = args_count(args);
    ID expr = (argc >= 1) ? args_nth(args, 0) : NULL;
    if (!expr || argc != 1) {
        throw_exception(EXCEPTION_ARITY, "throw requires 1 argument", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Evaluate the thrown expression in the current environment.
    ID thrown = eval_body(expr, eval_env_or_ns_mappings(env, st), st, ctx);

    // Rethrow exception objects directly.
    if (thrown && TAG(thrown) == CLJ_EXCEPTION) {
        throw_exception_object((CLJException*)thrown);
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

ID eval_special_go(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    CljList *do_list = NULL;
    unsigned int argc = args_count(args);
    if (argc > 0) {
        CljList *body_list = NULL;
        for (int i = (int)argc - 1; i >= 0; i--) {
            ID expr_i = args_nth(args, (unsigned int)i);
            body_list = make_list(expr_i, body_list);
        }
        do_list = make_list((CljObject*)SYM_DO, body_list);
    }

    CljPersistentVector* empty_params_vec = make_vector(0, STRONG);
    CljPersistentVector *fn_args = make_vector(2, STRONG);
    vector_conj_inplace(&fn_args, (ID)empty_params_vec);
    RELEASE(empty_params_vec);
    vector_conj_inplace(&fn_args, (ID)do_list);

    ID fn_obj = eval_fn(fn_args, env, st, NULL);
    RELEASE(fn_args);
    if (!fn_obj) {
        RELEASE(do_list);
        return NULL;
    }
    CljTransientMap *chan = make_result_channel();
    event_loop_enqueue(fn_obj, chan);
    RELEASE(do_list);
    return (CljObject*)chan;
}

// Wrapper functions for existing special form evaluators
ID eval_special_fn(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    return AUTORELEASE(eval_fn(args, eval_env_or_ns_mappings(env, st), st, ctx));
}

ID eval_special_let(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    return eval_let(args, env, st, ctx);
}

ID eval_special_var(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    return eval_var(args, env, st);
}

ID eval_special_recur(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    (void)env; (void)st;  // Unused
    return eval_handle_recur(args, ctx);
}

ID eval_special_loop(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(st != NULL && "eval_special_loop: st must not be NULL");
    if (!args || !st) return NULL;

    // Shape: (loop [sym1 init1 sym2 init2 ...] body...)
    ID bindings_vec = args_nth(args, 0);
    if (!bindings_vec || TAG(bindings_vec) != CLJ_VECTOR_PERSISTENT) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires a vector for bindings", __FILE__, __LINE__, 0);
        return NULL;
    }

    CljPersistentVector *bindings = as_vector(bindings_vec);
    int binding_count = bindings ? (int)vector_count(bindings) : 0;
    if (!bindings || (binding_count % 2) != 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop requires an even number of forms in binding vector",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int pair_count = binding_count / 2;
    if (pair_count > CALLFRAME_MAX_PARAMS) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop has too many bindings", __FILE__, __LINE__, 0);
    }

    // Start from captured env_stack (if any), but do NOT mutate it.
    CljPersistentVector *loop_stack = (ctx && ctx->env_stack) ? (CljPersistentVector*)RETAIN(ctx->env_stack) : NULL;

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
    CljPersistentMap *loop_env_map = NULL;
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
        RELEASE(loop_stack);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "loop binding must be a symbol", __FILE__, __LINE__, 0);
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
            CljPersistentMap *updated = map_assoc(loop_env_map, sym, value);
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
    unsigned int argc = args_count(args);
    for (;;) {
        recur_arg_count = 0;

        for (unsigned int i = 1; i < argc; i++) {
            ID body_expr = args_nth(args, i);
            if (!body_expr) continue;
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
                CljPersistentMap *updated = map_assoc(loop_env_map, binding_params[i], recur_args[i]);
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
    RELEASE(loop_stack);

    return result;
}

ID eval_special_time(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    return eval_time(args, eval_env_or_ns_mappings(env, st), st, ctx);
}

#ifdef DEBUG
ID eval_special_heap(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    return eval_heap(args, eval_env_or_ns_mappings(env, st), st, ctx);
}
#endif

ID eval_special_dotimes(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    (void)ctx;  // Unused
    return eval_dotimes(args, env, st, ctx);
}

ID eval_special_try(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    if (argc == 0) return NULL;
    CljPersistentMap *env_vol = env;
    // Establish base env (match other wrappers: fall back to current namespace mappings).
    CljPersistentMap *base_env = eval_env_or_ns_mappings(env_vol, st);

    // Split body expressions from catch/finally clauses.
    int clause_index = -1;
    for (unsigned int i = 0; i < argc; i++) {
        ID elem = args_nth(args, i);
        if (!elem) continue;
        elem = ensure_ast_call(elem, st);
        if (!elem || IS_IMMEDIATE(elem)) continue;
        CljType tag = TAG(elem);
        ID first = NULL;
        if (is_list_type(tag)) {
            CLJ_ASSERT(tag == CLJ_AST_CALL);
            continue;
        } else if (tag == CLJ_AST_CALL) {
            CljASTCall *call = as_ast_call(elem);
            first = call ? call->op : NULL;
        } else {
            continue;
        }
        if (first == (ID)SYM_CATCH || first == (ID)SYM_FINALLY ||
            sym_name_eq(first, "catch") || sym_name_eq(first, "finally")) {
            clause_index = (int)i;
            break;
        }
    }
    if (clause_index < 0) {
        clause_index = (int)argc;
    }

    // Find optional finally clause.
    ID finally_clause = NULL;
    for (unsigned int i = (unsigned int)clause_index; i < argc; i++) {
        ID elem = args_nth(args, i);
        if (!elem) continue;
        elem = ensure_ast_call(elem, st);
        if (!elem || IS_IMMEDIATE(elem)) continue;
        CljType tag = TAG(elem);
        ID first = NULL;
        if (is_list_type(tag)) {
            CLJ_ASSERT(tag == CLJ_AST_CALL);
            continue;
        } else if (tag == CLJ_AST_CALL) {
            CljASTCall *call = as_ast_call(elem);
            first = call ? call->op : NULL;
        } else {
            continue;
        }
        if (first == (ID)SYM_FINALLY || sym_name_eq(first, "finally")) {
            finally_clause = elem;
            break;
        }
    }

    ID result = NULL;
    TRY {
        for (unsigned int i = 0; i < (unsigned int)clause_index; i++) {
            ID expr = args_nth(args, i);
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

        for (unsigned int i = (unsigned int)clause_index; i < argc; i++) {
            ID elem = args_nth(args, i);
            if (!elem) continue;
            elem = ensure_ast_call(elem, st);
            if (!elem || IS_IMMEDIATE(elem)) continue;

            CljASTCall *clause_call = NULL;
            ID first = NULL;
            CljType elem_tag = TAG(elem);
            if (is_list_type(elem_tag)) {
                CLJ_ASSERT(elem_tag == CLJ_AST_CALL);
                continue;
            } else if (elem_tag == CLJ_AST_CALL) {
                clause_call = as_ast_call(elem);
                first = clause_call ? clause_call->op : NULL;
            } else {
                continue;
            }

            if (first != (ID)SYM_CATCH && !sym_name_eq(first, "catch")) continue;

            // Supported catch clause shapes:
            // - (catch sym body...)
            // - (catch Type sym body...)
            ID binding_sym = NULL;
            CljPersistentVector *call_args = NULL;
            unsigned int body_start = 0;

            call_args = clause_call ? clause_call->args : NULL;
            unsigned int ccount = call_args ? vector_count(call_args) : 0;
            if (ccount >= 3) {
                binding_sym = vector_nth(call_args, 1);
                body_start = 2;
            } else if (ccount >= 2) {
                binding_sym = vector_nth(call_args, 0);
                body_start = 1;
            } else {
                continue;
            }

            // Require at least one body form (even if it evaluates to nil).
            if (!binding_sym || !is_symbol(binding_sym)) {
                continue;
            }
            if (!call_args) continue;

            CljPersistentMap *catch_env = NULL;
            if (is_map(base_env)) {
                catch_env = RETAIN(map_assoc(base_env, binding_sym, ex_obj));
            } else {
                catch_env = (CljPersistentMap*)make_map(4);
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
            CljPersistentVector *catch_stack = NULL;
            if (ctx) {
                catch_ctx_storage = *ctx;
                catch_stack = ctx->env_stack ? (CljPersistentVector*)RETAIN(ctx->env_stack) : NULL;
                env_stack_push_inplace(&catch_stack, catch_env);
                catch_ctx_storage.env_stack = catch_stack;
                catch_ctx = &catch_ctx_storage;
            }

            unsigned int body_count = vector_count(call_args);
            for (unsigned int bi = body_start; bi < body_count; bi++) {
                ID body_expr = vector_nth(call_args, bi);
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

ID eval_special_binding(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    unsigned int argc = args_count(args);
    if (argc == 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding expects a bindings vector", __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!st || !st->dynamic_bindings) {
        throw_exception(EXCEPTION_RUNTIME, "binding requires an evaluation state with dynamic bindings", __FILE__, __LINE__, 0);
    }

    CljPersistentMap *env_vol = env;
    // Base env for evaluating init forms and body (match other wrappers).
    CljPersistentMap *base_env = eval_env_or_ns_mappings(env_vol, st);

    ID bindings_obj = args_nth(args, 0);
    if (!is_vector(bindings_obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding expects a vector of bindings", __FILE__, __LINE__, 0);
    }

    CljPersistentVector *bindings_vec = as_vector(bindings_obj);
    unsigned int bind_count = vector_count(bindings_vec);
    if ((bind_count % 2) != 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "binding vector must contain an even number of forms", __FILE__, __LINE__, 0);
        return NULL;
    }

    unsigned int base_depth = vector_count(st->dynamic_bindings->backing);
    CljNamespace *saved_ns = st->current_ns;

    // Build a single frame map: Symbol -> value.
    // NOTE: nil values are stored as DYNAMIC_BINDING_NIL so they remain distinguishable from missing.
    CljPersistentMap *frame = make_map((int)(bind_count / 2));
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
            }
            value = (ID)bound_ns;
        }

        map_assoc_inplace(&frame, sym_id, value);
        RELEASE(value);
    }

    // Push the new frame and run body; unwind stack even if an exception escapes.
    vector_push(st->dynamic_bindings, frame);
    RELEASE(frame);

    if (bound_ns) {
        st->current_ns = bound_ns;
    }

    ID result = NULL;
    TRY {
        for (unsigned int i = 1; i < argc; i++) {
            ID expr = args_nth(args, i);
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

ID eval_handle_recur(CljPersistentVector *args, const EvalContext *ctx) {
    if (!ctx || !ctx->recur_args || !ctx->recur_arg_count) {
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
    }

    // Get expected param count from recur context (set by function call)
    int expected = ctx->recur_param_count;
    int provided = (int)args_count(args);
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
    for (int arg_index = 0; arg_index < expected; arg_index++) {
        ID arg = args_nth(args, (unsigned int)arg_index);
        if (arg) {
            ID eval_arg = eval_body_with_params(arg, &arg_ctx);
            if (eval_arg) {
                evaluated_args[arg_index] = RETAIN(eval_arg);
            }
        }
    }

    if (provided > expected) {
        for (int i = 0; i < expected; i++) {
            RELEASE(evaluated_args[i]);
        }
        throw_exception(EXCEPTION_ARITY, "recur arity mismatch", NULL, 0, 0);
    }

    for (int i = 0; i < expected; i++) {
        RELEASE(ctx->recur_args[i]);
        ctx->recur_args[i] = evaluated_args[i];
    }
    *ctx->recur_arg_count = expected;

    return NULL;
}

// ============================================================================
// Quasiquote Special Form - delegates to Clojure quasiquote-fn after bootstrap
// ============================================================================

// Cached Clojure quasiquote-fn (resolved after bootstrap)
static CljFunction *g_quasiquote_fn = NULL;

void eval_special_forms_reset_caches(void) {
    g_quasiquote_fn = NULL;
}

ID eval_special_quasiquote(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(st != NULL && "eval_special_quasiquote: st must not be NULL");
    if (!st) return NULL;
    // Get the expression to quasiquote: (quasiquote expr)
    ID expr = args_nth(args, 0);
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
    ID qq_args[] = { expr };
    ID expansion = eval_function_call((CljObject*)g_quasiquote_fn, qq_args, 1, NULL, st);
    if (!expansion) {
        return NULL;
    }

    ID canonical = canonicalize_ast(expansion, st);
    if (!canonical) {
        return NULL;
    }

    ID value = eval_body(canonical, env, st, ctx);
    if (value == SYM_NIL) {
        value = NULL;
    }

    if (value && is_seq(value) && !is_list_type(TAG(value))) {
        ID list_value = seq_to_list_value(value);
        if (list_value) {
            value = list_value;
        }
    }

    CljList *quoted_arg = make_ast_list(value, NULL);
    CljList *quoted_form = make_ast_list(SYM_QUOTE, quoted_arg);
    RELEASE(quoted_arg);
    return AUTORELEASE(quoted_form);
}

// ============================================================================
// defmacro Special Form - defines a macro in the current namespace
// ============================================================================

ID eval_special_defmacro(CljPersistentVector *args, CljPersistentMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(st != NULL && "eval_special_defmacro: st must not be NULL");
    if (!st) return NULL;
    (void)ctx;

    // Parse: (defmacro name [params] body) or (defmacro name docstring [params] body)
    unsigned int argc = args_count(args);
    if (argc < 2) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro requires at least a name and body",
            __FILE__, __LINE__, 0);
        return NULL;
    }

    // Get macro name
    ID name_obj = args_nth(args, 0);
    if (!is_symbol(name_obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro name must be a symbol",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    CljSymbol *name = as_symbol(name_obj);

    unsigned int index = 1;
    ID params_obj = args_nth(args, index);
    if (is_string((CljObject*)params_obj)) {
        index++;
        params_obj = args_nth(args, index);
    }

    // Get params vector
    if (!is_vector(params_obj)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro params must be a vector",
            __FILE__, __LINE__, 0);
        return NULL;
    }

    unsigned int body_start = index + 1;
    if (body_start >= argc) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro requires at least a name and body",
            __FILE__, __LINE__, 0);
        return NULL;
    }

    // Build fn form: (fn [params] body...)
    // Single-pass traversal over body forms.
    CljList *fn_body = NULL;
    CljList *fn_body_tail = NULL;
    for (unsigned int i = body_start; i < argc; i++) {
        ID body_expr = args_nth(args, i);
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
    RELEASE(fn_body);
    CljList *fn_form = make_ast_list(SYM_FN, params_and_body);
    RELEASE(params_and_body);
    
    // Evaluate fn to get CljFunction (CLJ_CLOSURE type)
    // Use eval_parsed so the form is canonicalized (fn is a special form).
    ID fn_result = eval_parsed((ID)fn_form, st, env);
    RELEASE(fn_form);
    if (!fn_result || TAG(fn_result) != CLJ_CLOSURE) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
            "defmacro failed to create function",
            __FILE__, __LINE__, 0);
        return NULL;
    }
    
    CljFunction *macro_fn = as_function(fn_result);
    
    // Set :macro true in metadata
    CljPersistentMap *meta = make_map(4);
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
