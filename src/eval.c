#include "object.h"
#include "eval.h"
#include "symbol.h"
#include "exception.h"
#include "function.h"
#include "validation.h"
#include "builtins.h"
#include "optimize.h"
#include "parser.h"  // For eval_parsed
#include "common.h"  // For INLINE macro

#include "error_messages.h"
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include "seq.h"
#include "namespace.h"
#include "memory.h"
#include "meta.h"
#include "list.h"
#include "value.h"
#include "environment.h"
#include "ast.h"
#include "vector.h"
#include "event_loop.h"
#include "channel.h"
#include "strings.h"  // For pr_str
#include "to_string.h"  // For is_special_symbol
#include "eval_arithmetic.h"
#include "eval_comparison.h"
#include <time.h>

#include "eval_sequence.h"
#include "eval_special_forms.h"

static void rewrite_recursive_calls_in_slot(ID *slot, CljSymbol *unqualified, CljSymbol *qualified) {
    if (!slot || !unqualified || !qualified) {
        return;
    }
    ID expr = *slot;
    if (!expr || IS_IMMEDIATE(expr)) {
        return;
    }

    unsigned char tag = TAG(expr);
    if (tag == CLJ_SYMBOL) {
        if ((CljSymbol*)expr == unqualified) {
            *slot = qualified;
        }
        return;
    }

    if (tag == CLJ_AST_NODE) {
        CljASTNode *node = as_ast_node(expr);
        if (node) {
            rewrite_recursive_calls_in_slot((ID*)&node->first, unqualified, qualified);
            rewrite_recursive_calls_in_slot((ID*)&node->rest, unqualified, qualified);
        }
        return;
    }

    if (tag == CLJ_LIST) {
        CljList *list = as_list(expr);
        if (list) {
            rewrite_recursive_calls_in_slot((ID*)&list->first, unqualified, qualified);
            rewrite_recursive_calls_in_slot((ID*)&list->rest, unqualified, qualified);
        }
        return;
    }

    if (tag == CLJ_VECTOR) {
        CljVector *vec = as_vector(expr);
        if (vec) {
            unsigned int count = vector_count(vec);
            ID *data = vector_as_array(vec);
            if (data) {
                for (unsigned int i = 0; i < count; ++i) {
                    rewrite_recursive_calls_in_slot(&data[i], unqualified, qualified);
                }
            }
        }
        return;
    }
}

// Use C stack for recur state - each function call has its own stack frame
// No global variables needed - local variables in eval_function_call are automatically isolated

// Evaluation context structures are defined in function_call.h

#include "map.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// Global variable to suppress time output in tests
static bool g_suppress_time_output = false;

// Function to set time output suppression (for tests)
void set_suppress_time_output(bool suppress) {
    g_suppress_time_output = suppress;
}

// Resolve cache helpers ----------------------------------------------------
static inline CljSymbol* resolve_cache_ns_key(CljSymbol *op_sym, EvalState *st) {
    if (op_sym && op_sym->ns_name) {
        return op_sym->ns_name;
    }
    if (st && st->current_ns && st->current_ns->name) {
        return st->current_ns->name;
    }
    return NULL;
}

static inline ID resolve_cache_lookup_value(CljSymbol *ns_key, ID op) {
    if (!ns_key || !g_runtime.resolve_cache) {
        return NULL;
    }
    CljMap *ns_cache = (CljMap*)map_get(g_runtime.resolve_cache, ns_key, NULL);
    if (!ns_cache) {
        return NULL;
    }
    return map_get(ns_cache, op, NULL);
}

static void resolve_cache_store_value(CljSymbol *ns_key, ID op, ID resolved) {
    if (!ns_key || !g_runtime.resolve_cache) {
        return;
    }
    CljMap *ns_cache = (CljMap*)map_get(g_runtime.resolve_cache, ns_key, NULL);
    if (!ns_cache) {
        ns_cache = make_map(RESOLVE_CACHE_SIZE);
    }
    ns_cache = map_assoc(ns_cache, op, resolved);
    CljMap *updated_cache = map_assoc(g_runtime.resolve_cache, ns_key, ns_cache);
    ASSIGN(g_runtime.resolve_cache, updated_cache);
}

