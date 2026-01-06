#include <subjective-c/object.h>
#include "eval.h"
#include "symbol.h"
#include <stdio.h>
#include "exception.h"
#include "function.h"
#include "validation.h"
#include "builtins.h"
#include "optimize.h"
#include "parser.h"  // For eval_parsed
#include "reader.h"  // For Reader API (used by eval_string)
#include "common.h"

// Branch prediction hints for hot paths
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#include "error_messages.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "seq.h"
#include "namespace.h"
#include "memory.h"
#include "meta.h"
#include "list.h"
#include "value.h"
#include "environment.h"
#include "ast.h"
#include <subjective-c/vector.h>
#include "event_loop.h"
#include "channel.h"
#include <subjective-c/strings.h>  // For pr_str
#include "to_string.h"  // For is_special_symbol
#include "eval_arithmetic.h"
#include "eval_comparison.h"
#include <time.h>

#include "eval_sequence.h"
#include "eval_special_forms.h"

#include <signal.h>
extern __attribute__((weak)) volatile sig_atomic_t g_clojure_core_last_form;

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

// Evaluation context structures are defined in function_call.h

#include <subjective-c/map.h>
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

// Global variable to suppress time output in tests
static bool g_suppress_time_output = false;

void set_suppress_time_output(bool suppress) {
    g_suppress_time_output = suppress;
}

// Resolve cache helpers ----------------------------------------------------
static INLINE CljNamespace *get_resolve_ns(const EvalContext *ctx, EvalState *st) {
    if (ctx && ctx->resolve_ns) {
        return ctx->resolve_ns;
    }
    return st ? st->current_ns : NULL;
}

static INLINE CljSymbol* resolve_cache_ns_key_ctx(CljSymbol *op_sym, const EvalContext *ctx, EvalState *st) {
    if (op_sym && op_sym->ns_name) {
        return op_sym->ns_name;
    }
    CljNamespace *resolve_ns = get_resolve_ns(ctx, st);
    if (resolve_ns && resolve_ns->name) {
        return resolve_ns->name;
    }
    return NULL;
}

static INLINE ID resolve_cache_lookup_value(CljSymbol *ns_key, ID op) {
    if (!ns_key || !g_runtime.resolve_cache) {
        return NULL;
    }
    ID ns_cache_id = map_get(g_runtime.resolve_cache, ns_key);
    if (ns_cache_id == NOT_FOUND || !ns_cache_id) {
        return NULL;
    }
    CljMap *ns_cache = (CljMap*)ns_cache_id;
    ID cached = map_get(ns_cache, op);
    if (cached == NOT_FOUND || !cached) {
        return NULL;
    }
    return cached;
}

static void resolve_cache_store_value(CljSymbol *ns_key, ID op, ID resolved) {
    if (!ns_key || !g_runtime.resolve_cache) {
        return;
    }
    ID ns_cache_id = map_get(g_runtime.resolve_cache, ns_key);
    CljMap *ns_cache = (ns_cache_id == NOT_FOUND) ? NULL : (CljMap*)ns_cache_id;
    if (!ns_cache) {
        ns_cache = make_map(RESOLVE_CACHE_SIZE);
    }
    ASSIGN(ns_cache, map_assoc(ns_cache, op, resolved));
    ASSIGN(g_runtime.resolve_cache, map_assoc(g_runtime.resolve_cache, ns_key, ns_cache));
}

// Forward declarations
ID eval_body_with_params(ID body, const EvalContext *ctx);
ID eval_time(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_fn_with_context(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);
ID eval_arg_from_expr_with_context(ID expr, CljMap *env, EvalState *st, const EvalContext *ctx);
static INLINE ID eval_symbol_with_resolve_ns(CljSymbol *symbol, EvalState *st, CljNamespace *resolve_ns);
// is_special_symbol is now in symbol.c
static INLINE bool is_builtin_function(CljSymbol *symbol);



// Forward declarations for loop evaluation
ID eval_body_with_env(ID body, CljMap *env, EvalState *st);



// Helper function to throw unresolved symbol exception (DRY principle)
static void throw_unresolved_symbol_exception(const char *sym_name) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
        "Unable to resolve symbol: %s in this context", sym_name);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
ID eval_function_call(ID fn, ID *args, unsigned int argc, CljMap *env, EvalState *st) {
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
        ID result = native_func->fn(args, argc);
        builtin_set_eval_state(NULL); // Clear after call
        return result;
    }

    // It's a Clojure function (CljFunction)
    CljFunction *func = (CljFunction*)fn;
    if (!func) {
        return make_exception(EXCEPTION_RUNTIME, "Invalid function object", NULL, 0, 0);
    }

    // Arity check - variadic functions accept >= required params
    int param_count = func->params ? (int)vector_count(func->params) : 0;
    int8_t vi = func->variadic_index;
    unsigned int required = (param_count > 0) ? (unsigned int)param_count : 0;
    if (vi < 0) {
        if (argc != required) {
            throw_exception(EXCEPTION_ARITY, "Arity mismatch in function call", NULL, 0, 0);
            return NULL;
        }
    } else {
        if (argc < (unsigned int)vi) {
            throw_exception(EXCEPTION_ARITY, "Arity mismatch in function call", NULL, 0, 0);
            return NULL;
        }
    }

    // OPTIMIZATION: Use static arrays instead of STACK_ALLOC to avoid alloca overhead
    ID current_args[16];
    ID recur_args[16];
    int used_recur_slots = 0;
    ID *params_array = func->params ? vector_as_array(func->params) : NULL;
    
    // Variadic handling: build effective params/values only when needed
    ID variadic_params[16];
    ID *effective_params = params_array;
    int effective_count = param_count;
    
    if (UNLIKELY(vi >= 0)) {
        // Variadic: params before &, then rest param bound to list
        effective_count = vi + 1;
        for (int i = 0; i < vi; i++) {
            variadic_params[i] = params_array[i];
            current_args[i] = args[i];
        }
        variadic_params[vi] = params_array[vi + 1];  // rest param after &
        // Collect remaining args into list (nil if none)
        CljList *rest = NULL;
        for (int i = argc - 1; i >= vi; i--)
            rest = make_list(args[i], rest);
        current_args[vi] = rest;
        effective_params = variadic_params;
    } else {
        // Not variadic: direct copy
        for (int i = 0; i < param_count; i++)
            current_args[i] = args[i];
    }
    
    for (int i = 0; i < effective_count; i++)
        recur_args[i] = NULL;
    int current_argc = effective_count;
    int recur_arg_count = -1;

    // Create call frame with parameters (fixed-size stack variable)
    CLJ_ASSERT(effective_count <= CALLFRAME_MAX_PARAMS && "Too many parameters");
    CallFrame call_frame_storage;
    CallFrame *call_frame = &call_frame_storage;
    frame_init(call_frame, NULL);
    frame_set_bindings_init(call_frame, NULL, effective_params, current_args, current_argc);

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
            .env_stack = call_env_stack,  // Closure environment only
            .frame = call_frame,          // Stack-based frame for parameters
            .st = st,
            .resolve_ns = func->ns ? func->ns : (st ? st->current_ns : NULL),
            .recur_args = recur_args,
            .recur_arg_count = &recur_arg_count,
            .recur_param_count = param_count  // Fixed for this function
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
            frame_set_bindings(call_frame, NULL, effective_params, current_args, current_argc);

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
static INLINE CljMap* env_stack_head(CljList *stack) {
    if (stack && list_type_matches(TAG(stack))) {
        ID first = LIST_FIRST(stack);
        if (first && TAG(first) == CLJ_MAP) {
            return (CljMap*)first;
        }
    }
    return NULL;
}