// Forward declarations
ID eval_body_with_params(ID body, const EvalContext *ctx);
ID eval_time(CljList *list, CljMap *env, EvalState *st);
ID eval_fn_with_context(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_arg_from_expr_with_context(ID expr, CljMap *env, EvalState *st, const EvalContext *ctx);
// is_special_symbol is now in symbol.c
static inline bool is_builtin_function(CljSymbol *symbol);



// Forward declarations for loop evaluation
ID eval_body_with_env(ID body, CljMap *env, EvalState *st);



// Helper function to throw unresolved symbol exception (DRY principle)
static void throw_unresolved_symbol_exception(const char *sym_name) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
        "Unable to resolve symbol: %s in this context", sym_name);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st) {
    // for Clojure functions. For native functions, env is not used.
    (void)env; // Suppress unused parameter warning

    CLJ_ASSERT(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    // Check if it's a native function (CljCFunc) or Clojure function (CljFunction)
    if (is_native_fn(fn)) {
        // It's a native C function (CljCFunc)
        CljCFunc *native_func = (CljCFunc*)fn;
        CLJ_ASSERT(native_func && native_func->fn);
        // Set EvalState for builtins that need it (eval, read-string)
        extern void builtin_set_eval_state(EvalState *st);
        builtin_set_eval_state(st);
        ID result = native_func->fn((CljObject**)args, argc);
        builtin_set_eval_state(NULL); // Clear after call
        return result;
    }

    // It's a Clojure function (CljFunction)
    CljFunction *func = (CljFunction*)fn;
    if (!func) {
        return make_exception(EXCEPTION_RUNTIME, "Invalid function object", NULL, 0, 0);
    }

    // Arity check
    int param_count = func->params ? vector_count(func->params) : 0;
    if (argc != param_count) {
        throw_exception(EXCEPTION_ARITY, "Arity mismatch in function call", NULL, 0, 0);
        return NULL;
    }

    // This prevents stack overflow in deep recursion while still allowing proper cleanup

    // OPTIMIZATION: Use static arrays instead of STACK_ALLOC to avoid alloca overhead
    // Max 16 args supported, wastes some stack space but eliminates ___chkstk_darwin calls
    ID current_args[16];
    ID recur_args[16];
    int used_recur_slots = 0;
    // Only initialize slots we actually need (param_count, not all 16)
    for (int i = 0; i < param_count; i++) {
        current_args[i] = (i < argc) ? args[i] : NULL;
        recur_args[i] = NULL;
    }
    int current_argc = argc;
    int recur_arg_count = -1;

    // Create call frame with parameters (stack-allocated)
    ID *params_array = vector_as_array(func->params);
    size_t frame_bytes = frame_allocation_size(param_count);
    CallFrame *call_frame = (CallFrame*)STACK_ALLOC(char, frame_bytes);
    CallFrame *parent_frame = NULL;
    frame_init(call_frame, parent_frame);
    frame_set_bindings(call_frame, parent_frame, params_array, current_args, current_argc);

    // Legacy: Keep env_stack for closure environment (func->env_stack)
    // This is only used for closure bindings, not function parameters
    // NOTE: func->env_stack is borrowed (not retained) - func lives during the entire call
    CljList *call_env_stack = func->env_stack;

    // TCO Loop - iterate on recur
    ID result = NULL;
    do {
        // Reset recur state for each iteration
        recur_arg_count = -1;  // -1 = no tail call

        // OPTIMIZATION: Only cleanup recur_args if recur was actually used in previous iteration
        // For functions without recur (like fib), this check is always false - zero overhead
        if (used_recur_slots > 0) {
            for (int i = 0; i < used_recur_slots; i++) {
                RELEASE(recur_args[i]);
                recur_args[i] = NULL;
            }
            used_recur_slots = 0;
        }

        // Evaluate function body with context (stack-only, no allocations)
        EvalContext eval_ctx = {
            .env = NULL,
            .env_stack = call_env_stack,  // Legacy: closure environment only
            .frame = call_frame,          // Stack-based frame for parameters
            .st = st,
            .params = params_array,
            .param_values = current_args,
            .param_count = current_argc,
            .recur_args = recur_args,
            .recur_arg_count = &recur_arg_count
        };
        // If an exception is thrown, longjmp will jump to the outer handler and this function
        // will never return, so the loop will not continue
        ID new_result = eval_body_with_params(func->body, &eval_ctx);
        // Check if recur was triggered in THIS function
        // With C stack, nested functions have their own stack frames, so recur_arg_count
        // only changes if recur was used in THIS function
        if (recur_arg_count >= 0) {
            // Tail call detected: recur was used in this function
            CLJ_ASSERT(recur_arg_count <= param_count);
            // RELEASE handles NULL and immediates automatically
            RELEASE(new_result);

            // Update argc and copy new arguments from recur_args
            CLJ_ASSERT(recur_arg_count >= 0 && recur_arg_count <= param_count);
            current_argc = recur_arg_count;
            for (int i = 0; i < current_argc; i++) {
                current_args[i] = recur_args[i]; // Already retained in recur evaluation
                recur_args[i] = NULL; // Clear to prevent double-release
            }
            used_recur_slots = current_argc;

            // Recreate call frame with new parameters (stack-allocated)
            frame_set_bindings(call_frame, parent_frame, params_array, current_args, current_argc);

            // Continue loop - recur_arg_count will be reset at the start of the next iteration
            continue;
        }

        // No recur - this is the final result
        // Use ASSIGN for proper refcounting (handles retain/release automatically)
        ASSIGN(result, new_result);
        break;
    } while (true);

    // current_args[i] is stored in call_env, and call_env holds a reference to it.
    // If we release current_args[i] here, the object might be freed, but call_env
    // still holds a pointer to it. When call_env is released later, RELEASE will
    // be called on the already-freed object, causing a use-after-free error.
    // The call_env will be released below, which will properly release all stored values.

    // OPTIMIZATION: Only cleanup if recur args were actually set
    if (used_recur_slots > 0) {
        for (int i = 0; i < used_recur_slots; i++) {
            RELEASE(recur_args[i]);
        }
    }

    // Cleanup call frame (stack-allocated, but may contain retained values)
    frame_release(call_frame);
    
    // NOTE: call_env_stack is borrowed from func->env_stack, no release needed

    return result;
}


// DRY: Central symbol resolution function with environment stack support
// Resolves symbol by searching through environment stack (list of maps)
static inline CljMap* env_stack_head(CljList *stack) {
    if (stack && list_type_matches(TAG(stack))) {
        ID first = LIST_FIRST(stack);
        if (first && TAG(first) == CLJ_MAP) {
            return (CljMap*)first;
        }
    }
    return NULL;
}

static inline CljMap *get_closure_env(const EvalContext *ctx) {
    if (!ctx) {
        return NULL;
    }
    CljMap *from_stack = env_stack_head(ctx->env_stack);
    if (from_stack) {
        return from_stack;
    }
    return ctx->env;
}

static inline EvalState *get_eval_state(const EvalContext *ctx, EvalState *fallback) {
    if (ctx && ctx->st) {
        return ctx->st;
    }
    return fallback;
}

static const EvalContext* ensure_eval_context(CljMap *env,
                                              EvalState *st,
                                              const EvalContext *ctx,
                                              EvalContext *local_ctx,
                                              CljList **owned_stack) {
    *owned_stack = NULL;
    if (!ctx) {
        *local_ctx = (EvalContext){
            .env = env,
        .env_stack = env ? make_list(env, NULL) : NULL,
            .st = st,
            .params = NULL,
            .param_values = NULL,
            .param_count = 0,
            .recur_args = NULL,
            .recur_arg_count = NULL
        };
        *owned_stack = local_ctx->env_stack;
        return local_ctx;
    }

    *local_ctx = *ctx;

    if (!local_ctx->env_stack && env) {
        local_ctx->env_stack = make_list(env, NULL);
        *owned_stack = local_ctx->env_stack;
    }

    if (!local_ctx->env) {
        local_ctx->env = get_closure_env(local_ctx);
        if (!local_ctx->env) {
            local_ctx->env = env;
        }
    }

    if (!local_ctx->st) {
        local_ctx->st = st;
    }

    return local_ctx;
}

// Extended version that also searches in CallFrame
static INLINE ID resolve_symbol_in_env_with_frame(CljList *env_stack, CljMap *fallback_env, CallFrame *frame, ID sym, EvalState *st) {
    if (!sym || TAG(sym) != CLJ_SYMBOL) {
        return NULL;
    }

    // Fast-path: Check frame first (most common case for parameters)
    if (frame) {
        ID frame_value = FRAME_NIL_SENTINEL;
        if (frame_lookup(frame, sym, &frame_value)) {
            return frame_value;
        }
    }

    // OPTIMIZATION: If no env_stack and no fallback_env, go direct to namespace
    // This is common for function calls like (fib ...) where fib is in namespace
    if (!env_stack && !fallback_env) {
        if (st) {
            ID resolved_ns = eval_symbol(as_symbol(sym), st);
            if (resolved_ns && resolved_ns != sym) {
                return resolved_ns;
            }
        }
        return NULL;
    }

    // Search env_stack (for let bindings, closure captures)
    CljList *current_stack = env_stack;
    while (current_stack && list_type_matches(TAG(current_stack))) {
        ID env_obj_id = LIST_FIRST(current_stack);
        if (env_obj_id && TAG(env_obj_id) == CLJ_MAP) {
            CljMap *env = (CljMap*)env_obj_id;
            ID resolved = map_get(env, sym, NOT_FOUND);
            if (resolved != NOT_FOUND) {
                return resolved;
            }
        }

        ID rest_obj_id = LIST_REST(current_stack);
        if (rest_obj_id && list_type_matches(TAG(rest_obj_id))) {
            current_stack = (CljList*)rest_obj_id;
        } else {
            break;
        }
    }
    
    if (fallback_env) {
        ID resolved = map_get(fallback_env, sym, NOT_FOUND);
        if (resolved != NOT_FOUND) {
            return resolved;
        }
    }

    // Fallback to namespace
    if (st) {
        ID resolved_ns = eval_symbol(as_symbol(sym), st);
        if (resolved_ns && resolved_ns != sym) {
            return resolved_ns;
        }
    }

    return NULL;
}

// Convert a CallFrame chain into a heap-based env_stack for closures
static CljList* frame_chain_to_env_stack(CallFrame *frame, CljList *parent_stack) {
    if (!frame) {
        return parent_stack ? RETAIN(parent_stack) : NULL;
    }

    CljList *parent_with_frames = frame_chain_to_env_stack(frame->parent, parent_stack);

    int initial_capacity = frame->param_count > 0 ? frame->param_count : 4;
    CljMap *frame_map = make_map(initial_capacity);

    if (frame->params) {
        for (int i = 0; i < frame->param_count; i++) {
            ID key = frame->params[i];
            if (!key) continue;

            ID value = frame_decode_value(frame->values[i]);
            CljMap *new_map = map_assoc(frame_map, key, value);
            ASSIGN(frame_map, new_map);
        }
    }

    CljList *new_stack = make_list(frame_map, parent_with_frames);
    RELEASE(frame_map);
    if (parent_with_frames) {
        RELEASE(parent_with_frames);
    }
    return new_stack;
}

// Helper: Add namespace mappings to environment

// Evaluate body with environment lookup (for loops)
ID eval_body_with_env(ID body, CljMap *env, EvalState *st) {
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(body != NULL);

    // Check if body is an immediate value
    if (IS_IMMEDIATE(body)) {
        return body;
    }

    CljObject *body_obj = (CljObject*)body;
    switch (body_obj->type) {
        case CLJ_SYMBOL: {
            // Look up symbol in environment
            return map_get((CljMap*)env, body, NULL);
        }

        case CLJ_LIST:
        case CLJ_AST_NODE: {
            // Type check before calling
            if (!body_obj || !list_type_matches(TAG(body_obj))) return NULL;
            CljList *list_data = as_list(body);
            return eval_list(list_data, env, st, NULL);
        }

        default:
            // Literal value
            return AUTORELEASE(RETAIN(body));
    }
}

// Simplified body evaluation with parameter binding
ID eval_body_with_params(ID body, const EvalContext *ctx) {
    // Handle nil body gracefully (represents Clojure nil)
    if (!body) {
        return NULL;
    }

    if (ctx && ctx->param_count > 0) {
        assert(ctx->params != NULL);
        assert(ctx->param_values != NULL);
    }

    if (body && TAG(body) == CLJ_SYMBOL) {
        // Resolve symbol
        // This avoids expensive map copying in env_extend_stack for recursive calls
        // Parameters are typically accessed most frequently, so checking them first is optimal
        CljSymbol *body_sym = as_symbol(body);
        CLJ_ASSERT(body_sym != NULL && "TAG(body)==CLJ_SYMBOL but as_symbol returned NULL");

        // This avoids expensive closure_env map lookups for parameter access
        if (ctx && ctx->param_count > 0 && ctx->params && ctx->param_values) {
            // Iterate through parameter arrays to find matching symbol
            for (int i = 0; i < ctx->param_count; i++) {
                if (ctx->params[i] == body) {
                    // Found matching parameter - return its value
                    ID param_value = ctx->param_values[i];
                    // param_value is already retained - object survives until pool-pop
                    return param_value;
                }
            }
        }

        // CRITICAL: Check if symbol is a keyword FIRST - keywords evaluate to themselves
        // This must come BEFORE symbol resolution attempts
        if (IS_KEYWORD(body)) {
            return body;
        }

        // Use central symbol resolution function (DRY: handles environment stack and frames)
        if (ctx) {
            CljMap *ctx_env_map = ctx->env_stack ? env_stack_head(ctx->env_stack) : NULL;
            ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, ctx_env_map, ctx->frame, body, get_eval_state(ctx, NULL));
            const char *log_sym = (body_sym && body_sym->cname) ? body_sym->cname : "<anon>";
            if (resolved_id) {
                if (resolved_id == FRAME_NIL_SENTINEL) {
                    return NULL;
                }
                // CRITICAL: If resolved_id is still a symbol (not a value), throw exception
                // This prevents infinite loops where a symbol resolves to itself
                if (!IS_IMMEDIATE(resolved_id) &&
                    TAG(resolved_id) == CLJ_SYMBOL &&
                    !IS_KEYWORD(resolved_id)) {
                    bool resolves_to_self = (resolved_id == body);
                    if (!resolves_to_self && resolved_id && body) {
                        bool structural_equal = clj_equal(resolved_id, body);
                        resolves_to_self = structural_equal;
                    }
                    if (resolves_to_self) {
                        const char *sym_name = log_sym;
                        throw_unresolved_symbol_exception(sym_name);
                        return NULL;
                    }
                }
                ID ret_val = AUTORELEASE(RETAIN(resolved_id));
                return ret_val;
            }
        }
        // If still not found, try namespace lookup (for recursive function calls)
        // ns_resolve takes CljObject* (only objects, not immediates) and returns ID
        // body is a symbol (CljObject*), so we can pass it directly
        EvalState *ctx_state_after_env = get_eval_state(ctx, NULL);
        if (ctx_state_after_env) {
            ID resolved_id = ns_resolve(ctx_state_after_env, as_symbol(body));
            if (resolved_id) {
                // This can happen if a symbol is stored in namespace instead of its value
                // In this case, we should throw an exception instead of returning the symbol
                if (!IS_IMMEDIATE(resolved_id) && TAG(resolved_id) == CLJ_SYMBOL) {
                    // Symbol found in namespace but value is also a symbol - this is an error
                    CljSymbol *sym = as_symbol(body);
                    const char *sym_name = sym && sym->cname ? sym->cname : "unknown";
                    throw_unresolved_symbol_exception(sym_name);
                    return NULL;
                }
                // ns_resolve returns retained values - object survives until pool-pop
                return resolved_id;
            }
        }
        // Special case: nil should evaluate to NULL (not SYM_NIL)
        if (body == SYM_NIL) {
            // nil is represented as NULL - return NULL directly
            return NULL;
        }
        // Symbol not found - throw exception
        CljSymbol *sym_obj = as_symbol(body);
        const char *sym_name_final = sym_obj && sym_obj->cname ? sym_obj->cname : "unknown";
        throw_unresolved_symbol_exception(sym_name_final);
        return NULL;
    }

    // body is guaranteed non-NULL beyond this point

    // Check if body is an immediate value first
    // This must come BEFORE the pointer validation check, because immediate values
    // have small numeric values (e.g., 1 = 0x9) that would fail the pointer check
    if (IS_IMMEDIATE(body)) {
        // Immediate values don't need retain/release
        // CRITICAL: Return body directly as ID (void*), not as CljObject*
        // This ensures fixnum literals are returned correctly
        return body;
    }

    // Check if body is a valid pointer (not pointing to invalid memory)
    // NOTE: This check must come AFTER IS_IMMEDIATE, because immediate values
    // have small numeric values that would fail this check
    if ((uintptr_t)body < 0x1000 && !IS_IMMEDIATE(body)) {
        return NULL;
    }

    // For lists, evaluate them with parameter substitution
    // CRITICAL: Before casting to CljObject*, check if body is an immediate value
    // This prevents undefined behavior when accessing body_obj->type for fixnum literals
    if (IS_IMMEDIATE(body)) {
        // This should have been caught earlier, but as a safety check, return body directly
        return body;
    }

    CljObject *body_obj = (CljObject*)body;
    switch (body_obj->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE: {
            // Evaluate list with context (ctx preserves recur)
            CljMap *env_map = get_closure_env(ctx);
            EvalState *ctx_state = get_eval_state(ctx, NULL);
            // OPTIMIZATION: Use thread-local EvalState instead of creating temporary
            if (!ctx_state) ctx_state = builtin_get_eval_state();
            return eval_list(as_list(body), env_map, ctx_state, ctx);
        }

        default:
            // Literal value
            return RETAIN(body);
    }
}

// Simplified body evaluation (basic implementation)
/** @brief Evaluate function body expressions */
ID eval_body(ID body, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(body != NULL);

    // CRITICAL: If EvalContext is provided, use eval_body_with_params to preserve RecurContext
    // eval_body_with_params can handle ctx->params == NULL
    if (ctx) {
        return eval_body_with_params(body, ctx);
    }

    // Handle immediate values (fixnums, chars, booleans, nil)
    if (IS_IMMEDIATE(body)) {
        return body; // Immediate values evaluate to themselves
    }

    // Simplified implementation - would normally evaluate the AST
    switch (((CljObject*)body)->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE: {
            // Evaluate list
            // CRITICAL: Pass ctx to preserve RecurContext
            return eval_list(as_list(body), env, st, ctx);
        }

        case CLJ_SYMBOL: {
            // Check if symbol is a keyword - keywords evaluate to themselves
            // CRITICAL: This must come BEFORE symbol resolution attempts
            if (IS_KEYWORD(body)) {
                return body;
            }

            // Special case: nil should evaluate to NULL (not SYM_NIL)
            if (body == SYM_NIL) {
                return NULL; // nil evaluates to NULL
            }

            if (ctx) {
                // Use st from context if available, otherwise fall back to parameter
                EvalState *eval_st = get_eval_state(ctx, st);
                CljMap *fallback_env = ctx->env_stack ? env_stack_head(ctx->env_stack) : NULL;
                ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, fallback_env, ctx->frame, body, eval_st);
                if (resolved_id) {
                    if (resolved_id == FRAME_NIL_SENTINEL) {
                        return NULL;
                    }
                    // resolve_symbol_in_env returns values from map_get (retained) or eval_symbol (AUTORELEASE)
                    // eval_body should return AUTORELEASE objects
                    // RETAIN and AUTORELEASE macros handle immediate values safely
                    return AUTORELEASE(RETAIN(resolved_id));
                }
            }

            // Resolve symbol - first try local environment, then namespace
            // Note: We need to check if key exists, not just if value is non-NULL,
            // because nil (NULL) is a valid value
            if (env && TAG(env) == CLJ_MAP) {
                // Use sentinel to distinguish "key not found" from "value is nil"
                ID result_id = map_get((CljMap*)env, body, NOT_FOUND);
                if (result_id != NOT_FOUND) {
                    return (CljObject*)result_id;
                }
            }

            // If not found in local environment, try namespace
            if (st && st->current_ns && st->current_ns->mappings) {
                // Use sentinel to distinguish "key not found" from "value is nil"
                ID result_id = map_get(st->current_ns->mappings, body, NOT_FOUND);
                if (result_id != NOT_FOUND) {
                    return (CljObject*)result_id;
                }
            }

            // If still not found, try global symbol resolution (includes clojure.core)
            // This is important for built-in functions like inc, dec, etc.
            if (st) {
                ID resolved = eval_symbol(as_symbol(body), st);
                if (resolved) {
                    // Special case: nil should evaluate to NULL (not SYM_NIL)
                    if (resolved == SYM_NIL) {
                        return NULL; // nil evaluates to NULL
                    }
                    // eval_symbol returns AUTORELEASE - object survives until pool-pop
                    return resolved;
                }
            }

            // Symbol not found - this should throw an exception
            throw_exception(EXCEPTION_RUNTIME, "Unable to resolve symbol in this context",
                           __FILE__, __LINE__, 0);
            return NULL;
        }

        case CLJ_MAP: {
            // Map literals need to have their keys and values evaluated
            // This is necessary for cases like {nil "value"} where nil should be evaluated to NULL
            CljMap *map = (CljMap*)body;
            CljMap *result = map_empty();
            RETAIN(result);

            MAP_FOR_EACH(map, key, value) {
                // Cache tags for performance
                int key_tag = key ? TAG(key) : 0;
                int value_tag = value ? TAG(value) : 0;

                // Evaluate key and value (nil should evaluate to NULL)
                // Check for SYM_NIL before calling eval_body to avoid symbol resolution
                ID eval_key = NULL;
                if (key && key_tag == CLJ_SYMBOL && (CljObject*)key == (CljObject*)SYM_NIL) {
                    eval_key = NULL;  // nil evaluates to NULL
                } else if (key) {
                    eval_key = eval_body(key, env, st, ctx);
                }

                ID eval_value = NULL;
                if (value && value_tag == CLJ_SYMBOL && (CljObject*)value == (CljObject*)SYM_NIL) {
                    eval_value = NULL;  // nil evaluates to NULL
                } else if (value) {
                    eval_value = eval_body(value, env, st, ctx);
                }

                // Add evaluated key-value pair to result map
                CljMap *new_result = map_assoc(result, eval_key, eval_value);
                if (new_result != result) {
                    RELEASE(result);
                    result = new_result;
                    RETAIN(result);
                }

                // Release evaluated key and value if they were retained
                if (eval_key && eval_key != key) RELEASE(eval_key);
                if (eval_value && eval_value != value) RELEASE(eval_value);
            }

            return AUTORELEASE(result);
        }

        default:
            // Literal value
            return AUTORELEASE(RETAIN(body));
    }
}

// Helper functions for eval_list optimization
static ID call_function_with_args_and_context(ID fn, CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
static ID resolve_list_operator(ID op, CljMap *env, EvalState *st, const EvalContext *ctx, CljASTNode *call_node);
static ID eval_function_call_from_list(CljList *list, CljMap *env, EvalState *st, ID op, const EvalContext *ctx);

// Thread-local recursion depth tracking for eval_arg and eval_list
static _Thread_local int g_eval_arg_depth = 0;

// Reset eval arg depth (for test isolation)
void reset_eval_arg_depth(void) {
    g_eval_arg_depth = 0;
}

// ============================================================================
// Helper functions for eval_list refactoring
// ============================================================================

// Handle recur special form
// Resolve operator symbol from environment or namespace
// DRY: Uses central resolve_symbol_in_env function
static INLINE ID resolve_list_operator(ID op, CljMap *env, EvalState *st, const EvalContext *ctx, CljASTNode *call_node) {
    if (!op || TAG(op) != CLJ_SYMBOL) {
        return op;
    }

    CljSymbol *op_sym = as_symbol(op);

    EvalContext local_ctx;
    CljList *owned_env_stack = NULL;
    const EvalContext *effective_ctx = ensure_eval_context(env, st, ctx, &local_ctx, &owned_env_stack);
    ID resolved = NULL;
    CljMap *closure_env = effective_ctx ? get_closure_env(effective_ctx) : NULL;
    EvalState *ctx_st = get_eval_state(effective_ctx, st);
    CljMap *resolve_env = closure_env ? closure_env : env;
    CljList *resolve_stack = effective_ctx ? effective_ctx->env_stack : NULL;
    
    // Clear env_stack unless it has actual closure captures or different env
    if (resolve_stack) {
        ID stack_rest = LIST_REST(resolve_stack);
        ID stack_head = LIST_FIRST(resolve_stack);
        bool has_captures = stack_rest && list_type_matches(TAG(stack_rest));
        bool has_different_env = stack_head && stack_head != resolve_env;
        if (!has_captures && !has_different_env) {
            resolve_stack = NULL;
        }
    }
    
    // CRITICAL: Check call frame FIRST (before environment/namespace lookup)
    // This ensures Clojure shadowing semantics: parameters shadow environment/namespace bindings
    // NEW: Use stack-based frames for zero-allocation parameter lookup
    if (effective_ctx && effective_ctx->frame) {
        ID frame_value = NULL;
        if (frame_lookup(effective_ctx->frame, op, &frame_value)) {
            RELEASE(owned_env_stack);
            return frame_value;
        }
    }
    
    // LEGACY: Fallback to parameter array lookup (for compatibility)
    // Parameter lookup (symbols are interned, pointer comparison suffices)
    if (effective_ctx && effective_ctx->param_count > 0 &&
        effective_ctx->params && effective_ctx->param_values) {
        for (int i = 0; i < effective_ctx->param_count; i++) {
            if (effective_ctx->params[i] == op) {
                RELEASE(owned_env_stack);
                return effective_ctx->param_values[i];
            }
        }
    }
    
    // Parameter validation: Check st and ctx before accessing ctx->env
    if (!ctx_st || !effective_ctx) {
        // Fallback to namespace lookup if context not available
        resolved = eval_symbol(op_sym, ctx_st);
        ID return_value = resolved ? resolved : op;
        RELEASE(owned_env_stack);
        return return_value;
    }

    CljSymbol *cache_ns_key = resolve_cache_ns_key(op_sym, ctx_st);
    bool allow_callsite_cache = call_node && op_sym && !resolve_stack && g_runtime.resolve_cache_epoch != 0;
    if (allow_callsite_cache) {
        ID cached_call = ast_node_get_cached_resolution(call_node, op_sym, g_runtime.resolve_cache_epoch);
        if (cached_call) {
            RELEASE(owned_env_stack);
            return cached_call;
        }
    }
    
    // OPTIMIZATION: Check resolve_cache FIRST for namespace symbols (before environment lookup)
    // Symbols from namespace are stable (syntactic scope) and can be safely cached
    // This allows recursive calls to benefit from cache hits even when symbol is in environment
    if (g_runtime.resolve_cache) {
        ID cached = resolve_cache_lookup_value(cache_ns_key, op);
        if (cached) {
            // Populate per-callsite cache even when global resolve_cache hits.
            // This is important for hot recursive callsites (e.g. fib), where the resolve_cache
            // is often warmed up by an earlier top-level call, and the first encounter of an
            // inner callsite would otherwise return early and never initialize callsite_cache.
            if (allow_callsite_cache) {
                ast_node_update_callsite_cache(call_node, op_sym, cached, g_runtime.resolve_cache_epoch);
            }
            RELEASE(owned_env_stack);
            return cached;
        }
    }

    // OPTIMIZATION: Qualified symbols skip env_stack - go direct to namespace
    bool is_qualified = op_sym && op_sym->ns_name;
    
    // Check frame first (parameters)
    if (!is_qualified && effective_ctx && effective_ctx->frame) {
        ID frame_value = NULL;
        if (frame_lookup(effective_ctx->frame, op, &frame_value)) {
            resolved = frame_value;
        }
    }
    
    // Check env_stack only if not found in frame and stack exists
    if (!resolved && !is_qualified && resolve_stack) {
        CljList *current = resolve_stack;
        while (current && list_type_matches(TAG(current))) {
            ID env_obj = LIST_FIRST(current);
            if (env_obj && TAG(env_obj) == CLJ_MAP) {
                ID found = map_get((CljMap*)env_obj, op, NOT_FOUND);
                if (found != NOT_FOUND) {
                    resolved = found;
                    break;
                }
            }
            ID rest = LIST_REST(current);
            current = (rest && list_type_matches(TAG(rest))) ? (CljList*)rest : NULL;
        }
    }

    if (resolved) {
        RELEASE(owned_env_stack);
        return resolved;
    }

    // Namespace lookup - this result can be cached
    resolved = eval_symbol(op_sym, ctx_st);
    
    // Cache namespace lookups for future calls
    if (resolved && ctx_st && ctx_st->current_ns && TAG(resolved) != CLJ_SYMBOL) {
        if (!g_runtime.resolve_cache) {
            ASSIGN(g_runtime.resolve_cache, make_map(RESOLVE_CACHE_SIZE));
        }
        if (g_runtime.resolve_cache) {
            resolve_cache_store_value(cache_ns_key, op, resolved);
            if (call_node && !resolve_stack) {
                ast_node_update_callsite_cache(call_node, op_sym, resolved, g_runtime.resolve_cache_epoch);
            }
        }
    }
    
    RELEASE(owned_env_stack);
    return resolved ? resolved : op;
}

// Handle function call from resolved operator
static INLINE ID eval_function_call_from_list(CljList *list, CljMap *env, EvalState *st, ID op, const EvalContext *ctx) {
    if (!op) return NULL;

    // Handle keywords as functions (for map lookup)
    if (TAG(op) == CLJ_SYMBOL && IS_KEYWORD(op)) {
        int total_count = list_count(list);
        int argc = total_count - 1;
        if (argc == 1) {
            ID arg = eval_arg_with_context(list, 1, env, st, ctx);
            if (arg && TAG(arg) == CLJ_SYMBOL) {
                ID resolved = eval_symbol(as_symbol((CljObject*)arg), st);
                if (resolved) {
                    RELEASE(arg);
                    arg = resolved;
                }
            }
            if (arg && TAG(arg) == CLJ_MAP) {
                ID result = map_get((CljValue)arg, (CljValue)op, NULL);
                RELEASE(arg);
                return RETAIN(result);
            }
            RELEASE(arg);
        }
    }

    // Resolve symbol to get function
    unsigned char op_tag = op ? TAG(op) : 0;
    if (op_tag == CLJ_SYMBOL) {
        CljObject *fn = eval_symbol(as_symbol(op), st);
        if (!fn) return NULL;

        unsigned char fn_tag = TAG(fn);
        if (fn_tag == CLJ_MAP) {
            return eval_map_lookup(list, env, st, ctx, fn);
        }

        if (fn_tag == CLJ_FUNC || fn_tag == CLJ_CLOSURE) {
            if (g_eval_arg_depth >= MAX_CALL_STACK_DEPTH) {
                throw_exception(EXCEPTION_STACK_OVERFLOW,
                              "Maximum evaluation depth exceeded in nested function calls",
                              __FILE__, __LINE__, 0);
                return NULL;
            }
            g_eval_arg_depth++;
            ID result = call_function_with_args_and_context(fn, list, env, st, ctx);
            g_eval_arg_depth--;
            return result;
            // Exception propagates automatically - no cleanup needed!
        }

        if (fn_tag == CLJ_LIST) {
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call list as a function");
        }

        return AUTORELEASE(RETAIN(fn));
    }

    // Direct function call
    if (op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE) {
        return call_function_with_args_and_context(op, list, env, st, ctx);
    }

    return NULL; // Not a function
}

static INLINE ID call_function_with_args_and_context(ID fn, CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    ID args[16];
    int argc = 0;
    unsigned char fn_tag = TAG(fn);

    // Single-pass: traverse list once, evaluate args and count simultaneously
    if (env && TAG(env) == CLJ_MAP) {
        CljList *current = list ? as_list(list->rest) : NULL;
        while (current && argc < 16) {
            args[argc++] = eval_arg_from_expr_with_context(current->first, env, st, ctx);
            current = current->rest ? as_list(current->rest) : NULL;
        }
    }

    CljNamespace *saved_ns = st ? st->current_ns : NULL;
    CljNamespace *target_ns = NULL;
    if (fn_tag == CLJ_CLOSURE) {
        CljFunction *closure_fn = (CljFunction*)fn;
        target_ns = closure_fn->ns;
    }

    bool switched_ns = false;
    if (st && target_ns && st->current_ns != target_ns) {
        st->current_ns = target_ns;
        switched_ns = true;
    }

    // Call function - no TRY/CATCH needed, exception cleanup happens in outer handler
    ID result = eval_function_call(fn, args, argc, env, st);

    // Restore namespace after successful call (st guaranteed non-NULL if switched_ns)
    if (switched_ns) {
        st->current_ns = saved_ns;
    }

    if (result == SYM_NIL) {
        return NULL;
    }
    return AUTORELEASE(result);
}

// List evaluation (optionally accepts EvalContext for recur support)
ID eval_list(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (!list) {
        return NULL;
    }

    CljASTNode *call_node = is_ast_node(list) ? (CljASTNode*)list : NULL;

    // Use EvalState from context if provided
    EvalState *effective_st = ctx ? get_eval_state(ctx, st) : st;

    // Prefer the current head of env_stack (closures/let frames), fall back to env parameter
    CljMap *effective_env = ctx ? get_closure_env(ctx) : NULL;
    if (!effective_env) effective_env = env;

    ID head = LIST_FIRST(list);

    // First element is the operator
    CljObject *op = head;

    // If first element is a list, evaluate it first (for nested calls like ((array-map)))
    // CRITICAL: Pass ctx to preserve RecurContext
    if (op && list_type_matches(TAG(op))) {
        op = eval_list(as_list(op), effective_env, effective_st, ctx);
        if (!op) {
            return NULL;
        }
        // Now op is the result of evaluating the inner list - continue with it
    }

    // Handle maps as functions (for key lookup) - must be first
    if (op && TAG(op) == CLJ_MAP) {
        return eval_map_lookup(list, effective_env, effective_st, ctx, op);
    }

    // Check if op is a symbol and resolve it
    CljObject *original_op = op;
    unsigned char original_op_tag = op ? TAG(op) : 0;
    CljSymbol *original_op_sym = (original_op_tag == CLJ_SYMBOL) ? as_symbol(op) : NULL;

    // Handle def and ns before symbol resolution
    if (original_op_sym == SYM_DEF) return eval_def(list, effective_env, effective_st);
    if (original_op_sym == SYM_NS) return eval_ns(list, effective_env, effective_st);

    // Fast-path: Comparison operators (avoid symbol resolution for <, >, <=, >=, =)
    if (original_op_sym && (original_op_sym->base.flags & CLJ_FLAG_COMPARISON)) {
        CljObject *comparison_result = eval_comparison_dispatch(list, effective_env, effective_st, ctx, original_op);
        if (comparison_result) return comparison_result;
    }

    // Fast-path: Arithmetic operators - O(1) dispatch via flags
    if (original_op_sym && (original_op_sym->base.flags & CLJ_FLAG_ARITHMETIC)) {
        ArithOp op = (original_op_sym->base.flags >> CLJ_ARITH_OP_SHIFT) & 0x03;
        return eval_arithmetic_generic_with_context(list, effective_env, op, effective_st, ctx);
    }

    // Special forms (avoid symbol resolution for if/let/do/recur/etc.)
    // O(1) dispatch via function pointer (replaces O(n) if-chain)
    if (original_op_sym && (original_op_sym->base.flags & CLJ_FLAG_SPECIAL)) {
        CljSpecialSymbol *special = (CljSpecialSymbol*)original_op_sym;
        if (special->eval_fn) {
            // NOTE: NULL is a valid result for many special forms (e.g., (when false ...))
            // Cast function pointer to correct type for call
            SpecialFormEvalFn fn = (SpecialFormEvalFn)special->eval_fn;
            return fn(list, effective_env, effective_st, ctx);
        }
    }

    // Resolve operator symbol
    // CRITICAL: Pass ctx to allow environment chaining lookup (for functions defined in let)
    ID resolved_op = resolve_list_operator(op, effective_env, effective_st, ctx, call_node);
    
    op = resolved_op;

    // After resolution: check if resolved to arithmetic symbol (e.g., clojure.core/+ → SYM_PLUS)
    if (op && TAG(op) == CLJ_SYMBOL) {
        CljSymbol *resolved_sym = (CljSymbol*)op;
        if (resolved_sym->base.flags & CLJ_FLAG_ARITHMETIC) {
            ArithOp arith_op = (resolved_sym->base.flags >> CLJ_ARITH_OP_SHIFT) & 0x03;
            return eval_arithmetic_generic_with_context(list, effective_env, arith_op, effective_st, ctx);
        }
    }

    // Tier 3: Sequence operations (inline dispatch)
    // Note: Only return if result is non-NULL, otherwise continue to try other operations
    if (original_op_sym == SYM_FIRST) { CljObject *r = eval_and_call_native_with_context(list, effective_env, native_first, 1, ctx); if (r) return r; }
    if (original_op_sym == SYM_REST)  { CljObject *r = eval_and_call_native_with_context(list, effective_env, native_rest, 1, ctx); if (r) return r; }
    if (original_op_sym == SYM_CONS)  { CljObject *r = eval_and_call_native_with_context(list, effective_env, native_cons, 2, ctx); if (r) return r; }
    if (original_op_sym == SYM_SEQ)   { CljObject *r = eval_seq(list, effective_env); if (r) return r; }
    if (original_op_sym == SYM_NEXT)  { CljObject *r = eval_and_call_native_with_context(list, effective_env, native_next, 1, ctx); if (r) return r; }
    if (original_op_sym == SYM_COUNT) { CljObject *r = eval_and_call_native_with_context(list, effective_env, native_count, 1, ctx); if (r) return r; }

    // Tier 4: String and I/O operations
    if (original_op_sym == SYM_STR) {
        // Count arguments by traversing once
        int argc = 0;
        LIST_FOR_EACH(LIST_REST(list), elem) {
            (void)elem;  // unused in count phase
            if (argc < 16) argc++;
        }

        ID args_stack[16];
        ID *args = alloc_obj_array(argc, args_stack);
        if (!args) return NULL;

        // Traverse and evaluate arguments in one pass (O(n) instead of O(n²))
        int i = 0;
        LIST_FOR_EACH(LIST_REST(list), elem) {
            if (i >= argc) break;
            args[i] = eval_arg_from_expr_with_context(elem, effective_env, effective_st, ctx);
            if (!args[i]) {
                free_obj_array(args, args_stack);
                return NULL;
            }
            i++;
        }

        ID str_result = native_str(args, argc);
        free_obj_array(args, args_stack);
        return str_result;
    }

    // Tier 6: Loop operations (for, doseq, dotimes) - inline dispatch
    if (original_op_sym == SYM_FOR) {
        return AUTORELEASE(eval_for(list, effective_env));
    }
    if (original_op_sym == SYM_DOSEQ) {
        return AUTORELEASE(eval_doseq(list, effective_env));
    }
    if (original_op_sym == SYM_DOTIMES) {
        // OPTIMIZATION: Use thread-local EvalState instead of creating temporary
        EvalState *eval_st = effective_st ? effective_st : builtin_get_eval_state();
        return AUTORELEASE(eval_dotimes(list, effective_env, eval_st));
    }

    // Try function call
    // CRITICAL: Only try to call if op is a symbol or function
    // If op is not a symbol or function, eval_function_call_from_list will return NULL
    // and we should treat it as an error (not a function call)
    unsigned char op_tag = op ? TAG(op) : 0;
    if (op && (op_tag == CLJ_SYMBOL || op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE)) {
        return eval_function_call_from_list(list, effective_env, effective_st, op, ctx);
    }

    // Error: first element is not a function
    if (IS_IMMEDIATE(op)) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call %s as a function", clj_type_name(op->type));
    }

    // Error: op is a list (should have been evaluated earlier)
    if (op && list_type_matches(TAG(op))) {
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call list as a function");
    }

    // Error: first element is not a function and not a symbol
    return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
            "Cannot call %s as a function", clj_type_name(op->type));
}