static INLINE CljMap *get_closure_env(const EvalContext *ctx) {
    if (!ctx) {
        return NULL;
    }
    CljMap *from_stack = env_stack_head(ctx->env_stack);
    if (from_stack) {
        return from_stack;
    }
    return ctx->env;
}

static INLINE EvalState *get_eval_state(const EvalContext *ctx, EvalState *fallback) {
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
            .frame = NULL,
            .st = st,
            .resolve_ns = st ? st->current_ns : NULL,
            .recur_args = NULL,
            .recur_arg_count = NULL,
            .recur_param_count = 0
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

// Forward declarations (defined later in this file)
static INLINE bool is_dynamic_var_symbol(const CljSymbol *symbol);
static INLINE ID dynamic_binding_lookup(EvalState *st, CljSymbol *symbol);

// Extended version that also searches in CallFrame
static INLINE ID resolve_symbol_in_env_with_frame(CljList *env_stack, CljMap *fallback_env, CallFrame *frame, ID sym, EvalState *st) {
    if (!sym || TAG(sym) != CLJ_SYMBOL) {
        return NOT_FOUND;
    }

    // Fast-path: Check frame first (most common case for parameters)
    if (frame) {
        ID frame_value = NOT_FOUND;
        if (frame_lookup(frame, sym, &frame_value)) {
            if (frame_value == NOT_FOUND) {
                return (ID)SYM_NIL;
            }
            return frame_value;
        }
    }

    // Search env_stack (for let bindings, closure captures)
    CljList *current_stack = env_stack;
    while (current_stack && list_type_matches(TAG(current_stack))) {
        ID env_obj_id = LIST_FIRST(current_stack);
        if (env_obj_id && TAG(env_obj_id) == CLJ_MAP) {
            CljMap *env = (CljMap*)env_obj_id;
            ID resolved = map_get(env, sym);
            if (resolved != NOT_FOUND) {
                return resolved ? resolved : (ID)SYM_NIL;
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
        ID resolved = map_get(fallback_env, sym);
        if (resolved != NOT_FOUND) {
            return resolved ? resolved : (ID)SYM_NIL;
        }
    }

    // Dynamic vars can be bound to nil (NULL) and must be treated as resolved.
    if (st) {
        CljSymbol *sym_obj = as_symbol(sym);
        if (sym_obj && is_dynamic_var_symbol(sym_obj)) {
            ID bound = dynamic_binding_lookup(st, sym_obj);
            if (bound != NOT_FOUND) {
                return bound ? bound : (ID)SYM_NIL;
            }
        }
    }

    return NOT_FOUND;
}

// Convert a CallFrame chain into a heap-based env_stack for closures
static CljList* frame_chain_to_env_stack(CallFrame *frame, CljList *parent_stack) {
    if (!frame) {
        return RETAIN(parent_stack);
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
    RELEASE(parent_with_frames);
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
            return map_get_sentinel((CljMap*)env, body, NULL);
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
            return body;
    }
}

// Simplified body evaluation with parameter binding
ID eval_body_with_params(ID body, const EvalContext *ctx) {
    // Handle nil body gracefully (represents Clojure nil)
    if (!body) {
        return NULL;
    }

    if (body && TAG(body) == CLJ_SYMBOL) {
        // CRITICAL: Check if symbol is a keyword FIRST - keywords evaluate to themselves
        // This must come BEFORE symbol resolution attempts
        if (IS_KEYWORD(body)) {
            return body;
        }

        CljSymbol *body_sym = as_symbol(body);
        CLJ_ASSERT(body_sym != NULL && "TAG(body)==CLJ_SYMBOL but as_symbol returned NULL");

        // NOTE: Do NOT treat special symbols or dynamic vars as self-evaluating here.
        // Lexical bindings (let/params) must be able to shadow them in value position.

        // Use central symbol resolution function (DRY: handles environment stack and frames)
        if (ctx) {
            CljMap *ctx_env_map = ctx->env_stack ? env_stack_head(ctx->env_stack) : NULL;
            ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, ctx_env_map, ctx->frame, body, get_eval_state(ctx, NULL));
            const char *log_sym = (body_sym && body_sym->cname) ? body_sym->cname : "<anon>";
            if (resolved_id != NOT_FOUND) {
                if (!resolved_id || resolved_id == SYM_NIL) {
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
                return AUTORELEASE(RETAIN(resolved_id));
            }
        }

        // Dynamic vars (earmuffed symbols) are resolved dynamically, not lexically.
        // This is critical for closures: *ns* must reflect the caller's current namespace.
        EvalState *ctx_state_for_dynamic = get_eval_state(ctx, NULL);
        if (!ctx_state_for_dynamic) ctx_state_for_dynamic = builtin_get_eval_state();
        if (body_sym && body_sym->cname && strcmp(body_sym->cname, "*ns*") == 0) {
            if (ctx_state_for_dynamic && ctx_state_for_dynamic->current_ns) {
                return (ID)ctx_state_for_dynamic->current_ns;
            }
            return (ID)ns_get_or_create("user", NULL);
        }
        if (body_sym && is_dynamic_var_symbol(body_sym)) {
            ID bound = dynamic_binding_lookup(ctx_state_for_dynamic, body_sym);
            if (bound != NOT_FOUND) {
                return bound;
            }
        }

        // Special forms evaluate to themselves only if not lexically bound.
        if (is_special_symbol(body_sym)) {
            return body;
        }
        // If still not found, try namespace lookup (for recursive function calls)
        // ns_resolve takes CljObject* (only objects, not immediates) and returns ID
        // body is a symbol (CljObject*), so we can pass it directly
        EvalState *ctx_state_after_env = get_eval_state(ctx, NULL);
        {
            CljNamespace *resolve_ns = get_resolve_ns(ctx, ctx_state_after_env);
            ID resolved_id = ns_resolve_in(resolve_ns, as_symbol(body));
            if (resolved_id != NOT_FOUND) {
                if (!resolved_id || resolved_id == SYM_NIL) {
                    return NULL;
                }
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

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: {
            // Vector literals need to have their elements evaluated
            CljVector *vec = (CljVector*)body;
            unsigned int count = vector_count(vec);
            
            // Empty vector - return as-is
            if (count == 0) {
                return RETAIN(body);
            }
            
            // Create new vector with evaluated elements
            CljVector *result = make_vector(count, CLJ_VECTOR);
            RETAIN(result);
            
            VECTOR_FOR_EACH(vec, elem) {
                ID eval_elem = NULL;
                
                // Evaluate element recursively
                if (elem) {
                    eval_elem = eval_body_with_params(elem, ctx);
                }
                
                // Add evaluated element to result vector
                ASSIGN(result, vector_conj(result, eval_elem));
            }
            
            return AUTORELEASE(result);
        }

        case CLJ_MAP: {
            // Map literals need to have their keys and values evaluated
            CljMap *map = (CljMap*)body;
            CljMap *result = map_empty();
            RETAIN(result);
            
            MAP_FOR_EACH(map, key, value) {
                ID eval_key = key ? eval_body_with_params(key, ctx) : NULL;
                ID eval_value = value ? eval_body_with_params(value, ctx) : NULL;
                
                ASSIGN(result, map_assoc(result, eval_key, eval_value));
            }
            
            return AUTORELEASE(result);
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
                    if (resolved_id == NOT_FOUND) {
                        return NULL;
                    }
                    // resolve_symbol_in_env returns values from map_get (pointer) or eval_symbol (AUTORELEASE)
                    // eval_body should return AUTORELEASE objects
                    // For map_get: RETAIN needed. For eval_symbol: already AUTORELEASE.
                    // Since we can't distinguish, use RETAIN+AUTORELEASE for safety
                    return AUTORELEASE(RETAIN(resolved_id));
                }
            }

            // Resolve symbol - first try local environment, then namespace
            // Note: We need to check if key exists, not just if value is non-NULL,
            // because nil (NULL) is a valid value
            if (env && TAG(env) == CLJ_MAP) {
                // Use sentinel to distinguish "key not found" from "value is nil"
                ID result_id = map_get((CljMap*)env, body);
                if (result_id != NOT_FOUND) {
                    return (CljObject*)result_id;
                }
            }

            // If not found in local environment, try namespace
            if (st && st->current_ns && st->current_ns->mappings) {
                // Use sentinel to distinguish "key not found" from "value is nil"
                ID result_id = map_get(st->current_ns->mappings, body);
                if (result_id != NOT_FOUND) {
                    return (CljObject*)result_id;
                }
            }

            // If still not found, try global symbol resolution (includes clojure.core)
            // This is important for built-in functions like inc, dec, etc.
            if (st) {
                CljSymbol *sym = as_symbol(body);
                if (sym && is_dynamic_var_symbol(sym)) {
                    ID bound = dynamic_binding_lookup(st, sym);
                    if (bound != NOT_FOUND) {
                        return bound; // may be NULL for nil
                    }
                }

                ID resolved = eval_symbol(sym, st);
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

        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: {
            // Vector literals need to have their elements evaluated
            // This is necessary for cases like [(f x) (g x)] where f and g should be called
            CljVector *vec = (CljVector*)body;
            unsigned int count = vector_count(vec);
            
            // Empty vector - return as-is
            if (count == 0) {
                return body;
            }
            
            // Create new vector with evaluated elements
            CljVector *result = make_vector(count, CLJ_VECTOR);
            RETAIN(result);
            
            VECTOR_FOR_EACH(vec, elem) {
                ID eval_elem = NULL;
                
                // Check for SYM_NIL before calling eval_body
                if (elem && TAG(elem) == CLJ_SYMBOL && elem == SYM_NIL) {
                    eval_elem = NULL;  // nil evaluates to NULL
                } else if (elem) {
                    eval_elem = eval_body(elem, env, st, ctx);
                }
                
                // Add evaluated element to result vector
                ASSIGN(result, vector_conj(result, eval_elem));
            }
            
            return AUTORELEASE(result);
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
                if (key && key_tag == CLJ_SYMBOL && key == (CljObject*)SYM_NIL) {
                    eval_key = NULL;  // nil evaluates to NULL
                } else if (key) {
                    eval_key = eval_body(key, env, st, ctx);
                }

                ID eval_value = NULL;
                if (value && value_tag == CLJ_SYMBOL && value == (CljObject*)SYM_NIL) {
                    eval_value = NULL;  // nil evaluates to NULL
                } else if (value) {
                    eval_value = eval_body(value, env, st, ctx);
                }

                // Add evaluated key-value pair to result map
                ASSIGN(result, map_assoc(result, eval_key, eval_value));

                // Release evaluated key and value if they were retained
                if (eval_key && eval_key != key) RELEASE(eval_key);
                if (eval_value && eval_value != value) RELEASE(eval_value);
            }

            return AUTORELEASE(result);
        }

        default:
            // Literal value
            return body;
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

static INLINE bool is_dynamic_var_symbol(const CljSymbol *symbol) {
    return is_earmuffed_dynamic_symbol(symbol);
}

static INLINE ID dynamic_binding_lookup(EvalState *st, CljSymbol *symbol) {
    if (!st || !symbol || !st->dynamic_bindings) {
        return NOT_FOUND;
    }

    unsigned int depth = vector_count(st->dynamic_bindings);
    for (unsigned int i = depth; i > 0; i--) {
        ID frame_id = vector_nth(st->dynamic_bindings, i - 1);
        if (!frame_id || TAG(frame_id) != CLJ_MAP) {
            continue;
        }
        ID v = map_get((CljMap*)frame_id, (ID)symbol);
        if (v != NOT_FOUND) {
            // v may be NULL (nil) - that's a valid binding value.
            return v;
        }
    }

    return NOT_FOUND;
}

// Handle recur special form
// Resolve operator symbol from environment or namespace
// DRY: Uses central resolve_symbol_in_env function
static INLINE ID resolve_list_operator(ID op, CljMap *env, EvalState *st, const EvalContext *ctx, CljASTNode *call_node) {
    if (!op || TAG(op) != CLJ_SYMBOL) {
        return op;
    }

    CljSymbol *op_sym = as_symbol(op);
    bool op_is_dynamic = is_dynamic_var_symbol(op_sym);

    // === HOT PATH: ctx vorhanden (typisch für fib und andere rekursive Funktionen) ===
    if (ctx) {
        // 1) Frame-Lookup (Parameter wie n) - schnellster Pfad
        if (ctx->frame) {
            ID frame_value = NULL;
            if (frame_lookup(ctx->frame, op, &frame_value)) {
                return frame_value;
            }
        }
        
        // 2) Callsite-Cache (gecachte Funktionen wie fib selbst)
        // IMPORTANT: Dynamic vars must never use callsite caches.
        if (!op_is_dynamic && call_node && g_runtime.resolve_cache_epoch != 0) {
            ID cached_call = ast_node_get_cached_resolution(call_node, op_sym, g_runtime.resolve_cache_epoch);
            if (cached_call) {
                return cached_call;
            }
        }
        
        // 3) Resolve-Cache (globaler Cache für Namespace-Symbole)
        // IMPORTANT: Dynamic vars must never use the resolve cache.
        EvalState *ctx_st = get_eval_state(ctx, st);
        if (!op_is_dynamic && g_runtime.resolve_cache && ctx_st) {
            CljSymbol *cache_ns_key = resolve_cache_ns_key_ctx(op_sym, ctx, ctx_st);
            ID cached = resolve_cache_lookup_value(cache_ns_key, op);
            if (cached) {
                // Populate callsite cache for future hits
                if (call_node) {
                    ast_node_update_callsite_cache(call_node, op_sym, cached, g_runtime.resolve_cache_epoch);
                }
                return cached;
            }
        }
    }

    // === COLD PATH: ctx fehlt oder Cache-Miss ===
    EvalContext local_ctx;
    CljList *owned_env_stack = NULL;
    const EvalContext *effective_ctx = ensure_eval_context(env, st, ctx, &local_ctx, &owned_env_stack);
    ID resolved = NULL;
    bool resolved_found = false;
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
    
    // Parameter validation: Check st and ctx before continuing
    if (!ctx_st || !effective_ctx) {
        // Fallback to namespace lookup if context not available
        resolved = eval_symbol(op_sym, ctx_st);
        ID return_value = resolved ? resolved : op;
        RELEASE(owned_env_stack);
        return return_value;
    }

    CljSymbol *cache_ns_key = resolve_cache_ns_key_ctx(op_sym, effective_ctx, ctx_st);
    bool allow_callsite_cache = call_node && op_sym && !op_is_dynamic && !resolve_stack && g_runtime.resolve_cache_epoch != 0;

    // OPTIMIZATION: Qualified symbols skip env_stack - go direct to namespace
    // Unqualified symbols check env_stack first (let bindings, closures)
    bool is_qualified = op_sym && op_sym->ns_name;
    
    // Check env_stack only if not qualified and stack exists
    if (!is_qualified && resolve_stack) {
        CljList *current = resolve_stack;
        while (current && list_type_matches(TAG(current))) {
            ID env_obj = LIST_FIRST(current);
            if (env_obj && TAG(env_obj) == CLJ_MAP) {
                ID found = map_get((CljMap*)env_obj, op);
                if (found != NOT_FOUND) {
                    resolved = found;
                    resolved_found = true;
                    break;
                }
            }
            ID rest = LIST_REST(current);
            current = (rest && list_type_matches(TAG(rest))) ? (CljList*)rest : NULL;
        }
    }

    if (resolved_found) {
        RELEASE(owned_env_stack);
        return resolved;
    }

    // Namespace lookup - this result can be cached
    // For qualified symbols, this is the primary lookup path
    // For unqualified symbols, this is the fallback after env_stack
    // Resolve operator in lexical namespace (ctx->resolve_ns) while keeping *ns* dynamic.
    resolved = eval_symbol_with_resolve_ns(op_sym, ctx_st, get_resolve_ns(effective_ctx, ctx_st));
    
    // Cache namespace lookups for future calls
    if (!op_is_dynamic && resolved && cache_ns_key && TAG(resolved) != CLJ_SYMBOL) {
        if (!g_runtime.resolve_cache) {
            ASSIGN(g_runtime.resolve_cache, make_map(RESOLVE_CACHE_SIZE));
        }
        if (g_runtime.resolve_cache) {
            resolve_cache_store_value(cache_ns_key, op, resolved);
            if (allow_callsite_cache) {
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
                ID result = map_get_sentinel((CljValue)arg, (CljValue)op, NULL);
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
            // Convert SYM_NIL to NULL (nil representation)
            return (result == SYM_NIL) ? NULL : result;
            // Exception propagates automatically - no cleanup needed!
        }

        if (fn_tag == CLJ_LIST) {
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call list as a function");
        }

        // If fn is still a symbol, it means eval_symbol couldn't resolve it as a function
        // Check if it's unquote-splice (only valid inside quasiquote)
        if (fn_tag == CLJ_SYMBOL) {
            CljSymbol *sym = as_symbol(fn);
            if (sym == SYM_UNQUOTE_SPLICE) {
                throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                        "unquote-splice can only be used inside quasiquote");
                return NULL;
            }
            const char *sym_name = sym && sym->cname ? sym->cname : "unknown";
            throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call %s as a function", sym_name);
            return NULL;
        }

        // Unknown type - should not happen, but throw exception to be safe
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call object of type %d as a function", fn_tag);
        return NULL;
    }

    // Direct function call
    if (op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE) {
        ID result = call_function_with_args_and_context(op, list, env, st, ctx);
        // Convert SYM_NIL to NULL (nil representation)
        return (result == SYM_NIL) ? NULL : result;
    }

    return NULL; // Not a function
}

static INLINE ID call_function_with_args_and_context(ID fn, CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    ID args[16];
    unsigned int argc = 0;

    // Hot path: Most calls have <= 2 args (fib, +, -, <, etc.)
    // Avoid a loop in the common case: unroll traversal for 0..2 args.
    CljList *arg0 = list ? as_list(LIST_REST(list)) : NULL;
    CljList *arg1 = arg0 ? as_list(LIST_REST(arg0)) : NULL;
    CljList *arg2 = arg1 ? as_list(LIST_REST(arg1)) : NULL;

    // 1 branch into slowpath: anything unusual (non-map env or >2 args)
    if (LIKELY(env && TAG(env) == CLJ_MAP && !arg2)) {
        if (arg0) {
            args[0] = eval_arg_from_expr_with_context(LIST_FIRST(arg0), env, st, ctx);
            argc = 1;
            if (arg1) {
                args[1] = eval_arg_from_expr_with_context(LIST_FIRST(arg1), env, st, ctx);
                argc = 2;
            }
        }
    } else if (env && TAG(env) == CLJ_MAP) {
        // Slow path: 3+ args (rare) - keep the generic loop
        CljList *current = arg0;
        while (current && argc < 16) {
            args[argc++] = eval_arg_from_expr_with_context(LIST_FIRST(current), env, st, ctx);
            current = as_list(LIST_REST(current));
        }
    }

    // Call function - do NOT mutate st->current_ns for closures.
    // Lexical symbol resolution is handled via EvalContext.resolve_ns inside eval_function_call.
    ID result = eval_function_call(fn, args, argc, env, st);

    if (result == SYM_NIL) {
        return NULL;
    }
    return AUTORELEASE(result);
}

// Internal: resolve symbols in the context of a specific lexical namespace.
// Keeps dynamic *ns* behavior (SYM_NS_STAR uses st->current_ns).
static INLINE ID eval_symbol_with_resolve_ns(CljSymbol *symbol, EvalState *st, CljNamespace *resolve_ns) {
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

    // Dynamic vars: if an earmuffed symbol is dynamically bound, return its binding.
    // Values may be NULL (nil) and are stored directly in binding maps.
    if (is_dynamic_var_symbol(symbol)) {
        ID bound = dynamic_binding_lookup(st, symbol);
        if (bound != NOT_FOUND) {
            return bound;
        }
    }

    // *ns* is represented as the current namespace object (dynamic).
    if (symbol == SYM_NS_STAR) {
        if (st && st->current_ns) {
            return (ID)st->current_ns;
        }
        return (ID)ns_get_or_create("user", NULL);
    }

    // Qualified symbols resolve independent of lexical namespace.
    if (symbol->ns_name && symbol->ns_name->cname) {
        return eval_symbol(symbol, st);
    }

    // Check special forms first - they return themselves
    if (is_special_symbol(symbol)) {
        return symbol;
    }

    // Builtins resolve via namespace (lexical resolve namespace)
    ID value = ns_resolve_in(resolve_ns, symbol);
    if (value != NOT_FOUND) {
        if (!value || value == SYM_NIL) {
            return NULL;
        }
        return value;
    }

    if (is_builtin_function(symbol)) {
        return symbol;
    }

    const char *cname = symbol->cname ? symbol->cname : "unknown";
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", cname);
    return NULL;
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
    
    // Check if op is an immediate value - if so, it's invalid for a list first element
    // This can happen if the macro expansion returned corrupted data
    CLJ_ASSERT(!IS_IMMEDIATE(op) && "Invalid list structure: first element is an immediate value");

    // If first element is a list, evaluate it first (for nested calls like ((array-map)))
    // CRITICAL: Pass ctx to preserve RecurContext
    if (op && list_type_matches(TAG(op))) {
        op = eval_list(as_list(op), effective_env, effective_st, ctx);
        if (!op) {
            // Evaluation returned nil (NULL) - cannot call nil as a function
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call nil as a function");
        }
        // Check if result is an immediate value (macro expansion may return incorrectly)
        if (IS_IMMEDIATE(op)) {
            const char *type_name = is_fixed((CljValue)op) ? "number" : (is_bool((CljValue)op) ? "boolean" : "immediate value");
            return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call %s as a function (this may indicate a macro expansion error)", type_name);
        }
    }

    // Handle maps as functions (for key lookup) - must be first
    if (op && TAG(op) == CLJ_MAP) {
        return eval_map_lookup(list, effective_env, effective_st, ctx, op);
    }

    // Check if op is a symbol and resolve it
    CljObject *original_op = op;
    unsigned char original_op_tag = op ? TAG(op) : 0;

#ifdef DEBUG
    if (original_op_tag == CLJ_SYMBOL) {
        const char *dbg = getenv("TINYCLJ_DEBUG_EVAL_LIST_OP");
        if (dbg && dbg[0] == '1' && (&g_clojure_core_last_form != NULL) && g_clojure_core_last_form == 210) {
            fprintf(stderr, "[debug] eval_list: form=%d list=%p op=%p tag=%u\n",
                    (int)g_clojure_core_last_form, (void*)list, (void*)op, (unsigned)original_op_tag);
            fflush(stderr);
        }
    }
#endif
    CljSymbol *original_op_sym = (original_op_tag == CLJ_SYMBOL) ? as_symbol(op) : NULL;

    // === HOT PATH: Callsite-Cache für gecachte Funktionen (fib, etc.) ===
    // Prüfe Cache VOR allen anderen Checks - überspringt komplette Resolution
    if (call_node && original_op_sym && g_runtime.resolve_cache_epoch != 0) {
        ID cached_fn = ast_node_get_cached_resolution(call_node, original_op_sym, 
                                                       g_runtime.resolve_cache_epoch);
        if (cached_fn) {
            unsigned char cached_tag = TAG(cached_fn);
            if (cached_tag == CLJ_FUNC || cached_tag == CLJ_CLOSURE) {
                // DIREKT zum Funktionsaufruf - komplette Resolution übersprungen!
                return eval_function_call_from_list(list, effective_env, effective_st, 
                                                    cached_fn, ctx);
            }
        }
    }

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

    // Fallback: Some runtimes may intern "try" before special symbols are registered,
    // resulting in a non-special symbol (flags unset). Treat it as a special form by name.
    if (original_op_sym && original_op_sym->cname && strcmp(original_op_sym->cname, "try") == 0) {
        return eval_special_try(list, effective_env, effective_st, ctx);
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
    ID (*seq_native)(ID*, unsigned int) = NULL;
    unsigned int seq_max_args = 0;
    if (original_op_sym == SYM_FIRST) { seq_native = native_first; seq_max_args = 1; }
    else if (original_op_sym == SYM_REST) { seq_native = native_rest; seq_max_args = 1; }
    else if (original_op_sym == SYM_CONS) { seq_native = native_cons; seq_max_args = 2; }
    else if (original_op_sym == SYM_SEQ) { seq_native = native_seq; seq_max_args = 1; }
    else if (original_op_sym == SYM_NEXT) { seq_native = native_next; seq_max_args = 1; }
    else if (original_op_sym == SYM_COUNT) { seq_native = native_count; seq_max_args = 1; }
    if (seq_native) {
        ID r = eval_and_call_native_with_context(list, effective_env, seq_native, seq_max_args, ctx);
        if (r) return r;
    }

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
        return eval_for(list, effective_env);
    }
    if (original_op_sym == SYM_DOSEQ) {
        return eval_doseq(list, effective_env);
    }
    if (original_op_sym == SYM_DOTIMES) {
        // OPTIMIZATION: Use thread-local EvalState instead of creating temporary
        EvalState *eval_st = effective_st ? effective_st : builtin_get_eval_state();
        return eval_dotimes(list, effective_env, eval_st, ctx);
    }

    // Try function call
    // CRITICAL: Only try to call if op is a symbol or function
    // If op is not a symbol or function, eval_function_call_from_list will return NULL
    // and we should treat it as an error (not a function call)
    unsigned char op_tag = op ? TAG(op) : 0;
    if (op && (op_tag == CLJ_SYMBOL || op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE)) {
        return eval_function_call_from_list(list, effective_env, effective_st, op, ctx);
    }

    // Safety check: ensure op is not NULL before accessing op->type
    CLJ_ASSERT(op != NULL && "op must not be NULL before accessing op->type");

    // Check if op is an immediate value (macro expansion may return incorrectly)
    if (IS_IMMEDIATE(op)) {
        const char *type_name = is_fixed((CljValue)op) ? "number" : (is_bool((CljValue)op) ? "boolean" : "immediate value");
        return throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call %s as a function (this may indicate a macro expansion error)", type_name);
    }

    // Error: op is a list (should have been evaluated earlier)
    if (list_type_matches(op_tag)) {
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
    ID value = NULL;
    if (value_expr) {
        value = list_type_matches(TAG(value_expr))
            ? eval_list(as_list(value_expr), eval_env, st, NULL)
            : eval_parsed(value_expr, st, eval_env);
    }
    // If value_expr is NULL, value remains NULL (nil case)
    // value can be NULL if nil was evaluated (legitimate case)
    // If evaluation failed, eval_list/eval_parsed should have thrown an exception

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

    // Resolve symbol once for reuse
    CljSymbol *sym = as_symbol(symbol);
    
    // If the value is a function, set its name and rewrite recursive calls
    // CRITICAL: Only for CLJ_CLOSURE (Clojure functions), not CLJ_FUNC (native functions)
    if (TAG(value) == CLJ_CLOSURE) {
        CljFunction *func = as_function(value);
        if (func && sym && sym->cname[0]) {
            if (!func->name) func->name = strdup(sym->cname);
            
            // Rewrite recursive calls to use qualified name (for TCO optimization)
            // Only if namespace is available (avoids unnecessary work)
            if (st->current_ns && st->current_ns->name) {
                CljSymbol *qualified = intern_symbol(st->current_ns->name, sym->cname);
                if (qualified && qualified != sym) {
                    rewrite_recursive_calls_in_slot((ID*)&func->body, sym, qualified);
                }
            }
        }
    }

    // Store in namespace (value can be NULL/nil - legitimate case)
    ns_define(st->current_ns, symbol, value);

    // Apply metadata to value
    // In Clojure, metadata from ^#^{...} (def ...) is applied to the value
#if defined(META_ENABLED) && META_ENABLED
    if (value) {
        unsigned char value_tag = TAG(value);
        ID form_meta = meta_get(list);
        
        // Optimized: Only process metadata for functions (most common case)
        // For non-functions, just copy form metadata if present
        if (value_tag == CLJ_CLOSURE || value_tag == CLJ_FUNC) {
            if (form_meta && TAG(form_meta) == CLJ_MAP) {
                CljMap *meta_map = (CljMap*)RETAIN(form_meta);
                // Add :name and :ns directly (map_assoc overwrites if present, :name/:ns are rare)
                if (SYM_KW_NAME && sym && sym->cname && sym->cname[0] != '\0') {
                    CljString *name_str = make_string(sym->cname);
                    if (name_str) {
                        ASSIGN(meta_map, map_assoc(meta_map, SYM_KW_NAME, name_str));
                        RELEASE(name_str);
                    }
                }
                if (SYM_KW_NS && st->current_ns && st->current_ns->name) {
                    ASSIGN(meta_map, map_assoc(meta_map, SYM_KW_NS, st->current_ns->name));
                }
                meta_set(value, meta_map);
                RELEASE(meta_map);
            }
        } else if (form_meta) {
            meta_set(value, form_meta);
        }
    }
#endif // META_ENABLED

    // Return the symbol key that was actually stored in the namespace mappings.
    // For non-core namespaces, ns_define qualifies & interns unqualified symbols
    // (e.g. user/test-fn). Returning that canonical key keeps (def ...) consistent
    // with direct map_get-based lookups in tests and tooling.
    CljSymbol *ret_sym = sym;
    if (st->current_ns && sym && sym->cname) {
        if (st->current_ns->name == SYM_CLOJURE_CORE) {
            ret_sym = intern_symbol_global(sym->cname);
        } else if (sym->ns_name && st->current_ns->name && sym->ns_name == st->current_ns->name) {
            ret_sym = sym;
        } else if (sym->ns_name && sym->ns_name->cname) {
            ret_sym = sym;
        } else if (st->current_ns->name && st->current_ns->name->cname) {
            ret_sym = intern_symbol(st->current_ns->name, sym->cname);
        }
    }
    return ret_sym;
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
    if (value == NOT_FOUND) {
        // Try to find the symbol in the current namespace mappings
        CljMap *mappings = st->current_ns->mappings;
        if (mappings) {
            value = map_get(mappings, sym_obj);
        }
    }

    if (value == NOT_FOUND) {
        eval_error("var: symbol not found", st);
        return NULL;
    }

    // Normalize SYM_NIL to runtime nil (NULL)
    if (value == SYM_NIL) {
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

    // Get potential function name (for named fn like (fn step [x] ...))
    CljList *rest1 = as_list(LIST_REST(list));     // nach 'fn'
    ID second = LIST_FIRST(rest1);        // name oder params
    CljSymbol *fn_name = NULL;
    ID params_list = NULL;
    ID body = NULL;

    // Check if second element is a symbol (named fn) but NOT a keyword
    // Also check it's not a vector (anonymous fn case)
    CljList *body_rest = NULL;  // Rest of body expressions (for multiple bodies)
    if (TAG(second) == CLJ_SYMBOL && !IS_KEYWORD(second)) {
        // Named fn: (fn name [params] body...)
        fn_name = (CljSymbol*)second;
        CljList *rest2 = as_list(LIST_REST(rest1));   // nach name
        params_list = LIST_FIRST(rest2);
        body_rest = as_list(LIST_REST(rest2));        // body expressions
        body = body_rest ? LIST_FIRST(body_rest) : NULL;
    } else {
        // Anonymous fn: (fn [params] body...)
        params_list = second;
        body_rest = as_list(LIST_REST(rest1));
        body = body_rest ? LIST_FIRST(body_rest) : NULL;
    }
    
    // Handle multiple body expressions: wrap in (do ...)
    // (fn [x] expr1 expr2) → body becomes (do expr1 expr2)
    if (body_rest && body_rest->rest) {
        // Multiple body expressions - wrap in do block
        body = make_list(SYM_DO, body_rest);
    }

    // Parameters can be a vector [a b] or a list (a b)
    bool is_vector_params = TAG(params_list) == CLJ_VECTOR;
    if (!params_list || (!list_type_matches(TAG(params_list)) && !is_vector_params)) {
        return NULL;
    }

    if (!body) {
        return NULL;
    }

    // Check if body is :native marker (for native function stubs)
    // Keywords are interned, so pointer comparison suffices
    if (body == (CljObject*)SYM_KW_NATIVE) {
        // Named fn required for :native lookup
        if (!fn_name || !fn_name->cname) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                           "fn with :native requires a function name",
                           NULL, 0, 0);
            return NULL;
        }
        
        // Lookup native function by name (try qualified first, then unqualified)
        BuiltinFn native_func = NULL;
        CljSymbol *lookup_symbol = fn_name;
        if (st && st->current_ns && st->current_ns->name) {
            CljSymbol *qualified = intern_symbol(st->current_ns->name, fn_name->cname);
            if (qualified) lookup_symbol = qualified;
        }
        native_func = native_function_lookup(lookup_symbol);
        if (!native_func && lookup_symbol != fn_name) {
            native_func = native_function_lookup(fn_name);
        }
        if (!native_func) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Native function not found for: %s", fn_name->cname);
            throw_exception(EXCEPTION_RUNTIME, error_msg, NULL, 0, 0);
            return NULL;
        }
        
        // Create and return native function object
        return AUTORELEASE(make_named_func(native_func, fn_name));
    }

    // Convert parameter list/vector to array
    // NOTE: Destructuring is handled at AST canonicalization time, so params are always symbols here
    int param_count = is_vector_params 
        ? vector_count(as_vector(params_list))
        : list_count(as_list(params_list));

    ID params_stack[16];
    ID *params = alloc_obj_array(param_count, params_stack);

    for (int i = 0; i < param_count; i++) {
        params[i] = is_vector_params
            ? vector_nth(as_vector(params_list), i)
            : list_get_element(as_list(params_list), i);
        if (TAG(params[i]) != CLJ_SYMBOL) {
            free_obj_array(params, params_stack);
            return NULL;
        }
    }

    // TCO: Transform recursive tail calls to recur for named functions
    if (fn_name && body) {
        CljObject *transformed = transform_recursive_tail_calls(body, (CljObject*)fn_name,
                                                                 (CljObject**)params, param_count, body);
        if (transformed) {
            body = transformed;
        }
    }

    // CRITICAL: Use env_stack from context if available (for closures)
    // This ensures that nested functions can access outer function parameters
    // OPTIMIZATION: Only create env_stack if we have a frame (lazy evaluation)
    // Most functions don't need env_stack, so we avoid the expensive frame_chain_to_env_stack call
    CljList *fn_env_stack = NULL;
    if (ctx && ctx->frame) {
        // Only create env_stack if we have a frame (indicates closure might be needed)
        // NOTE: This is a conservative approach - we create env_stack even if not strictly needed
        // A more aggressive optimization would analyze the body to detect outer scope references
        fn_env_stack = frame_chain_to_env_stack(ctx->frame, ctx->env_stack);
    } else if (ctx && ctx->env_stack) {
        // No frame, but we have env_stack (from outer closure)
        fn_env_stack = RETAIN(ctx->env_stack);
    } else {
        // Fallback: Create env_stack from env if provided, otherwise use namespace mappings
        CljMap *env_source = env;
        if (!env_source && st && st->current_ns) {
            env_source = st->current_ns->mappings;
        }
        fn_env_stack = env_source ? make_list(env_source, NULL) : NULL;
    }

    // Create function object
    CljFunction *fn = make_function(params, param_count, body, fn_env_stack, NULL, st ? st->current_ns : NULL);

    // If this is a named function, bind it to its own name in closure for recursion
    if (fn_name) {
        CljMap *self_binding = map_assoc(map_empty(), fn_name, fn);
        RELEASE(fn);  // Balance map_assoc's RETAIN
        
        // Rewrite recursive calls: For def, use qualified name (TCO optimization)
        // For let, keep unqualified (function not in namespace, so qualified lookup would fail)
        // The rewrite still happens to ensure recursive calls work correctly
        // Note: eval_def will do the qualified rewrite when the function is stored in namespace
        // Here we only handle the closure binding for let-based recursive functions
        
        fn->env_stack = make_list(self_binding, fn->env_stack);
    }

    free_obj_array(params, params_stack);

    return AUTORELEASE(fn);
}

// is_special_symbol is now in symbol.c (uses dynamic registration)
// This inline version was removed to use the centralized implementation

// Check if symbol is a builtin function (+, -, *, /, etc.)
// Uses compact array-based lookup for smaller code size
static INLINE bool is_builtin_function(CljSymbol *symbol) {
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

    // Dynamic vars: if an earmuffed symbol is dynamically bound, return its binding.
    // Values may be NULL (nil) and are stored directly in binding maps.
    if (is_dynamic_var_symbol(symbol)) {
        ID bound = dynamic_binding_lookup(st, symbol);
        if (bound != NOT_FOUND) {
            return bound;
        }
    }

    // *ns* is represented as the current namespace object.
    // This makes it dynamically bindable by updating EvalState.current_ns in (binding ...).
    if (symbol == SYM_NS_STAR) {
        if (st && st->current_ns) {
            return (ID)st->current_ns;
        }
        return (ID)ns_get_or_create("user", NULL);
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
            ID resolved = map_get(target_ns->mappings, symbol);
            if (resolved != NOT_FOUND) {
                // Found in target namespace - return it (can be NULL/nil, which is valid)
                return resolved;
            }
            CljSymbol *symbol_alias = NULL;
            if (symbol->cname) {
                symbol_alias = intern_symbol_global(symbol->cname);
            }
            if (symbol_alias && symbol_alias != symbol) {
                resolved = map_get(target_ns->mappings, symbol_alias);
                if (resolved != NOT_FOUND) {
                    return resolved;
                }
            }

            // If direct lookup failed, rely on namespace mappings to contain canonical keys.
        }

        // CLOJURE COMPATIBILITY: Qualified symbols require explicit (require 'namespace)
        // Native functions are only accessible via defn :native stubs in the Clojure source.
        // This ensures clojure.string/pad-left only works after (require 'clojure.string).

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
    // CRITICAL: All functions must be registered in the namespace (via register_builtins()
    // or via :native stubs in clojure.core.clj). No fallback to native_function_lookup
    // to avoid hiding errors where functions are used before they are defined.
    ID value = ns_resolve_in(st ? st->current_ns : NULL, symbol);
    if (value != NOT_FOUND) {
        // Normalize nil representations
        if (!value || value == SYM_NIL) {
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

    // CLOJURE COMPATIBILITY: Native functions (e.g. clojure.string/trim) are only
    // accessible after explicit (require 'clojure.string). The native_function_table
    // is only used by (defn ... :native) stubs, not by symbol resolution.

    // Symbol not found in namespace - this is an error
    // Functions must be registered via register_builtins() or :native stubs
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
            return arg;
        }

        default: {
            // For other seqable types, return SeqIterator directly
            CljSeqIterator *seq = (CljSeqIterator*)AUTORELEASE(make_seq(arg));
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

            WITH_AUTORELEASE_POOL({
                // Create new environment with binding using helper
                CljMap *new_env = extend_env_with_binding(env, var, element);
                if (new_env) {
                    // Evaluate body with new binding
                    ID body_result = eval_body_with_env(body, new_env, NULL);
                    RELEASE(body_result);

                    // Clean up environment
                    RELEASE(new_env);
                }
            });

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

            WITH_AUTORELEASE_POOL({
                // Create new environment with binding using helper
                CljMap *new_env = extend_env_with_binding(env, var, element);
                if (new_env) {
                    // Evaluate body with new binding
                    ID body_result = eval_body_with_env(body, new_env, NULL);
                    RELEASE(body_result);

                    // Clean up environment
                    RELEASE(new_env);
                }
            });


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
    // args_list is part of the list_data structure, which is already safe (caller has strong reference)
    return args_list;
}

// ============================================================================
// EVAL_LET - Let bindings implementation
// NOTE: Destructuring is handled at AST canonicalization time (ast_canon.c)
// using Clojure's (destructure ...) function. By the time we get here,
// all bindings are simple symbol-value pairs.
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
    CallFrame let_frame_storage;
    ID binding_slots[CALLFRAME_MAX_PARAMS * 2];
    if (has_frame) {
        CLJ_ASSERT(pair_count <= CALLFRAME_MAX_PARAMS && "Too many let bindings");
        let_frame = &let_frame_storage;
        frame_init(let_frame, ctx ? ctx->frame : NULL);
        binding_params = binding_slots;
        binding_values = binding_slots + pair_count;
    }

    EvalContext let_ctx = ctx ? *ctx : (EvalContext){0};
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
            if (parent_stack_owned) RELEASE(parent_stack);
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
                RETAIN(value);
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
                RELEASE(frame_env_stack);
            }
        }

        if (value && !IS_IMMEDIATE(value)) {
            RELEASE(value);
        }

        binding_index++;
    }

    if (has_frame) {
        // Keep frame for direct symbol resolution; avoid materializing a heap env_stack for the let body.
        // Closures created inside let still get a captured env_stack via frame_chain_to_env_stack at bind time.
        let_ctx.frame = let_frame;
        let_ctx.env_stack = parent_stack;
    }

    ID result = NULL;
    int list_len = list_count(list);
    if (list_len > 2) {
        CljMap *body_env = let_ctx.env ? let_ctx.env : env;
        for (int i = 2; i < list_len; i++) {
            ID body_expr = list_get_element(list, i);
            if (!body_expr) continue;

            RELEASE(result);
                if (is_fixnum((CljValue)body_expr) || is_special((CljValue)body_expr)) {
                    result = body_expr;
                RETAIN(result);
                } else {
                result = eval_body(body_expr, body_env, st, &let_ctx);
            }
        }
    }

    if (has_frame) {
        frame_release(let_frame);
    }
    if (parent_stack_owned) RELEASE(parent_stack);
    return result;
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
        return expr;
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
                if (frame_value == NOT_FOUND) {
                    return NULL;  // Parameter bound to nil
                }
                if (!frame_value) return NULL;
                if (IS_IMMEDIATE(frame_value)) return frame_value;
                return AUTORELEASE(RETAIN(frame_value));
            }
        }
        
        ID resolved_value = NULL;
        bool resolved_found = false;
        
        // If context is provided with env_stack, use resolve_symbol_in_env
        // to search through the entire environment stack (for nested let blocks)
        if (ctx) {
            EvalState *eval_st = get_eval_state(ctx, st);
            ID resolved_id = resolve_symbol_in_env_with_frame(ctx->env_stack, env, ctx->frame, expr, eval_st);
            if (resolved_id != NOT_FOUND) {
                if (!resolved_id || resolved_id == SYM_NIL) {
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
                resolved_found = true;
            }
            // Not found in env_stack or still a symbol, fall through to namespace resolution
        }
        
        if (!resolved_value && env && TAG(env) == CLJ_MAP) {
            // Use sentinel to distinguish "key not found" from "value is nil"
            ID resolved_id = map_get(env, expr);
            if (resolved_id != NOT_FOUND) {
                // Key exists in map (value may be NULL/nil)
                // map_get returns retained value, eval_arg should return AUTORELEASE
                if (!resolved_id) return NULL; // nil
                resolved_value = resolved_id;
                resolved_found = true;
            }
        }

        if (!resolved_value) {
            // Dynamic vars may be bound to nil; allow use in argument position.
            if (st) {
                CljSymbol *sym = as_symbol(expr);
                if (sym && is_dynamic_var_symbol(sym)) {
                    ID bound = dynamic_binding_lookup(st, sym);
                    if (bound != NOT_FOUND) {
                        if (!bound) return NULL;
                        if (IS_IMMEDIATE(bound)) return bound;
                        return AUTORELEASE(RETAIN(bound));
                    }
                }
            }

            CljNamespace *resolve_ns = get_resolve_ns(ctx, st);
            ID resolved = ns_resolve_in(resolve_ns, as_symbol(expr));
            if (resolved != NOT_FOUND) {
                resolved_found = true;
                if (!resolved || resolved == SYM_NIL) {
                    return NULL;
                }
                resolved_value = resolved;
            }
        }

        if (resolved_found) {
            if (!resolved_value) {
                return NULL;
            }
            if (IS_IMMEDIATE(resolved_value)) {
                return resolved_value;
            }
            // resolved_value can come from map_get (pointer) or ns_resolve (safe)
            // map_get returns only pointer, ns_resolve returns safe value
            // Since we can't distinguish, use RETAIN+AUTORELEASE for safety
            return AUTORELEASE(RETAIN(resolved_value)); // question this!!
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

    if (expr_tag == CLJ_VECTOR || expr_tag == CLJ_VECTOR_TRANSIENT || expr_tag == CLJ_VECTOR_TRANSIENT_WEAK) {
        CljVector *vec = (CljVector*)expr;
        unsigned int count = vector_count(vec);
        if (count == 0) return expr;
        
        CljVector *result = make_vector(count, CLJ_VECTOR);
        RETAIN(result);
        VECTOR_FOR_EACH(vec, elem) {
            ID eval_elem = (elem && elem != SYM_NIL) ? eval_body(elem, env, st, ctx) : NULL;
            ASSIGN(result, vector_conj(result, eval_elem));
        }
        return AUTORELEASE(result);
    }

    return expr;
}

ID eval_dotimes(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CLJ_ASSERT(env != NULL);
    // (dotimes [var n] expr)
    // Executes expr n times with var bound to 0, 1, ..., n-1

    if (!list) {
        return NULL;
    }

    // Parse arguments directly without evaluation
    CljList *list_data = as_list(list);
    if (!list_data || !list_data->rest) return NULL;

    // Extract binding vector/list (first argument after dotimes) and body forms.
    CljList *args_list = as_list(list_data->rest);
    if (!args_list) return NULL;
    ID binding_list = LIST_FIRST(args_list);
    ID body_list = LIST_REST(args_list); // list of body expressions (may be NULL)

    if (!binding_list) return NULL;

    // Parse binding: [var n] - support both vectors and lists
    ID var = NULL;
    ID n_obj = NULL;

    if (TAG(binding_list) == CLJ_VECTOR) {
        CljVector *vec = as_vector(binding_list);
        if (!vec || vector_count(vec) < 2) return NULL;
        var = vector_nth(vec, 0);
        n_obj = vector_nth(vec, 1);
    } else if (list_type_matches(TAG(binding_list))) {
        CljList *binding_data = as_list(binding_list);
        if (!binding_data || !binding_data->first) return NULL;
        var = binding_data->first;
        CljList *rest_list = as_list(binding_data->rest);
        if (!rest_list || !rest_list->first) return NULL;
        n_obj = rest_list->first;
    } else {
        return NULL;
    }

    if (!var || TAG(var) != CLJ_SYMBOL || !n_obj) return NULL;

    EvalContext local_ctx = {0};
    CljList *owned_stack = NULL;
    const EvalContext *effective_ctx = ensure_eval_context(env, st, ctx, &local_ctx, &owned_stack);

    // Evaluate n (once) using the current lexical context.
    ID n_evaluated = eval_arg_from_expr_with_context(n_obj, env, st, effective_ctx);

    if (!n_evaluated || TAG(n_evaluated) != CLJ_INT) return NULL;
    int n = as_fixnum((CljValue)n_evaluated);
    if (n <= 0) return AUTORELEASE(NULL);

    // Loop var binding: use a stack CallFrame to avoid allocating a new map/env each iteration.
    CallFrame dotimes_frame;
    frame_init(&dotimes_frame, effective_ctx ? effective_ctx->frame : NULL);
    ID dotimes_params[1] = { var };
    dotimes_frame.params = dotimes_params;
    dotimes_frame.param_count = 1;

    // Evaluate body with context so symbol lookup hits frame_lookup.
    EvalContext dotimes_ctx = effective_ctx ? *effective_ctx : (EvalContext){0};
    dotimes_ctx.env = dotimes_ctx.env ? dotimes_ctx.env : env;
    dotimes_ctx.frame = &dotimes_frame;

    EvalState *eval_st = st ? st : builtin_get_eval_state();
    dotimes_ctx.st = eval_st;

    for (int i = 0; i < n; i++) {
        dotimes_frame.values[0] = frame_encode_value(fixnum((int32_t)i));

        WITH_AUTORELEASE_POOL({
            ID body_result = NULL;
            if (body_list && list_type_matches(TAG(body_list))) {
                CljList *body_items = as_list(body_list);
                LIST_FOR_EACH(body_items, body_expr) {
                    if (!body_expr) continue;
                    RELEASE(body_result);
                    body_result = eval_body(body_expr, env, eval_st, &dotimes_ctx);
                }
            } else if (body_list) {
                body_result = eval_body(body_list, env, eval_st, &dotimes_ctx);
            }
            RELEASE(body_result);
        });
    }

    frame_release(&dotimes_frame);
    if (owned_stack) RELEASE(owned_stack);
    return AUTORELEASE(NULL);
}

// ============================================================================
// EVAL_TIME - Time measurement special form implementation
// ============================================================================
ID eval_time(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
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

    // Evaluate the expression in the current lexical context.
    // Use eval_env so namespace-bound symbols (e.g. +) are available.
    ID result = eval_body((ID)expr, eval_env, st, ctx);

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
 * @brief Evaluate a parsed CljValue (handles immediate values and heap objects)
 * @param parsed The parsed CljValue (can be immediate or heap object)
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 * 
 * This is a DRY helper used by both eval_string and eval_multiform_string.
 */
ID eval_parsed_value(CljValue parsed, EvalState *eval_state) {
    // Check if parsed is an immediate value
    if (IS_IMMEDIATE(parsed)) {
        // For immediate values, return them as CljObject* (they're already evaluated)
        return parsed;
    }

    // For heap objects, evaluate them (use NULL env to use current_ns->mappings)
    ID result = eval_parsed(parsed, eval_state, NULL);

    // Convert SYM_NIL to NULL (nil representation)
    if (result == SYM_NIL) {
        return NULL;
    }

    // result can be NULL only if the evaluation result is nil
    // If eval_parsed fails, it should throw an exception, not return NULL
    return result;
}

/**
 * @brief Parse and evaluate a Clojure expression from a string (convenience)
 * @param expr_str The Clojure expression as a string
 * @param eval_state The evaluation state
 * @return The evaluated result (autoreleased) or NULL only if result is nil
 */
ID eval_string(const char* expr_str, EvalState *eval_state) {
    CLJ_ASSERT(expr_str != NULL);
    CLJ_ASSERT(eval_state != NULL);

    Reader reader;
    reader_init(&reader, expr_str);
    reader_set_source_name(&reader, "<string input>");

    CljValue parsed = parse_from_reader(&reader, eval_state);
    if (parsed == NULL) {
        throw_exception(EXCEPTION_PARSE, "Failed to parse expression", __FILE__, __LINE__, 0);
        return NULL;
    }

    return eval_parsed_value(parsed, eval_state);
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