ID eval_def(CljList *list, CljMap *env, EvalState *st) {
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(is_list(list));

    // Get the symbol name (second argument) - don't evaluate it, just get the symbol
    CljObject *symbol = list_get_element(list, 1);
    if (!symbol || TAG(symbol) != CLJ_SYMBOL) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "def requires a symbol as first argument",
                       NULL, 0, 0);
        return NULL;
    }

    // Get the value (third argument) - evaluate this
    // Note: value_expr can be NULL if nil was parsed (nil is represented as NULL)
    CljObject *value_expr = list_get_element(list, 2);
    // Check if list has at least 3 elements (def, symbol, value)
    // If value_expr is NULL, it might be nil (valid) or missing (invalid)
    // We need to check if there are actually 3 elements in the list
    int list_len = list_count(list);
    if (list_len < 3) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "def requires a value expression as second argument",
                       NULL, 0, 0);
        return NULL;
    }
    // If list_len >= 3 but value_expr is NULL, it's nil (valid case)

    // Evaluate the value expression
    // Use current namespace mappings as environment for evaluation
    // This ensures that builtin functions like + are available during evaluation
    CljMap *eval_env = (st && st->current_ns) ? st->current_ns->mappings : env;
    CljObject *value = NULL;
    if (value_expr) {
        if (value_expr && list_type_matches(TAG(value_expr))) {
            value = eval_list(as_list(value_expr), eval_env, st, NULL);
        } else {
            value = eval_parsed(value_expr, st, eval_env);
        }
    }
    // If value_expr is NULL, value remains NULL (nil case)
    // value can be NULL if nil was evaluated (legitimate case)
    // If evaluation failed, eval_list/eval_parsed should have thrown an exception

    // If the value is a function, set its name
    // CRITICAL: Only set name for CLJ_CLOSURE (Clojure functions), not CLJ_FUNC (native functions)
    // as_function only works with CLJ_CLOSURE, not CLJ_FUNC
    if (value && TAG(value) == CLJ_CLOSURE) {
        CljFunction *func = as_function(value);
        CljSymbol *sym = as_symbol(symbol);
        if (func && sym && sym->cname[0] && !func->name) {
            func->name = strdup(sym->cname);
        }
    }

    // Store the symbol-value binding in the environment
    if (!st) {
        throw_exception(EXCEPTION_RUNTIME,
                       "def requires an evaluation state",
                       NULL, 0, 0);
        return NULL;
    }

    if (!st->current_ns) {
        throw_exception(EXCEPTION_RUNTIME,
                       "def requires a current namespace",
                       NULL, 0, 0);
        return NULL;
    }

    // Store in namespace (value can be NULL/nil - legitimate case)
    ns_define(st->current_ns, symbol, value);
    CljSymbol *return_symbol = as_symbol(symbol);

    // Apply metadata to value
    // In Clojure, metadata from ^#^{...} (def ...) is applied to the value
#ifdef ENABLE_META
    // Try to get metadata from the def form (list object)
    ID form_meta = meta_get((CljObject*)list);
    if (form_meta && value) {
        // Metadata found on the form - apply it to the value
        meta_set((CljObject*)value, (CljObject*)form_meta);
    }
#endif // ENABLE_META

    // Return the symbol that was actually stored (Clojure-compatible: def returns the var/symbol, not the value)
    return return_symbol;
}

ID eval_ns(CljList *list, CljMap *env, EvalState *st) {
    CLJ_ASSERT(env != NULL);
    (void)env;
    CLJ_ASSERT(list != NULL);
    assert(st != NULL);

    // Get namespace name (first argument) - use list_get_element like eval_def
    CljObject *ns_name_obj = list_get_element(list, 1);
    if (!ns_name_obj || TAG(ns_name_obj) != CLJ_SYMBOL) {
        eval_error("ns expects a symbol", st);
        return NULL;
    }

    CljSymbol *ns_sym = as_symbol(ns_name_obj);
    if (!ns_sym || !ns_sym->cname[0]) {
        eval_error("ns symbol has no name", st);
        return NULL;
    }

    // Switch to namespace (creates if not exists)
    evalstate_set_ns(st, ns_sym->cname);

    // Process :require clauses: (ns name (:require [ns :as alias]))
    int list_len = list_count(list);
    for (int i = 2; i < list_len; i++) {
        CljObject *clause = list_get_element(list, i);
        if (!clause || !list_type_matches(TAG(clause))) continue;

        CljList *clause_list = as_list(clause);
        if (!clause_list) continue;

        ID first = LIST_FIRST(clause_list);
        if (!first || TAG(first) != CLJ_SYMBOL) continue;

        CljSymbol *clause_sym = as_symbol((CljObject*)first);
        if (!clause_sym || !clause_sym->cname) continue;

        // Check if this is a :require clause
        if (clause_sym->cname[0] == ':' && strcmp(clause_sym->cname, ":require") == 0) {
            // Process require specs: (:require [ns :as alias] [ns2 :as alias2])
            int clause_len = list_count(clause_list);
            for (int j = 1; j < clause_len; j++) {
                CljObject *spec = list_get_element(clause_list, j);
                if (!spec) continue;

                // Process require spec using native_require
                // This ensures consistent behavior whether namespace exists or not
                // native_require will handle both loading and alias setting
#ifndef ESP32_BUILD
                // Set g_current_eval_state so native_require can use it
                extern void builtin_set_eval_state(EvalState *st);
                builtin_set_eval_state(st);
                ID spec_id = spec;
                ID args[1] = { spec_id };
                (void)native_require(args, 1);
                builtin_set_eval_state(NULL); // Clear after call
#endif
            }
        }
    }

    return NULL;
}

ID eval_var(CljList *list, CljMap *env, EvalState *st) {
    (void)env;  // Not used

    // Get symbol name (first argument)
    CljObject *sym_obj = list_get_element(list, 1);
    if (!sym_obj || TAG(sym_obj) != CLJ_SYMBOL) {
        eval_error("var expects a symbol", st);
        return NULL;
    }

    CljSymbol *sym = as_symbol(sym_obj);
    if (!sym || !sym->cname[0]) {
        eval_error("var symbol has no name", st);
        return NULL;
    }

    // Look up the symbol in the current namespace
    ID value = ns_resolve(st, sym);
    if (!value) {
        // Try to find the symbol in the current namespace mappings
        CljMap *mappings = st->current_ns->mappings;
        if (mappings) {
            value = map_get((CljValue)mappings, sym_obj, NULL);
        }
    }

    if (!value) {
        eval_error("var: symbol not found", st);
        return NULL;
    }

    // Return the value (in Clojure, var returns the actual value, not a var object)
    return value;
}

// ============================================================================
// AST Transformation Functions for TCO
// ============================================================================
// TCO functions moved to optimize.c

ID eval_fn(CljList *list, CljMap *env, EvalState *st) {
    return eval_fn_with_context(list, env, st, NULL);
}

ID eval_fn_with_context(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(is_list(list));

    // Get the parameter list (second argument) - don't evaluate it
    CljObject *params_list = list_get_element(list, 1);
    // Parameters can be a vector [a b] or a list (a b)
    if (!params_list || (!list_type_matches(TAG(params_list)) && TAG(params_list) != CLJ_VECTOR)) {
        return NULL;
    }

    // Get the body (third argument) - don't evaluate it
    CljObject *body = list_get_element(list, 2);
    if (!body) {
        return NULL;
    }

    // Convert parameter list/vector to array
    int param_count = 0;
    if (params_list && TAG(params_list) == CLJ_VECTOR) {
        CljVector *vec = as_vector(params_list);
        param_count = vec ? vector_count(vec) : 0;
    } else {
        param_count = list_count(as_list(params_list));
    }

    ID params_stack[16];
    ID *params = alloc_obj_array(param_count, params_stack);

    for (int i = 0; i < param_count; i++) {
        if (params_list && TAG(params_list) == CLJ_VECTOR) {
            CljVector *vec = as_vector(params_list);
            params[i] = vector_nth(vec, i);
        } else {
            params[i] = list_get_element(as_list(params_list), i);
        }
        if (!params[i] || TAG(params[i]) != CLJ_SYMBOL) {
            // Invalid parameter
            free_obj_array(params, params_stack);
            return NULL;
        }
    }

    // CRITICAL: Use env_stack from context if available (for closures)
    // This ensures that nested functions can access outer function parameters
    CljList *fn_env_stack = NULL;
    if (ctx && (ctx->frame || ctx->env_stack)) {
        fn_env_stack = frame_chain_to_env_stack(ctx->frame, ctx->env_stack);
    } else {
        // Fallback: Create env_stack from env if provided, otherwise use namespace mappings
        fn_env_stack = env ? make_list(env, NULL) : NULL;
        if (!fn_env_stack && st && st->current_ns && st->current_ns->mappings) {
            fn_env_stack = make_list(st->current_ns->mappings, NULL);
        }
    }

    // Create function object
    CljObject *fn = AUTORELEASE((CljObject*)make_function(params, param_count, body, fn_env_stack, NULL, st ? st->current_ns : NULL));

    // Cleanup heap-allocated params if any
    free_obj_array(params, params_stack);

    return fn;
}

// is_special_symbol is now in symbol.c (uses dynamic registration)
// This inline version was removed to use the centralized implementation

// Check if symbol is a builtin function (+, -, *, /, etc.)
// Uses compact array-based lookup for smaller code size
static inline bool is_builtin_function(CljSymbol *symbol) {
    if (!symbol) return false;
    return (symbol == SYM_PLUS ||
            symbol == SYM_MINUS ||
            symbol == SYM_MULTIPLY ||
            symbol == SYM_DIVIDE ||
            symbol == SYM_EQUALS ||
            symbol == SYM_LT ||
            symbol == SYM_GT ||
            symbol == SYM_LE ||
            symbol == SYM_GE ||
            symbol == SYM_PRINT ||
            symbol == SYM_PRINTLN ||
            symbol == SYM_STR ||
            symbol == SYM_NTH ||
            symbol == SYM_FIRST ||
            symbol == SYM_REST ||
            symbol == SYM_COUNT ||
            symbol == SYM_CONS ||
            symbol == SYM_SEQ ||
            symbol == SYM_NEXT ||
            symbol == SYM_FOR ||
            symbol == SYM_DOSEQ ||
            symbol == SYM_DOTIMES);
}

ID eval_symbol(CljSymbol *symbol, EvalState *st) {
    if (!symbol) {
        return NULL;
    }

    // Special case: nil evaluates to NULL
    if (symbol == SYM_NIL) {
        return NULL;
    }

    // Keywords evaluate to themselves
    if (IS_KEYWORD(symbol)) {
        return symbol;
    }

    // Special handling for *ns*
    if (symbol == SYM_NS_STAR) {
        if (st && st->current_ns && st->current_ns->name) {
            return st->current_ns->name;
        }
        CljNamespace *user_ns = ns_get_or_create("user", NULL);
        return (user_ns && user_ns->name) ? user_ns->name : NULL;
    }

    // CRITICAL: Handle qualified symbols (symbol->ns_name is set during parsing)
    // Parser already splits qualified symbols into name and namespace
    // This avoids string parsing in the hot-path
    // NOTE: Alias resolution is now done in the parser, not at runtime
    // OPTIMIZATION: For fully qualified symbols, use pointer directly (no re-interning)
    // This eliminates expensive strcmp calls in hot paths
    if (symbol->ns_name && symbol->ns_name->cname) {
        // Qualified symbol: find target namespace and resolve symbol
        // Try ns_find_by_symbol first (fast path if symbol pointer matches)
        CljNamespace *target_ns = symbol->ns_name ? ns_find_by_symbol(symbol->ns_name) : NULL;
        // Fallback to ns_find if ns_find_by_symbol didn't find it (handles cases where symbol pointers differ)
        // This is important because ns_get_or_create uses intern_symbol(NULL, name), which may return
        // a different symbol pointer than the one from the parser
        if (!target_ns && symbol->ns_name && symbol->ns_name->cname) {
            target_ns = ns_find(symbol->ns_name->cname);
        }
        if (!target_ns) {
            // Namespace not found - throw exception
            const char *cname = symbol->cname ? symbol->cname : "unknown";
            const char *ns_cname = symbol->ns_name && symbol->ns_name->cname ? symbol->ns_name->cname : "unknown";
            size_t qualified_len = strlen(ns_cname) + 1 + strlen(cname) + 1;
            char *qualified_name = (char*)malloc(qualified_len);
            if (qualified_name) {
                snprintf(qualified_name, qualified_len, "%s/%s", ns_cname, cname);
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", qualified_name);
                free(qualified_name);
            } else {
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s/%s in this context", ns_cname, cname);
            }
            return NULL;
        }
        if (target_ns->mappings && symbol->cname) {
            // OPTIMIZATION: For fully qualified symbols, use the symbol pointer directly
            // This avoids expensive re-interning (find_symbol + strcmp) in hot paths
            // The parser already creates the correct qualified symbol pointer
            // CRITICAL: Use sentinel to distinguish "key not found" from "value is nil"
            ID resolved = map_get(target_ns->mappings, symbol, NOT_FOUND);
            if (resolved != NOT_FOUND) {
                // Found in target namespace - return it (can be NULL/nil, which is valid)
                return resolved;
            }
            CljSymbol *symbol_alias = NULL;
            if (symbol->cname) {
                symbol_alias = intern_symbol_global(symbol->cname);
            }
            if (symbol_alias && symbol_alias != symbol) {
                resolved = map_get(target_ns->mappings, symbol_alias, NOT_FOUND);
                if (resolved != NOT_FOUND) {
                    return resolved;
                }
            }

            // If direct lookup failed, rely on namespace mappings to contain canonical keys.
        }

        // Qualified symbol not found in mappings - try native function lookup
        // This handles cases where native functions (like trim) are not yet registered in mappings
        // NOTE: Alias resolution is now done in the parser, so symbol->ns_name is already resolved
        // OPTIMIZATION: Use symbol directly (already qualified) instead of re-interning
        if (symbol->cname) {
            BuiltinFn native_func = native_function_lookup(symbol);
            if (native_func) {
                // Found native function - create function object and return it
                CljCFunc *native_func_obj = (CljCFunc*)make_named_func(native_func, NULL, symbol->cname);
                if (native_func_obj) {
                    // Cache native function in namespace mappings to preserve invariants
                    // Use symbol directly (already qualified) - no need to re-intern
                    ns_define(target_ns, symbol, native_func_obj);
                    return native_func_obj;
                }
            }
        }

        // Qualified symbol not found in target namespace
        const char *cname = symbol->cname ? symbol->cname : "unknown";
        const char *ns_cname = symbol->ns_name && symbol->ns_name->cname ? symbol->ns_name->cname : "unknown";
        size_t qualified_len = strlen(ns_cname) + 1 + strlen(cname) + 1;
        char *qualified_name = (char*)malloc(qualified_len);
        if (qualified_name) {
            snprintf(qualified_name, qualified_len, "%s/%s", ns_cname, cname);
            throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", qualified_name);
            free(qualified_name);
        } else {
            throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s/%s in this context", ns_cname, cname);
        }
        return NULL;
    }

    // Check special forms first - they return themselves
    // Builtin functions need to be resolved from namespace
    if (is_special_symbol(symbol)) {
        return symbol;
    }

    // For builtin functions, resolve from namespace to get the actual function object
    ID value = ns_resolve(st, symbol);
    if (value) {
        // Special case: If value is SYM_NIL, return NULL
        if (value == SYM_NIL) {
            return NULL;
        }
        // ns_resolve returns a value that is safe to use in our scope
        // No need for AUTORELEASE/RETAIN - we didn't create the object
        return value;
    }

    // If not found in namespace but is a builtin, return symbol as fallback
    // (will be handled by eval_list)
    if (is_builtin_function(symbol)) {
        return symbol;
    }

    // Check if symbol is a native function (via native_function_table lookup)
    // Returns function object without registering - registration happens via stubs
    BuiltinFn native_func = native_function_lookup(symbol);
    if (native_func) {
        const char *cname = symbol->cname ? symbol->cname : "unknown";
        return (CljObject*)make_named_func(native_func, NULL, cname);
    }

    // Symbol not found
    const char *cname = symbol->cname ? symbol->cname : "unknown";
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", cname);
    return NULL;
}

ID eval_seq(CljList *list, CljMap *env) {
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg(list, 1, env, NULL);
    if (!arg) return NULL;

    // If argument is already nil, return nil
    // Note: nil is now represented as NULL, so no special nil check needed

    // Check if argument is seqable
    if (!is_seqable(arg)) {
        return NULL;
    }

    // For lists, check if empty - if so, return nil
    switch (arg->type) {
        case CLJ_LIST:
        case CLJ_AST_NODE: {
            CljList *list_data = as_list(arg);
            if (!LIST_FIRST(list_data)) return NULL;  // Empty list -> nil
            return AUTORELEASE(RETAIN(arg));
        }

        default: {
            // For other seqable types, return SeqIterator directly
            CljSeqIterator *seq = (CljSeqIterator*)AUTORELEASE((CljObject*)make_seq(arg));
            if (!seq) return NULL;

            return (CljObject*)seq;
        }
    }
}

// ============================================================================
// FOR-LOOP IMPLEMENTATIONS
// ============================================================================

/**
 * @brief Helper function to extend environment with a new binding
 * @param env The existing environment
 * @param var The variable to bind
 * @param element The value to bind to the variable
 * @return A new environment with the binding added
 */
static CljMap* extend_env_with_binding(CljMap *env, CljObject *var, CljObject *element) {
    // Estimate capacity: existing bindings + new binding
    int capacity = env ? ((CljMap*)env)->count + 1 : 4;
    CljMap *new_env = (CljMap*)make_map(capacity);
    if (new_env) {
        // Copy existing environment bindings
        if (env) {
            MAP_FOR_EACH(env, key, value) {
                CljMap *updated = map_assoc(new_env, key, value);
                ASSIGN(new_env, updated);
            }
        }
        // Add new binding (overwrites the existing value if the key already exists)
        // CRITICAL: map_assoc may return a new map (COW), so we must use the result
        CljMap *updated_env = map_assoc(new_env, var, element);
        ASSIGN(new_env, updated_env);
    }
    return new_env;
}

ID eval_for(CljList *list, CljMap *env) {
    CLJ_ASSERT(env != NULL);
    // (for [binding coll] expr)
    // Returns a lazy sequence of results

    if (!list) {
        return NULL;
    }

    CljObject *binding_list = eval_arg(list, 1, env, NULL);
    CljObject *body = eval_arg(list, 2, env, NULL);

    if (!binding_list || !list_type_matches(TAG(binding_list))) {
        return NULL;
    }

    // Parse binding: [var coll]
    CljList *binding_data = as_list(binding_list);
    if (!binding_data->first || !binding_data->rest) {
        return NULL;
    }

    CljObject *var = binding_data->first;
    CljObject *coll = (CljObject*)LIST_REST(binding_data);

    // Get collection to iterate over
    CljList *coll_data = as_list(coll);
    if (!coll_data->first) {
        return NULL;
    }

    CljObject *collection = coll_data->first; // Simple: just use the expression directly
    if (!collection) {
        return NULL;
    }

    // Create result list
    CljObject *result = (CljObject*)empty_list();

    // Iterate over collection using seq
    CljSeqIterator *seq = make_seq(collection);
    if (seq) {
        while (!seq_empty((CljObject*)seq)) {
            CljObject *element = (CljObject*)seq_first((CljObject*)seq);

            // Create new environment with binding using helper
            CljMap *new_env = extend_env_with_binding(env, var, element);
            if (new_env) {
                // Evaluate body with new binding
                ID body_result = eval_body_with_env(body, new_env, NULL);
                if (body_result) {
                    RELEASE(body_result);
                }

                // Clean up environment
                RELEASE(new_env);
            }

            // Move to next element
            CljObject *next = (CljObject*)seq_next((CljObject*)seq);
            RELEASE(seq);
            seq = (CljSeqIterator*)next;
        }
        // Clean up final seq iterator (not returned as value)
        RELEASE(seq);
    }

    RELEASE(collection);
    return AUTORELEASE(result);
}

ID eval_doseq(CljList *list, CljMap *env) {
    CLJ_ASSERT(env != NULL);
    // (doseq [binding coll] expr)
    // Executes expr for side effects, returns nil

    if (!list) {
        return NULL;
    }

    CljObject *binding_list = list_get_element(list, 1);
    CljObject *body = list_get_element(list, 2);

    if (!binding_list || binding_list->type != CLJ_LIST || !body) {
        return NULL;
    }

    // Parse binding: [var coll]
    CljList *binding_data = as_list(binding_list);
    if (!binding_data->first || !binding_data->rest) {
        return NULL;
    }

    CljObject *var = binding_data->first;
    CljObject *coll = (CljObject*)LIST_REST(binding_data);

    // Get collection to iterate over
    CljList *coll_data = as_list(coll);
    if (!coll_data->first) {
        return NULL;
    }

    CljObject *collection = coll_data->first; // Simple: just use the expression directly
    if (!collection) {
        return NULL;
    }

    // Iterate over collection using seq
    CljSeqIterator *seq = make_seq(collection);
    if (seq) {
        while (!seq_empty((CljObject*)seq)) {
            CljObject *element = (CljObject*)seq_first((CljObject*)seq);

            // Create new environment with binding using helper
            CljMap *new_env = extend_env_with_binding(env, var, element);
            if (new_env) {
                // Evaluate body with new binding
                ID body_result = eval_body_with_env(body, new_env, NULL);
                if (body_result) {
                    RELEASE(body_result);
                }

                // Clean up environment
                RELEASE(new_env);
            }


            CljObject *next = (CljObject*)seq_next((CljObject*)seq);
            RELEASE(seq);
            seq = (CljSeqIterator*)next;
        }
        // Clean up final seq iterator
        RELEASE(seq);
    }

    // Clean up allocated objects
    // Note: collection is a parameter, don't release it
    return AUTORELEASE(NULL); // doseq always returns nil
}

ID eval_list_function(CljList *list, CljMap *env) {
    CLJ_ASSERT(env != NULL);
    (void)env; // Suppress unused parameter warning
    // (list arg1 arg2 ...)
    CLJ_ASSERT(list != NULL);
    if (!list || !list_type_matches(TAG(list))) return NULL;

    CljList *list_data = as_list(list);

    // Create new list starting from the second element (skip 'list' symbol)
    CljObject *args_list = (CljObject*)LIST_REST(list_data);
    if (!args_list) {
        // No arguments - return empty list (like native_list does)
        return empty_list();
    }

    // Simply return the arguments as a list (they're already evaluated by eval_list)
    // ✅ FIX: LIST_REST does NOT return autoreleased object - need to autorelease it
    return AUTORELEASE(RETAIN(args_list));
}

// ============================================================================
// EVAL_LET - Let bindings implementation
// ============================================================================
ID eval_let(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // (let [bindings*] body*)
    // bindings* => binding-form init-expr

    CLJ_ASSERT(env != NULL);

    if (!list || !st) {
        return NULL;
    }

    // Get bindings vector (second element): (let [x 10 y 20] ...)
    CljObject *bindings_vec = list_get_element(list, 1);
    if (!bindings_vec || TAG(bindings_vec) != CLJ_VECTOR) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "let requires a vector for bindings",
                       NULL, 0, 0);
        return NULL;
    }

    CljVector *bindings = as_vector((CljValue)bindings_vec);
    if (!bindings) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "let bindings must be a valid vector",
                       NULL, 0, 0);
        return NULL;
    }
    int binding_count = vector_count(bindings);

    // Bindings must come in pairs (symbol value symbol value ...)
    if (binding_count % 2 != 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "let requires an even number of forms in binding vector",
                       NULL, 0, 0);
        return NULL;
    }

    int pair_count = binding_count / 2;
    CljList *parent_stack = NULL;
    bool parent_stack_owned = false;

    if (ctx && ctx->env_stack) {
        parent_stack = ctx->env_stack;
    } else if (env) {
        parent_stack = make_list(env, NULL);
        parent_stack_owned = true;
    }

    bool has_frame = pair_count > 0;
    CallFrame *let_frame = NULL;
    ID *binding_params = NULL;
    ID *binding_values = NULL;
    if (has_frame) {
        size_t frame_bytes = frame_allocation_size(pair_count);
        let_frame = (CallFrame*)STACK_ALLOC(char, frame_bytes);
        frame_init(let_frame, ctx ? ctx->frame : NULL);

        ID *binding_slots = (ID*)STACK_ALLOC(ID, pair_count * 2);
        binding_params = binding_slots;
        binding_values = binding_slots + pair_count;
    }

    EvalContext let_ctx = ctx ? *ctx : (EvalContext){0};
    // Don't set frame yet - we'll set it after bindings are evaluated
    // This prevents issues when evaluating init expressions
    let_ctx.frame = ctx ? ctx->frame : NULL;
    let_ctx.env_stack = parent_stack;
    if (!let_ctx.env) {
        let_ctx.env = env;
    }
    if (!let_ctx.st) {
        let_ctx.st = st;
    }

    int binding_index = 0;
    for (int i = 0; i < binding_count; i += 2) {
        CljValue sym_val = (CljValue)vector_nth(bindings, i);
        CljValue init_val = (CljValue)vector_nth(bindings, i + 1);

        if (!sym_val || TAG(sym_val) != CLJ_SYMBOL) {
            if (has_frame) frame_release(let_frame);
            if (parent_stack_owned && parent_stack) {
                RELEASE(parent_stack);
            }
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                           "let binding must be a symbol",
                           NULL, 0, 0);
            return NULL;
        }

        ID value = NULL;
        if (!init_val) {
            value = NULL;
        } else if (is_fixnum(init_val) || is_special(init_val)) {
                value = init_val;
            } else {
            value = eval_body(init_val, env, st, &let_ctx);
        }

        if (has_frame) {
            binding_params[binding_index] = sym_val;
            binding_values[binding_index] = value;
            if (value && !IS_IMMEDIATE(value)) {
                RETAIN((CljObject*)value);
            }
            frame_set_bindings(let_frame, ctx ? ctx->frame : NULL,
                               binding_params, binding_values, binding_index + 1);

            // Make newly created bindings visible to subsequent initializers
            let_ctx.frame = let_frame;

            ID stored_value = binding_values[binding_index];
            if (stored_value && TAG(stored_value) == CLJ_CLOSURE) {
                CljList *frame_env_stack = frame_chain_to_env_stack(let_frame, parent_stack);
                CljFunction *func = as_function(stored_value);
                if (func) {
                    ASSIGN(func->env_stack, frame_env_stack);
                }
                if (frame_env_stack) {
                    RELEASE(frame_env_stack);
                }
            }
        }

        if (value && !IS_IMMEDIATE(value)) {
            RELEASE((CljObject*)value);
        }

        binding_index++;
    }

    CljList *frame_env_stack = NULL;
    CljMap *frame_env_head = NULL;
    if (has_frame) {
        frame_env_stack = frame_chain_to_env_stack(let_frame, parent_stack);
        let_ctx.env_stack = frame_env_stack;
        // Keep frame for direct symbol resolution (frame_lookup is faster than map lookup)
        let_ctx.frame = let_frame;
        if (frame_env_stack && LIST_FIRST(frame_env_stack)) {
            frame_env_head = (CljMap*)LIST_FIRST(frame_env_stack);
            let_ctx.env = frame_env_head;
        }
    }

    ID result = NULL;
    int list_len = list_count(list);
    if (list_len > 2) {
        CljMap *body_env = frame_env_head ? frame_env_head : env;
        for (int i = 2; i < list_len; i++) {
            ID body_expr = list_get_element(list, i);
            if (!body_expr) continue;

            RELEASE(result);
                if (is_fixnum((CljValue)body_expr) || is_special((CljValue)body_expr)) {
                    result = body_expr;
                RETAIN((CljObject*)result);
                } else {
                result = eval_body(body_expr, body_env, st, &let_ctx);
            }
        }
    }

    if (has_frame) {
        frame_release(let_frame);
        RELEASE(frame_env_stack);
    }
    if (parent_stack_owned && parent_stack) {
        RELEASE(parent_stack);
    }
    return result;
}

// ============================================================================
// EVAL_DEFN - Function definition macro implementation
// ============================================================================
ID eval_defn(CljList *list, CljMap *env, EvalState *st) {
    // (defn name [params*] body*)
    // Expands to: (def name (fn [params*] body*))

    CLJ_ASSERT(env != NULL);

    if (!list || !st) {
        return NULL;
    }

    // Parse arguments using rest traversal: (defn name [params] body...)
    CljObject *rest_obj = list->rest;
    CljList *rest = as_list(rest_obj);
    if (!rest) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn requires function name",
                       NULL, 0, 0);
        return NULL;
    }

    // Get function name (first element after defn)
    CljObject *name_sym = rest->first;
    if (!name_sym || TAG(name_sym) != CLJ_SYMBOL) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn requires a symbol for function name",
                       NULL, 0, 0);
        return NULL;
    }

#ifdef ENABLE_META
    // Capture metadata from symbol (user-provided) and form (location info)
    CljObject *symbol_meta_obj = meta_get((CljObject*)name_sym);
    CljObject *list_meta_obj = meta_get((CljObject*)list);
    CljMap *symbol_meta_map = (symbol_meta_obj && TAG(symbol_meta_obj) == CLJ_MAP)
                                ? (CljMap*)symbol_meta_obj : NULL;
    CljMap *list_meta_map = (list_meta_obj && TAG(list_meta_obj) == CLJ_MAP)
                                ? (CljMap*)list_meta_obj : NULL;
#endif // ENABLE_META
    
    // Get parameter vector (second element after defn)
    rest_obj = rest->rest;
    rest = as_list(rest_obj);
    if (!rest) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn requires parameter vector",
                       NULL, 0, 0);
        return NULL;
    }

    CljObject *params_vec = rest->first;
    if (!params_vec || TAG(params_vec) != CLJ_VECTOR) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn requires a vector for parameters",
                       NULL, 0, 0);
        return NULL;
    }

    // Get body expressions (everything after params)
    rest_obj = rest->rest;
    rest = as_list(rest_obj);
    if (!rest) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn requires at least one body expression",
                       NULL, 0, 0);
        return NULL;
    }

    // Extract parameters from vector
    CljVector *params_vec_data = as_vector((CljValue)params_vec);
    if (!params_vec_data) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn requires a valid parameter vector",
                       NULL, 0, 0);
        return NULL;
    }

    int param_count = vector_count(params_vec_data);
    ID params_stack[16];
    ID *params = alloc_obj_array(param_count, params_stack);

    for (int i = 0; i < param_count; i++) {
        params[i] = vector_nth(params_vec_data, i);
        if (!params[i] || TAG(params[i]) != CLJ_SYMBOL) {
            free_obj_array(params, params_stack);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                           "defn parameters must be symbols",
                           NULL, 0, 0);
            return NULL;
        }
    }

    // Create fn expression: (fn [params*] body*)
    // We'll create this as a list structure

    // Create body expression or list
    // For multiple body expressions, we need to create a do block
    CljObject *body_expr_obj = NULL;

    // rest now points to the body expressions
    // CRITICAL: For defn, body can be a single expression or multiple expressions
    // If multiple, we need to wrap them in a do block
    // However, we need to distinguish between:
    // 1. Real multiple body expressions: (defn f [x] expr1 expr2)
    // 2. Parsing errors where the next defn is incorrectly parsed as part of the body
    // We check if the second element is a defn - if so, it's likely a parsing error
    if (rest->rest == NULL) {
        // Single body expression - use directly
        body_expr_obj = rest->first;
    } else {
        // Check if second element is a defn (parsing error indicator)
        CljList *rest_list = as_list(rest->rest);
        bool is_parsing_error = false;
        if (rest_list && rest_list->first && list_type_matches(TAG(rest_list->first))) {
            CljList *second_list = as_list(rest_list->first);
            if (second_list && second_list->first && TAG(second_list->first) == CLJ_SYMBOL) {
                CljSymbol *second_sym = as_symbol(second_list->first);
                if (second_sym && second_sym->cname && strcmp(second_sym->cname, "defn") == 0) {
                    // Second element is a defn - this is likely a parsing error
                    // Use only the first body expression
                    is_parsing_error = true;
                }
            }
        }

        if (is_parsing_error) {
            // Parsing error - use only first body expression
            body_expr_obj = rest->first;
        } else {
            // Real multiple body expressions - wrap in do block
            int body_count = 0;
            CljList *current = rest;
            while (current) {
                body_count++;
                if (!current->rest) break;
                current = as_list(current->rest);
            }

            // Build do block: (do expr1 expr2 ...)
            if (body_count > 15) {
                free_obj_array(params, params_stack);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                               "defn body has too many expressions (max 15)",
                               NULL, 0, 0);
                return NULL;
            }

            // Build do list backwards (from end to start) using make_list
            CljList *do_result = NULL;
            current = rest;
            // First collect all body expressions
            ID body_exprs[15];
            int actual_count = 0;
            for (int i = 0; i < body_count && i < 15; i++) {
                if (current && current->first) {
                    body_exprs[actual_count++] = current->first;
                }
                if (!current->rest) break;
                current = as_list(current->rest);
            }
            // Build list backwards
            for (int i = actual_count - 1; i >= 0; i--) {
                do_result = make_list(body_exprs[i], do_result);
            }
            body_expr_obj = (CljObject*)make_list(SYM_DO, do_result);
        }
    }

    if (!body_expr_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "defn body cannot be empty",
                       NULL, 0, 0);
        return NULL;
    }

    // Check if body is :native marker (for native function stubs)
    // Keywords are interned, so pointer comparison suffices
    if (body_expr_obj == (CljObject*)SYM_KW_NATIVE) {
        // Extract Clojure function name
        CljSymbol *name_symbol = as_symbol(name_sym);
        if (!name_symbol || !name_symbol->cname) {
            free_obj_array(params, params_stack);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                           "defn with :native requires a valid function name",
                           NULL, 0, 0);
            return NULL;
        }

        // Lookup native function by Clojure symbol (uses pointer comparison for efficiency)
        BuiltinFn native_func = NULL;
        CljSymbol *lookup_symbol = name_symbol;
        if (st && st->current_ns && st->current_ns->name && name_symbol->cname) {
            CljSymbol *qualified =
                intern_symbol(st->current_ns->name, name_symbol->cname);
            if (qualified) {
                lookup_symbol = qualified;
            }
        }
        native_func = native_function_lookup(lookup_symbol);
        if (!native_func && lookup_symbol != name_symbol) {
            native_func = native_function_lookup(name_symbol);
        }
        if (!native_func) {
            free_obj_array(params, params_stack);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Native function not found for: %s", name_symbol->cname);
            throw_exception(EXCEPTION_RUNTIME, error_msg, NULL, 0, 0);
            return NULL;
        }

        // Create native function object
        // Use Clojure function name (already interned symbol name)
        CljCFunc *native_func_obj = (CljCFunc*)make_named_func(native_func, NULL, name_symbol->cname);
        if (!native_func_obj) {
            free_obj_array(params, params_stack);
            throw_exception(EXCEPTION_RUNTIME,
                           "Failed to create native function object",
                           NULL, 0, 0);
            return NULL;
        }

        // Register native function in namespace (replaces any existing registration)
        ns_define(st->current_ns, name_sym, native_func_obj);

        // Apply metadata to native function
        // Merge user metadata (from ^#^{...}) with standard metadata (:name, :ns)
#ifdef ENABLE_META
        // Build standard metadata (:name, :ns)
        CljMap *standard_meta = make_map(4);
        
        if (SYM_KW_NAME && name_symbol->cname && name_symbol->cname[0] != '\0') {
            CljString *name_str = make_string(name_symbol->cname);
            if (name_str) {
                standard_meta = map_assoc(standard_meta, SYM_KW_NAME, name_str);
                RELEASE(name_str);
            }
        }
        
        if (SYM_KW_NS && st->current_ns && st->current_ns->name) {
            standard_meta = map_assoc(standard_meta, SYM_KW_NS, st->current_ns->name);
        }
        
        // Merge metadata: add location info first (no overwrite), then user metadata
        CljMap *merged_meta = standard_meta;
        if (list_meta_map) {
            merged_meta = map_merge(merged_meta, list_meta_map, false);
        }
        if (symbol_meta_map) {
            merged_meta = map_merge(merged_meta, symbol_meta_map, true);
        }
        
        meta_set((CljObject*)native_func_obj, (CljObject*)merged_meta);
        RELEASE(standard_meta);
#endif // ENABLE_META

        free_obj_array(params, params_stack);
        return name_sym;  // defn returns the symbol
    }

    // Transform recursive tail calls to recur (automatic TCO)
    CljObject *transformed_body = transform_recursive_tail_calls(body_expr_obj, name_sym,
                                                                  (CljObject**)params, param_count,
                                                                  body_expr_obj);
    if (!transformed_body) {
        // Transformation failed - use original body
        transformed_body = body_expr_obj;
    }

    CljSymbol *name_symbol = as_symbol(name_sym);
    if (name_symbol && st && st->current_ns && st->current_ns->name && name_symbol->cname) {
        CljSymbol *qualified_name = intern_symbol(st->current_ns->name, name_symbol->cname);
        if (qualified_name && qualified_name != name_symbol) {
            ID body_id = transformed_body;
            rewrite_recursive_calls_in_slot(&body_id, name_symbol, qualified_name);
            transformed_body = (CljObject*)body_id;
        }
    }

    // CRITICAL: Don't validate recur positions here - recur is only valid when
    // the function is called, not when it's defined. validate_recur_positions
    // would try to evaluate the body, which would fail because there's no RecurContext.

    // Create function object directly (skip fn list creation to save code)
    // Use current namespace mappings as environment for function evaluation
    // This ensures that builtin functions like + are available in closures
    if (!st || !st->current_ns) {
        free_obj_array(params, params_stack);
        throw_exception(EXCEPTION_RUNTIME, "defn requires an evaluation state with namespace", NULL, 0, 0);
        return NULL;
    }

    // Create closure environment stack
    // Get parent stack from env if provided, otherwise create from namespace mappings
    CljList *parent_stack = env ? make_list(env, NULL) : NULL;
    
    // Create environment map with namespace mappings
    int ns_mapping_count = st->current_ns->mappings ? ((CljMap*)st->current_ns->mappings)->count : 0;
    extern TinyClJRuntime g_runtime;
    int core_mapping_count = 0;
    CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
    if (clojure_core && clojure_core->mappings) {
        core_mapping_count = ((CljMap*)clojure_core->mappings)->count;
    }
    
    CljMap *fn_env = (CljMap*)make_map(ns_mapping_count + core_mapping_count + 4);

    // Add current namespace mappings
    if (st->current_ns->mappings) {
        CljMap *ns_mappings = (CljMap*)st->current_ns->mappings;
        MAP_FOR_EACH(ns_mappings, key, val) {
            if (!key) continue;
            CljMap *new_fn_env = map_assoc(fn_env, key, val);
            ASSIGN(fn_env, new_fn_env);

            if (TAG(key) == CLJ_SYMBOL && st->current_ns->name != SYM_CLOJURE_CORE) {
                CljSymbol *qualified_key = as_symbol(key);
                CljSymbol *unqualified_sym = NULL;
                if (qualified_key && qualified_key->cname) {
                    unqualified_sym = intern_symbol_global(qualified_key->cname);
                }
                if (unqualified_sym && unqualified_sym != qualified_key && !map_contains(fn_env, unqualified_sym)) {
                    CljMap *alias_env = map_assoc(fn_env, unqualified_sym, val);
                    ASSIGN(fn_env, alias_env);
                }
            }
        }
    }

    // Add clojure.core namespace mappings
    if (fn_env && clojure_core && clojure_core->mappings) {
        CljMap *core_mappings = (CljMap*)clojure_core->mappings;
        MAP_FOR_EACH(core_mappings, key, val) {
            if (!map_contains(fn_env, key)) {
                CljMap *new_fn_env = map_assoc(fn_env, key, val);
                ASSIGN(fn_env, new_fn_env);
            }
        }
    }

    // Create env_stack with fn_env as first and parent_stack as rest
    CljList *fn_env_stack = make_list(fn_env, parent_stack);

    // Create function object with env_stack
    CljFunction *fn_obj = make_function(params, param_count, transformed_body, fn_env_stack, NULL, st ? st->current_ns : NULL);
    if (!fn_obj) {
        free_obj_array(params, params_stack);
        return NULL;
    }

    // Set function name
    CljFunction *func = fn_obj;
    CljSymbol *sym = as_symbol(name_sym);
    if (func && sym && sym->cname[0] && !func->name) {
        func->name = strdup(sym->cname);
    }

    // Apply metadata to function
#ifdef ENABLE_META
    // Build standard metadata (:name, :ns) - same as for native functions
    CljMap *standard_meta = make_map(4);
    
    if (SYM_KW_NAME && sym->cname && sym->cname[0] != '\0') {
        CljString *name_str = make_string(sym->cname);
        if (name_str) {
            standard_meta = map_assoc(standard_meta, SYM_KW_NAME, name_str);
            RELEASE(name_str);
        }
    }
    
    if (SYM_KW_NS && st->current_ns && st->current_ns->name) {
        standard_meta = map_assoc(standard_meta, SYM_KW_NS, st->current_ns->name);
    }
    
    CljMap *merged_meta = standard_meta;
    if (list_meta_map) {
        merged_meta = map_merge(merged_meta, list_meta_map, false);
    }
    if (symbol_meta_map) {
        merged_meta = map_merge(merged_meta, symbol_meta_map, true);
    }
    
    meta_set((CljObject*)fn_obj, (CljObject*)merged_meta);
    RELEASE(standard_meta);
#endif // ENABLE_META

    // Register function in namespace (after creation for recursive calls)
    ns_define(st->current_ns, name_sym, fn_obj);

    // Add function to env_stack so it can find itself recursively
    CljMap *first_env = (CljMap*)LIST_FIRST(func->env_stack);
    if (first_env) {
        CljObject *func_in_env = map_get((CljValue)first_env, (CljValue)name_sym, NULL);
        if (func_in_env != (CljObject*)fn_obj) {
            CljMap *new_first_env = map_assoc(first_env, name_sym, fn_obj);
            CljList *new_env_stack = make_list(new_first_env, as_list(LIST_REST(func->env_stack)));
            ASSIGN(func->env_stack, new_env_stack);
        }
    }

    RELEASE((CljObject*)fn_obj);
    free_obj_array(params, params_stack);
    return name_sym;
}

// Helper function for evaluating arguments
ID eval_arg(CljList *list, int index, CljMap *env, EvalState *st) {
    return eval_arg_with_context(list, index, env, st, NULL);
}

ID eval_arg_with_context(CljList *list, int index, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(list != NULL);
    if (!list || !list_type_matches(TAG(list))) return NULL;

    // Get element from list
    ID element = list_nth(as_list(list), index);
    if (!element) return NULL;
    
    return eval_arg_from_expr_with_context(element, env, st, ctx);
}

ID eval_arg_from_expr_with_context(ID expr, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (expr == SYM_NIL) return NULL;
    if (!expr) return NULL;

    if (IS_IMMEDIATE(expr)) {
        return expr;
    }

    if (env == NULL) {
        return AUTORELEASE(RETAIN(expr));
    }

    unsigned char expr_tag = TAG(expr);

    CLJ_ASSERT(expr_tag != CLJ_SYMBOL_TOKEN && "Symbol tokens must be canonicalized before evaluation");

    if (expr_tag == CLJ_SYMBOL) {
        // NOTE: SYM_NIL already checked at function entry
        if (IS_KEYWORD(expr)) {
            return expr;
        }
        
        // Use frame_lookup for O(1) parameter resolution (symbols are interned)
        if (ctx && ctx->frame) {
            ID frame_value = NULL;
            if (frame_lookup(ctx->frame, expr, &frame_value)) {
                if (frame_value == FRAME_NIL_SENTINEL) {
                    return NULL;  // Parameter bound to nil
                }
                if (!frame_value) return NULL;
                if (IS_IMMEDIATE(frame_value)) return frame_value;
                return AUTORELEASE(RETAIN(frame_value));
            }
        }
        
        // Fallback: check legacy params array (for tests/contexts without frame)
        // Cold path: only reached when frame_lookup didn't find the symbol
        if (ctx && ctx->params && ctx->param_values) {
            for (int i = 0; i < ctx->param_count; i++) {
                if (ctx->params[i] == expr) {
                    ID value = ctx->param_values[i];
                    if (!value) return NULL;
                    if (IS_IMMEDIATE(value)) return value;
                    return AUTORELEASE(RETAIN(value));
                }
            }
        }
        
        ID resolved_value = NULL;
        
        // If context is provided with env_stack, use resolve_symbol_in_env
        // to search through the entire environment stack (for nested let blocks)
        if (ctx) {
            EvalState *eval_st = get_eval_state(ctx, st);
            ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, env, ctx->frame, expr, eval_st);
            if (resolved_id) {
                if (resolved_id == FRAME_NIL_SENTINEL) {
                    return NULL;
                }
                // CRITICAL: If resolved_id is still a symbol (not a value), throw exception
                // This prevents infinite loops where a symbol resolves to itself
                if (!IS_IMMEDIATE(resolved_id) &&
                    TAG(resolved_id) == CLJ_SYMBOL &&
                    !IS_KEYWORD(resolved_id)) {
                    bool resolves_to_self = (resolved_id == expr);
                    if (!resolves_to_self && resolved_id && expr) {
                        resolves_to_self = clj_equal(resolved_id, expr);
                    }
                    if (resolves_to_self) {
                        CljSymbol *sym_obj = as_symbol(expr);
                        const char *sym_name = sym_obj && sym_obj->cname ? sym_obj->cname : "unknown";
                        throw_unresolved_symbol_exception(sym_name);
                        return NULL;
                    }
                }
                resolved_value = resolved_id;
            }
            // Not found in env_stack or still a symbol, fall through to namespace resolution
        }
        
        if (!resolved_value && env && TAG(env) == CLJ_MAP) {
            // Use sentinel to distinguish "key not found" from "value is nil"
            ID resolved_id = map_get(env, expr, NOT_FOUND);
            if (resolved_id != NOT_FOUND) {
                // Key exists in map (value may be NULL/nil)
                // map_get returns retained value, eval_arg should return AUTORELEASE
                if (!resolved_id) return NULL; // nil
                resolved_value = resolved_id;
            }
        }

        if (!resolved_value) {
            CljObject *resolved = ns_resolve(st, as_symbol(expr));
            if (resolved) {
                resolved_value = resolved;
            }
        }

        if (resolved_value) {
            if (IS_IMMEDIATE(resolved_value)) {
                return resolved_value;
            }
            return AUTORELEASE(RETAIN(resolved_value));
        }

        // Only call as_symbol when needed (error paths)
        CljSymbol *sym_obj = as_symbol(expr);
        if (sym_obj && sym_obj->cname) {
            CljNamespace *ns_candidate = ns_find(sym_obj->cname);
            if (ns_candidate) {
                return (CljObject*)ns_candidate;
            }
        }
        const char *sym_name = sym_obj && sym_obj->cname ? sym_obj->cname : "unknown";
        throw_unresolved_symbol_exception(sym_name);
        return NULL;
    }

    if (list_type_matches(expr_tag)) {
        // Fast-path: use caller's EvalState (99% of cases)
        // OPTIMIZATION: Use thread-local EvalState instead of creating temporary
        EvalState *eval_st = st ? st : builtin_get_eval_state();
        return eval_list(as_list(expr), env, eval_st, ctx);
    }

    if (expr_tag == CLJ_MAP) {
        CljMap *map = (CljMap*)expr;
        CljMap *result = map_empty();

        MAP_FOR_EACH(map, key, value) {
            ID key_id = key;
            ID value_id = value;
            ID eval_key = (key_id == SYM_NIL) ? NULL : eval_body(key_id, env, st, NULL);
            ID eval_value = (value_id == SYM_NIL) ? NULL : eval_body(value_id, env, st, NULL);
            ASSIGN(result, map_assoc(result, eval_key, eval_value));
        }

        return AUTORELEASE(result);
    }

    return expr;
}

ID eval_dotimes(CljList *list, CljMap *env, EvalState *st) {
    CLJ_ASSERT(env != NULL);
    // (dotimes [var n] expr)
    // Executes expr n times with var bound to 0, 1, ..., n-1

    if (!list) {
        return NULL;
    }

    // Parse arguments directly without evaluation
    CljList *list_data = as_list(list);
    if (!list_data->rest) {
        return NULL;
    }

    // Extract binding vector/list (first argument after dotimes)
    CljObject *binding_list = NULL;
    CljObject *body_list = NULL;

    if (list_data->rest && list_type_matches(TAG(list_data->rest))) {
        CljList *args_list = as_list(list_data->rest);
        binding_list = args_list->first;
        body_list = args_list->rest; // Rest contains all body expressions
    }

    if (!binding_list || !body_list) {
        return NULL;
    }

    // Parse binding: [var n] - support both vectors and lists
    CljObject *var = NULL;
    CljObject *n_obj = NULL;

    if (binding_list && TAG(binding_list) == CLJ_VECTOR) {
        // Use nth function to safely access vector elements
        var = nth2((ID[]){binding_list, fixnum(0)}, 2);
        n_obj = nth2((ID[]){binding_list, fixnum(1)}, 2);
    } else if (binding_list && list_type_matches(TAG(binding_list))) {
        CljList *binding_data = as_list(binding_list);
        if (!binding_data->first || !binding_data->rest) {
            return NULL;
        }
        var = binding_data->first;
        CljList *rest_list = as_list(binding_data->rest);
        if (!rest_list || !rest_list->first) {
            return NULL;
        }
        n_obj = rest_list->first;
    } else {
        return NULL;
    }

    if (!var || !n_obj) {
        return NULL;
    }

    // Evaluate n_obj if it's a symbol (e.g., when dotimes is used inside let)
    CljObject *n_evaluated = (TAG(n_obj) == CLJ_SYMBOL)
        ? resolve_symbol_in_env_with_frame(NULL, env, NULL, n_obj, st)
        : n_obj;

    if (!n_evaluated || TAG(n_evaluated) != CLJ_INT) {
        return NULL;
    }

    int n = as_fixnum((CljValue)n_evaluated);

    // Namespace mappings are now resolved via resolve_symbol_in_env
    // No need to pre-add them to the environment
    CljMap *base_env = env;

    // Execute body n times
    for (int i = 0; i < n; i++) {
        // Extend environment with loop variable binding
        CljMap *new_env = NULL;
        if (base_env && TAG(base_env) == CLJ_MAP) {
            CljValue i_value = fixnum((int32_t)i);
            new_env = map_assoc(base_env, var, i_value);
            RETAIN(new_env);
        } else {
            new_env = (CljMap*)make_map(4);
            if (new_env) {
                CljValue i_value = fixnum((int32_t)i);
                CljMap *updated_env = map_assoc(new_env, var, i_value);
                ASSIGN(new_env, updated_env);
            }
        }

        if (new_env) {
            // OPTIMIZATION: Use thread-local EvalState instead of creating temporary
            EvalState *eval_st = st ? st : builtin_get_eval_state();

            // Evaluate body expressions
            CljObject *body_result = NULL;
            if (body_list && list_type_matches(TAG(body_list))) {
                CljList *body_items = as_list(body_list);
                while (body_items && body_items->first) {
                    if (body_result) {
                        RELEASE(body_result);
                    }
                    body_result = eval_body(body_items->first, new_env, eval_st, NULL);
                    body_items = body_items->rest ? as_list(body_items->rest) : NULL;
                }
            } else {
                body_result = eval_body(body_list, new_env, eval_st, NULL);
            }
            if (body_result) {
                RELEASE(body_result);
            }

            // Clean up environment
            RELEASE(new_env);
        }
    }

    // Clean up base_env if we created it
    if (base_env != env && base_env) {
        RELEASE(base_env);
    }

    return AUTORELEASE(NULL);
}

// ============================================================================
// EVAL_TIME - Time measurement special form implementation
// ============================================================================
ID eval_time(CljList *list, CljMap *env, EvalState *st) {
    // (time expr)
    if (!list || !st) {
        return NULL;
    }

    // Validate arity: exactly 1 argument
    int argc = list_count(list);
    if (!validate_arity(argc - 1, 1, "time")) { // -1 because list includes the 'time' symbol
        return NULL;
    }

    // Get the expression to time (second element): (time expr)
    CljObject *expr = list_get_element(list, 1);
    if (!expr) {
        return NULL;
    }

    // Start timing with gettimeofday (works on all Unix systems)
    struct timeval start, end;
    gettimeofday(&start, NULL);

    // Use provided env or fall back to current_ns->mappings (like eval_parsed does)
    CljMap *eval_env = env;
    if (!eval_env && st && st->current_ns) {
        eval_env = (CljMap*)st->current_ns->mappings;
    }

    // Evaluate the expression using eval_list with the provided environment
    // This ensures that functions like + are available from the environment
    CljObject *result = NULL;
    if (expr && list_type_matches(TAG(expr))) {
        result = eval_list(as_list(expr), eval_env, st, NULL);
    } else if (expr && TAG(expr) == CLJ_SYMBOL) {
        // For symbols, look up in environment (if provided) or namespace
        if (eval_env && TAG(eval_env) == CLJ_MAP) {
            CljObject *resolved = map_get((CljValue)eval_env, (CljValue)expr, NULL);
            // map_get returns retained values - use AUTORELEASE for eval_time
            result = resolved ? AUTORELEASE(resolved) : NULL;
        }
        // If not found in environment, try namespace lookup
        if (!result && st) {
            CljObject *resolved = ns_resolve(st, as_symbol(expr));
            // ns_resolve returns retained values - use AUTORELEASE for eval_time
            result = resolved ? AUTORELEASE(resolved) : NULL;
        }
    } else {
        // Literal value - return as-is (no memory management needed for immediates)
        result = expr;
        // For heap objects, use AUTORELEASE to match eval_list behavior
        // expr is already retained by caller, so just use AUTORELEASE
        if (result && !IS_IMMEDIATE(result)) {
            result = AUTORELEASE(result);
        }
    }

    // End timing
    gettimeofday(&end, NULL);

    // Calculate elapsed time in milliseconds with microsecond precision
    long long start_us = start.tv_sec * 1000000LL + start.tv_usec;
    long long end_us = end.tv_sec * 1000000LL + end.tv_usec;
    double elapsed_ms = (double)(end_us - start_us) / 1000.0;

    // Print timing information (Clojure-compatible: "msecs" format)
    // Suppress output in test context
    if (!g_suppress_time_output) {
        printf("Elapsed time: %.2f msecs\n", elapsed_ms);
    }

    // Return the result of the evaluated expression (Clojure-compatible: return the value)
    // All paths now return AUTORELEASE objects (or immediates/NULL)
    // If result is NULL, return NULL (nil)
    if (!result) {
        return NULL;
    }
    // If result is immediate, return it directly
    if (IS_IMMEDIATE(result)) {
        return result;
    }
    // For heap objects from map_get/ns_resolve, use AUTORELEASE
    // eval_list already returns AUTORELEASE, so we just return it
    return result;
}

// ============================================================================
// FUNCTION CALL IMPLEMENTATION
// ============================================================================


// ============================================================================
// EVAL_STRING IMPLEMENTATION
// ============================================================================

/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_string(const char* expr_str, EvalState *eval_state) {
    CLJ_ASSERT(expr_str != NULL);
    CLJ_ASSERT(eval_state != NULL);

    CljValue parsed = parse(expr_str, eval_state);
    if (parsed == NULL) {
        // NULL from parse() indicates a parsing error (nil is now parsed as SYM_NIL)
        throw_exception(EXCEPTION_PARSE, "Failed to parse expression", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Check if parsed is an immediate value
    if (IS_IMMEDIATE(parsed)) {
        // For immediate values, return them as CljObject* (they're already evaluated)
        return parsed;
    }

    // For heap objects, evaluate them (use NULL env to use current_ns->mappings)
    ID result = eval_parsed(parsed, eval_state, NULL);

    // result can be NULL only if the evaluation result is nil
    // If eval_parsed fails, it should throw an exception, not return NULL
    return result;
}

// ============================================================================
// COMMON EVALUATION HELPERS
// ============================================================================

ID* alloc_obj_array(int size, ID *stack_buffer) {
    if (size <= 16) {
        return stack_buffer;
    }
    return malloc((size_t)size * sizeof(*stack_buffer));
}

void free_obj_array(ID *array, ID *stack_buffer) {
    if (array != stack_buffer) {
        free((void*)array);
    }
}

/*
 * list_get_element elimination plan (todo3):
 * 1. Replace the helper with a streaming iterator in the evaluator: callers
 *    in function_call.c and eval_special_forms.c will switch to walking the
 *    list once (using LIST_FIRST/LIST_REST) instead of repeatedly invoking an
 *    O(n) walker per access.
 * 2. Add small helpers (e.g. list_nth_cached) local to the handful of spots
 *    that genuinely need random access (def/let destructuring) and have them
 *    operate on temporary arrays built during parsing rather than runtime
 *    traversal.
 * 3. Once all call-sites are migrated, delete this helper entirely and move
 *    the remaining nth-style logic into parser-time normalization so the
 *    evaluator always works with stable AST nodes.
 */
CljObject* list_get_element(CljList *list, int index) {
    if (!list || index < 0) return NULL;
    CljList *node = list;
    if (index == 0) return LIST_FIRST(node);
    int i = 0;
    while (i < index) {
        CljObject *rest = LIST_REST(node);
        if (!rest || !list_type_matches(TAG(rest))) return NULL;
        node = as_list(rest);
        i++;
    }
    return LIST_FIRST(node);
}



