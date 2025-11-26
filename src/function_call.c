/*
 * Function Call Implementation
 * 
 * Simplified function call system for Tiny-Clj:
 * - eval_function_call: Main function call evaluator
 * - eval_body: Evaluate function body expressions
 * - eval_list: Evaluate list expressions
 * - Built-in function evaluators (add, sub, mul, div, println)
 * - Stack-allocated argument handling
 */

#include "common.h"
#include "object.h"
#include "function_call.h"
#include "symbol.h"
#include "exception.h"
#include "function.h"
#include "validation.h"
#include "builtins.h"
#include "optimize.h"
#include "parser.h"  // For eval_parsed

#include "error_messages.h"
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "seq.h"
#include "namespace.h"
#include "memory.h"
#include "meta.h"
#include "list.h"
#include "value.h"
#include "environment.h"
#include "vector.h"
#include "event_loop.h"
#include "channel.h"
#include "strings.h"

// Use C stack for recur state - each function call has its own stack frame
// No global variables needed - local variables in eval_function_call are automatically isolated

// Sentinel value to distinguish "key not found" from "value is nil"
// This is a unique pointer that will never appear as a real value in a map
static CljObject not_found = { .type = CLJ_NIL, .rc = SINGLETON_RC };

// Evaluation context structures are defined in function_call.h

#include "map.h"
#include "kv_macros.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

// Global variable to suppress time output in tests
static bool g_suppress_time_output = false;

// Function to set time output suppression (for tests)
void set_suppress_time_output(bool suppress) {
    g_suppress_time_output = suppress;
}

// Forward declarations  
ID eval_body_with_params(ID body, const EvalContext *ctx);
ID eval_time(CljList *list, CljMap *env, EvalState *st);



// Forward declarations for loop evaluation
ID eval_body_with_env(ID body, CljMap *env, EvalState *st);

// Helper: Evaluate symbol in environment with namespace fallback
static CljObject* eval_symbol_in_env(CljObject *sym, CljMap *env, EvalState *st);
// Helper: Add namespace mappings to environment
static CljMap* env_add_namespace_mappings(CljMap *env, EvalState *st);


// ============================================================================
// COMPARISON OPERATORS REFACTORING - Type Promotion and Generic Functions
// ============================================================================

// Macros for common argument evaluation patterns
#define EVAL_TWO_ARGS(list, env, a, b) do { \
    (a) = eval_arg(as_list(list), 1, (env), NULL); \
    (b) = eval_arg(as_list(list), 2, (env), NULL); \
    if (!(a) || !(b)) { \
        RELEASE(a); \
        RELEASE(b); \
        return NULL; \
    } \
} while(0)

#define RELEASE_TWO_ARGS_SAFE(a, b) do { \
    RELEASE(a); \
    RELEASE(b); \
} while(0)

// Legacy macro for backward compatibility (use RELEASE_TWO_ARGS_SAFE instead)
#define RELEASE_TWO_ARGS(a, b) RELEASE_TWO_ARGS_SAFE(a, b)

#define EVAL_AND_CHECK_TWO_ARGS(list, env, a, b) do { \
    EVAL_TWO_ARGS(list, env, a, b); \
} while(0)

typedef enum { COMP_LT, COMP_GT, COMP_LE, COMP_GE, COMP_EQ } ComparisonOp;

/**
 * @brief Extract numeric values from CljObjects with type promotion to float
 * @param a First object
 * @param b Second object  
 * @param val_a Output: promoted value of a
 * @param val_b Output: promoted value of b
 * @return true if both objects are numeric, false otherwise
 */
static bool extract_numeric_values(CljObject *a, CljObject *b, float *val_a, float *val_b) {
    // Extract value from first object
    switch (TAG(a)) {
        case CLJ_INT:
            *val_a = (float)as_fixnum((CljValue)a);
            break;
        case CLJ_FLOAT:
            *val_a = as_fixed((CljValue)a);
            break;
        default:
            return false; // Invalid type
    }
    
    // Extract value from second object
    switch (TAG(b)) {
        case CLJ_INT:
            *val_b = (float)as_fixnum((CljValue)b);
            break;
        case CLJ_FLOAT:
            *val_b = as_fixed((CljValue)b);
            break;
        default:
            return false; // Invalid type
    }
    
    return true;
}

/**
 * @brief Perform numeric comparison with type promotion
 * @param a First object
 * @param b Second object
 * @param op Comparison operation
 * @return true if comparison is true, false otherwise
 */
static bool compare_numeric_values(CljObject *a, CljObject *b, ComparisonOp op) {
    float val_a, val_b;
    
    if (!extract_numeric_values(a, b, &val_a, &val_b)) {
        return false; // Invalid types
    }
    
    // Single comparison logic
    switch (op) {
        case COMP_LT: return val_a < val_b;
        case COMP_GT: return val_a > val_b;
        case COMP_LE: return val_a <= val_b;
        case COMP_GE: return val_a >= val_b;
        case COMP_EQ: return val_a == val_b;
        default: return false;
    }
}

/**
 * @brief Generic numeric comparison function for all comparison operators
 * @param list The list containing the comparison expression
 * @param env The environment
 * @param op The comparison operation to perform
 * @return CljObject* The result (true/false) or NULL on error
 */
static CljObject* eval_numeric_comparison(CljList *list, CljMap *env, ComparisonOp op) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *a, *b;
    EVAL_TWO_ARGS(list, env, a, b);
    
    bool result = compare_numeric_values(a, b, op);
    
    if (!result && op != COMP_EQ) {
        // Check if it's a type error (not just a false comparison)
        float val_a, val_b;
        if (!extract_numeric_values(a, b, &val_a, &val_b)) {
            RELEASE_TWO_ARGS(a, b);
            throw_exception_formatted("TypeError", __FILE__, __LINE__, 0, ERR_EXPECTED_NUMBER);
            return NULL;
        }
        // It's a valid comparison that returned false
        result = false;
    }
    
    RELEASE_TWO_ARGS(a, b);
    return result ? clj_true : clj_false;
}


/** @brief Allocate array with stack optimization (size <= 16 on stack, else heap) */
static inline ID* alloc_obj_array(int size, CljObject **stack_buffer) {
    return size <= 16 ? (ID*)stack_buffer : (ID*)malloc(sizeof(CljObject*) * size);
}

/** @brief Free array allocated with alloc_obj_array */
static inline void free_obj_array(ID *array, CljObject **stack_buffer) {
    if (array != (ID*)stack_buffer) free((void*)array);
}

/** @brief Get raw nth element from a list (0=head). Returns NULL if out of bounds */
static inline CljObject* list_get_element(CljList *list, int index) {
    if (!list || index < 0) return NULL;
        CljList *node = list;
    if (index == 0) return LIST_FIRST(node);
    int i = 0;
    while (i < index) {
        CljObject *rest = LIST_REST(node);
        if (!rest || TAG(rest) != CLJ_LIST) return NULL;
        node = as_list(rest);
        i++;
    }
    return LIST_FIRST(node);
}

// Arithmetic operation types
typedef enum {
    ARITH_ADD, ARITH_SUB, ARITH_MUL, ARITH_DIV
} ArithOp;


// Helper function to check if a type is numeric
static bool is_numeric_type(CljObject *obj) {
    if (!obj) return false;
    return IS_IMMEDIATE(obj);
}

// Helper function to throw unresolved symbol exception (DRY principle)
static void throw_unresolved_symbol_exception(const char *sym_name) {
    throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
        "Unable to resolve symbol: %s in this context", sym_name);
}

/** @brief Generic arithmetic function (variadic version) */
CljObject* eval_arithmetic_generic(CljList *list, CljMap *env, ArithOp op, EvalState *st) {
    // Clojure-compatible: Accept NULL environment - eval_arg handles it
    // and falls back to namespace lookup for symbol resolution
    // CRITICAL: st is now used by eval_arg for namespace resolution
    int total_count = list_count(list);
    int argc = total_count - 1;  // Subtract 1 for the operator
    
    if (argc == 0) {
        // Handle zero arguments case
        switch (op) {
            case ARITH_ADD:
                return fixnum(0);  // (+) → 0
            case ARITH_MUL:
                return fixnum(1);  // (*) → 1
            case ARITH_SUB:
            case ARITH_DIV:
                throw_exception_formatted("ArityError", __FILE__, __LINE__, 0,
                    "Wrong number of args: 0");
                return NULL;
        }
    }
    
    // Evaluate all arguments
    CljObject **args = (CljObject**)malloc(sizeof(CljObject*) * argc);
    if (!args) return NULL;
    
    for (int i = 0; i < argc; i++) {
        args[i] = eval_arg(list, i + 1, env, st);
        if (!args[i]) {
            // Clean up already evaluated arguments
            for (int j = 0; j < i; j++) {
                RELEASE(args[j]);
            }
            free(args);
            return NULL;
        }
        
        // Check for nil arguments
        // Note: nil is now represented as NULL, so no special nil check needed
        
        // Check for non-numeric types
        if (!is_numeric_type(args[i])) {
            // Clean up already evaluated arguments BEFORE throwing exception
            for (int j = 0; j <= i; j++) {
                RELEASE(args[j]);
            }
            free(args);
            throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "String cannot be used as a Number");
            return NULL; // Unreachable, but prevents fallthrough
        }
    }
    
    // Call the appropriate variadic function
    CljObject *result = NULL;
    switch (op) {
        case ARITH_ADD:
            result = (CljObject*)native_add_variadic((ID*)(void**)args, argc);
            break;
        case ARITH_SUB:
            result = (CljObject*)native_sub_variadic((ID*)(void**)args, argc);
            break;
        case ARITH_MUL:
            result = (CljObject*)native_mul_variadic((ID*)(void**)args, argc);
            break;
        case ARITH_DIV:
            result = (CljObject*)native_div_variadic((ID*)(void**)args, argc);
            break;
    }
    
    // Clean up arguments
    for (int i = 0; i < argc; i++) {
        RELEASE(args[i]);
    }
    free(args);
    
    // AUTORELEASE handles immediates and NULL safely - no checks needed
    return AUTORELEASE(result);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st) {
    // Note: env parameter is used for environment context, but closure_env takes precedence
    // for Clojure functions. For native functions, env is not used.
    (void)env; // Suppress unused parameter warning
    
    unsigned char fn_tag = TAG(fn);
    if (fn_tag != CLJ_FUNC && fn_tag != CLJ_CLOSURE) {
        throw_exception(EXCEPTION_TYPE, "Attempt to call non-function value", NULL, 0, 0);
        return NULL;
    }
    
    // Check if it's a native function (CljCFunc) or Clojure function (CljFunction)
    if (is_native_fn(fn)) {
        // It's a native C function (CljCFunc)
        CljCFunc *native_func = (CljCFunc*)fn;
        if (!native_func || !native_func->fn) {
            throw_exception(EXCEPTION_TYPE, "Invalid native function", NULL, 0, 0);
            return NULL;
        }
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
    
    // Note: No autorelease pool here - let the calling context handle memory management
    // This prevents stack overflow in deep recursion while still allowing proper cleanup
    
    // Clojure functions with parameters are now supported
    
    // TCO Loop for tail-call optimization with recur
    // Use C stack for recur state - local variables are automatically isolated per function call
    ID current_args[16] = {NULL};  // Initialize to NULL
    int current_argc = argc;
    
    // Local recur state for THIS function call (on C stack)
    ID recur_args[16] = {NULL};  // Max 16 arguments, initialized to NULL
    int recur_arg_count = -1;  // -1 = kein Tail Call, >= 0 = Tail Call erkannt
    
    // Copy initial arguments
    // CRITICAL: args are already retained by call_function_with_args, so we just copy them
    for (int i = 0; i < argc && i < 16; i++) {
        current_args[i] = args[i];
    }
    
    // CRITICAL: Extend closure environment with parameter bindings
    // This ensures that when eval_list is called, it can find parameters in the environment
    // Get parameter array pointer directly (no copying needed)
    ID *params_array = vector_as_array(func->params);
    
    CljMap *call_env = NULL;
    if (func->closure_env) {
        // Use env_extend_stack to add parameters to the environment
        call_env = env_extend_stack(func->closure_env, params_array, (ID*)current_args, current_argc);

    } else {
        // No closure environment - create new environment with parameters
        call_env = env_extend_stack(NULL, params_array, (ID*)current_args, current_argc);

    }
    
    // TCO Loop - iterate on recur
    ID result = NULL;
    do {
        // Reset recur state for each iteration
        recur_arg_count = -1;  // -1 = kein Tail Call
        
        // CRITICAL: Clean up any leftover recur_args from previous iteration or exception
        // This ensures that if an exception occurred in the previous iteration, recur_args are freed
        // Check all 16 slots to ensure nothing is left over
        for (int i = 0; i < 16; i++) {
            RELEASE(recur_args[i]);
            recur_args[i] = NULL;
        }
        
        // Evaluate function body with context
        ParamContext param_ctx = {.params = params_array, .values = current_args, .param_count = current_argc};
        EvalEnv env_ctx = {.closure_env = call_env, .st = st};
        RecurContext recur_ctx = {.recur_args = recur_args, .recur_arg_count = &recur_arg_count};
        EvalContext ctx = {.params = &param_ctx, .env = &env_ctx, .recur = &recur_ctx};
        // CRITICAL: No TRY/CATCH needed here - exceptions are caught by outer handlers
        // If an exception is thrown, longjmp will jump to the outer handler and this function
        // will never return, so the loop will not continue
        ID new_result = eval_body_with_params(func->body, &ctx);
        
        // Check if recur was triggered in THIS function
        // With C stack, nested functions have their own stack frames, so recur_arg_count
        // only changes if recur was used in THIS function
        if (recur_arg_count >= 0) {
            // Tail Call erkannt - recur was used in THIS function
            CLJ_ASSERT(recur_arg_count <= 16);  // Assertion für max 16 Argumente
            // CRITICAL: Release intermediate result from recur iteration
            // RELEASE handles NULL and immediates automatically
            RELEASE(new_result);
            
            // Update argc and copy new arguments from recur_args
            CLJ_ASSERT(recur_arg_count >= 0 && recur_arg_count <= 16);  // Validierung
            current_argc = recur_arg_count;
            for (int i = 0; i < current_argc; i++) {
                CLJ_ASSERT(i < 16 && i < recur_arg_count);  // Array-Zugriff-Validierung
                current_args[i] = recur_args[i]; // Already retained in recur evaluation
                recur_args[i] = NULL; // Clear to prevent double-release
            }
            
            // CRITICAL: Recreate call_env with new arguments for recur iteration
            // eval_body_with_params uses call_env for parameter lookups, so we must update it
            CljMap *new_call_env = env_extend_stack(
                func->closure_env, 
                params_array, 
                (ID*)current_args, 
                current_argc
            );
            // Use ASSIGN to safely replace call_env (releases old, retains new)
            ASSIGN(call_env, new_call_env);
            
            // Continue loop - recur_arg_count will be reset at the start of the next iteration
            continue;
        }
        
        // No recur - this is the final result
        // Use ASSIGN for proper refcounting (handles retain/release automatically)
        ASSIGN(result, new_result);
        break;
    } while (true);
    
    // CRITICAL: Do NOT release current_args[i] here!
    // current_args[i] is stored in call_env, and call_env holds a reference to it.
    // If we release current_args[i] here, the object might be freed, but call_env
    // still holds a pointer to it. When call_env is released later, RELEASE will
    // be called on the already-freed object, causing a use-after-free error.
    // The call_env will be released below, which will properly release all stored values.
    
    // Cleanup recur args (if any were set but not used)
    // Check all 16 slots to ensure nothing is left over
    for (int i = 0; i < 16; i++) {
        RELEASE(recur_args[i]);
    }
    
    // Cleanup call_env (created by env_extend_stack)
    // This will properly release all stored values (including current_args[i])
    RELEASE(call_env);
    
    return result;
}


// Helper: Evaluate symbol in environment with namespace fallback
static CljObject* eval_symbol_in_env(CljObject *sym, CljMap *env, EvalState *st) {
    if (!sym || TAG(sym) != CLJ_SYMBOL) {
        return NULL;
    }
    
    if (env && TAG(env) == CLJ_MAP) {
        ID resolved = map_get(env, sym, NULL);
        if (resolved) {
            return (CljObject*)resolved;
        }
    }
    
    if (st) {
        ID resolved_ns = eval_symbol(as_symbol(sym), st);
        if (resolved_ns) {
            return (CljObject*)resolved_ns;
        }
    }
    
    return NULL;
}

// Helper: Add namespace mappings to environment
// Optimized: Uses map_get instead of map_contains, only iterates over actual entries
static CljMap* env_add_namespace_mappings(CljMap *env, EvalState *st) {
    if (!env || !st || !st->current_ns || !st->current_ns->mappings) {
        return env;
    }
    
    CljMap *ns_mappings = (CljMap*)st->current_ns->mappings;
    int ns_count = ns_mappings->count;
    if (ns_count == 0) {
        return env;
    }
    
    CljMap *result = env;
    bool changed = false;
    
    // Only iterate over actual entries, not full capacity
    for (int i = 0; i < ns_mappings->capacity; i++) {
        CljValue key = ns_mappings->data[i * 2];
        if (!key) continue; // Skip empty slots
        
        // Use map_get to check existence (more efficient than map_contains)
        ID existing = map_get(result, key, NULL);
        if (!existing) {
            CljValue val = ns_mappings->data[i * 2 + 1];
            CljMap *updated = map_assoc(result, key, val);
            if (updated != result) {
                if (changed && result != env) {
                    RELEASE(result);
                }
                result = updated;
                changed = true;
                RETAIN(result);
            }
        }
    }
    
    return result;
}

// Evaluate body with environment lookup (for loops)
ID eval_body_with_env(ID body, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
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
        
        case CLJ_LIST: {
            // Type check before calling
            if (!body_obj || TAG(body_obj) != CLJ_LIST) return NULL;
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
    
    // Assertion: Parameters and values must not be NULL when param_count > 0
    if (ctx->params && ctx->params->param_count > 0) {
        assert(ctx->params->params != NULL);
        assert(ctx->params->values != NULL);
    }
    
    if (body && TAG(body) == CLJ_SYMBOL) {
        // Resolve symbol - check environment map first (contains parameter bindings)
        // OPTIMIZATION: Use environment map directly instead of iterating over arrays
        // The environment map already contains all parameter bindings (created by env_extend_stack)
        CljSymbol *body_sym = as_symbol(body);
        // CRITICAL: body_sym should never be NULL if body is a symbol, but check for safety
        if (!body_sym) {
            // This should never happen, but if it does, try to resolve from namespace
            if (ctx->env && ctx->env->st) {
                CljObject *resolved = ns_resolve(ctx->env->st, body_sym);
                // ns_resolve returns retained values - object survives until pool-pop
                return resolved;
            }
            throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                "Unable to resolve symbol: invalid symbol object");
            return NULL;
        }
        
        // OPTIMIZATION: Check environment map first (contains parameter bindings)
        // This eliminates O(n) array iteration - map lookup is O(1) for pointer equality
        if (ctx->env && ctx->env->closure_env) {
            // Check if key exists in closure_env (even if value is nil/NULL)
            // Use sentinel to distinguish "key not found" from "value is nil"
            ID resolved_id = map_get(ctx->env->closure_env, body, &not_found);
            if (resolved_id != &not_found) {
                // map_get returns ID (can be object or immediate)
                // CRITICAL: resolved_id can be NULL (nil), which is valid in Clojure
                // We need to distinguish between nil (valid) and not found (error)
                // Sentinel check already verified that the key exists, so resolved_id can be NULL (nil)
                
                // CRITICAL: If resolved_id is a symbol, it means the symbol wasn't properly resolved
                // This can happen if a parameter is stored as a symbol in closure_env instead of its value
                // In this case, we should throw an exception instead of returning the symbol
                if (resolved_id && !IS_IMMEDIATE(resolved_id) && TAG(resolved_id) == CLJ_SYMBOL) {
                    // Symbol found in closure_env but value is also a symbol - this is an error
                    CljSymbol *sym = as_symbol(body);
                    const char *sym_name = sym && sym->name ? sym->name : "unknown";
                    throw_unresolved_symbol_exception(sym_name);
                    return NULL;
                }
                
                // map_get returns retained values - object survives until pool-pop
                return resolved_id;
            }
        }
        // If closure_env is not available, check ParamContext arrays directly
        // This is needed when closure_env is NULL (e.g., in tests)
        if (ctx->params && ctx->params->param_count > 0 && ctx->params->params && ctx->params->values) {
            // Iterate through parameter arrays to find matching symbol
            for (int i = 0; i < ctx->params->param_count; i++) {
                if (ctx->params->params[i] == body) {
                    // Found matching parameter - return its value
                    ID param_value = ctx->params->values[i];
                    // param_value is already retained - object survives until pool-pop
                    return param_value;
                }
            }
        }
        // Check if symbol is a keyword - keywords evaluate to themselves
        if (IS_KEYWORD(body)) {
            // Keywords evaluate to themselves (singletons need no memory management)
            return body;
        }
        // Special case: nil should evaluate to NULL (not SYM_NIL)
        if (body == SYM_NIL) {
            // nil is represented as NULL - return NULL directly
            return NULL;
        }
        // If still not found, try namespace lookup (for recursive function calls)
        // ns_resolve takes CljObject* (only objects, not immediates) and returns ID
        // body is a symbol (CljObject*), so we can pass it directly
        if (ctx->env && ctx->env->st) {
            ID resolved_id = ns_resolve(ctx->env->st, as_symbol(body));
            if (resolved_id) {
                // CRITICAL: If resolved_id is a symbol, it means the symbol wasn't properly resolved
                // This can happen if a symbol is stored in namespace instead of its value
                // In this case, we should throw an exception instead of returning the symbol
                if (!IS_IMMEDIATE(resolved_id) && TAG(resolved_id) == CLJ_SYMBOL) {
                    // Symbol found in namespace but value is also a symbol - this is an error
                    CljSymbol *sym = as_symbol(body);
                    const char *sym_name = sym && sym->name ? sym->name : "unknown";
                    throw_unresolved_symbol_exception(sym_name);
                    return NULL;
                }
                // ns_resolve returns retained values - object survives until pool-pop
                return resolved_id;
            }
        }
        // Symbol not found - throw exception
        CljSymbol *sym_obj = as_symbol(body);
        const char *sym_name_final = sym_obj && sym_obj->name ? sym_obj->name : "unknown";
        throw_unresolved_symbol_exception(sym_name_final);
        return NULL;
    }
    
    // body is guaranteed non-NULL beyond this point
    
    // CRITICAL: Check if body is an immediate value (fixnum, char, special, fixed) FIRST
    // This must come BEFORE the pointer validation check, because immediate values
    // have small numeric values (e.g., 1 = 0x9) that would fail the pointer check
    if (IS_IMMEDIATE(body)) {
        // Immediate values don't need retain/release
        // CRITICAL: Return body directly as ID (void*), not as CljObject*
        // This ensures Fixnum-Literale korrekt zurückgegeben werden
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
    // This prevents undefined behavior when accessing body_obj->type for Fixnum-Literale
    if (IS_IMMEDIATE(body)) {
        // This should have been caught earlier, but as a safety check, return body directly
        return body;
    }
    
    CljObject *body_obj = (CljObject*)body;
    switch (body_obj->type) {
        case CLJ_LIST: {
            // Evaluate list using eval_list_with_context to support recur
            CljMap *env_map = (ctx->env && ctx->env->closure_env) ? ctx->env->closure_env : NULL;
            if (!ctx->env || !ctx->env->st) {
                EvalState *temp_st = evalstate_new(false);
                CljObject *result = eval_list_with_context(as_list(body), env_map, temp_st, ctx);
                evalstate_free(temp_st);
                return result;
            }
            return eval_list_with_context(as_list(body), env_map, ctx->env->st, ctx);
        }
        
        default:
            // Literal value
            return RETAIN(body);
    }
}

// Simplified body evaluation (basic implementation)
/** @brief Evaluate function body expressions */
ID eval_body(ID body, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(body != NULL);
    
    // CRITICAL: If EvalContext is provided AND has params, use eval_body_with_params to preserve RecurContext
    // If ctx has no params but has recur, we still need to handle recur, so we check for params
    if (ctx && ctx->params) {
        return eval_body_with_params(body, ctx);
    }
    
    // Handle immediate values (fixnums, chars, booleans, nil)
    if (IS_IMMEDIATE(body)) {
        return body; // Immediate values evaluate to themselves
    }
    
    // Simplified implementation - would normally evaluate the AST
    switch (((CljObject*)body)->type) {
        case CLJ_LIST: {
            // Evaluate list
            // CRITICAL: Pass ctx to preserve RecurContext
            return eval_list(as_list(body), env, st, ctx);
        }
        
        case CLJ_SYMBOL: {
            // Check if symbol is a keyword - keywords evaluate to themselves
            if (IS_KEYWORD(body)) {
                // Keywords evaluate to themselves (singletons need no memory management)
                return body;
            }
            
            // Special case: nil should evaluate to NULL (not SYM_NIL)
            if (body == SYM_NIL) {
                return NULL; // nil evaluates to NULL
            }
            
            // Resolve symbol - first try local environment, then namespace
            // Note: We need to check if key exists, not just if value is non-NULL,
            // because nil (NULL) is a valid value
            if (env && TAG(env) == CLJ_MAP) {
                // Use sentinel to distinguish "key not found" from "value is nil"
                ID result_id = map_get((CljMap*)env, body, &not_found);
                if (result_id != &not_found) {
                    return (CljObject*)result_id;
                }
            }
            
            // If not found in local environment, try namespace
            if (st && st->current_ns && st->current_ns->mappings) {
                // Use sentinel to distinguish "key not found" from "value is nil"
                ID result_id = map_get(st->current_ns->mappings, body, &not_found);
                if (result_id != &not_found) {
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
static CljObject* eval_map_lookup(CljList *list, CljMap *env, CljObject *map) {
    int total_count = list_count(list);
    int argc = total_count - 1;
    
    if (argc != 1) {
        throw_exception_formatted("ArityException", __FILE__, __LINE__, 0,
            "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
        return NULL;
    }
    
    CljObject *key = eval_arg(list, 1, env, NULL);
    if (!key) return NULL;
    
    CljObject *result = (CljObject*)map_get((CljValue)map, (CljValue)key, NULL);
    RELEASE(key);
    // RETAIN and AUTORELEASE handle immediates and NULL safely - no checks needed
    return AUTORELEASE(RETAIN(result));
}

static CljObject* eval_cond(CljList *list, CljMap *env, EvalState *st) {
    int argc = list_count(list);
    if (argc <= 1) return NULL; // (cond) => nil
    
    // Process pairs: test1 expr1 test2 expr2 ...
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) break; // Odd number of args
        
        CljObject *test = list_get_element(list, i);
        CljObject *expr = list_get_element(list, i + 1);
        
        if (!test || !expr) continue;
        
        CljObject *test_result = eval_body(test, env, st, NULL);
        if (clj_is_truthy(test_result)) {
            return eval_body(expr, env, st, NULL);
        }
    }
    return NULL; // No condition matched
}

static inline CljObject* eval_arithmetic_dispatch(CljList *list, CljMap *env, EvalState *st, CljObject *op) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_PLUS) return eval_arithmetic_generic(list, env, ARITH_ADD, st);
    if (op_sym == SYM_MINUS) return eval_arithmetic_generic(list, env, ARITH_SUB, st);
    if (op_sym == SYM_MULTIPLY) return eval_arithmetic_generic(list, env, ARITH_MUL, st);
    if (op_sym == SYM_DIVIDE) return eval_arithmetic_generic(list, env, ARITH_DIV, st);
    return NULL;
}

static inline CljObject* eval_comparison_dispatch(CljList *list, CljMap *env, CljObject *op) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_EQUALS) {
        CljObject *a, *b;
        EVAL_TWO_ARGS(list, env, a, b);
        
        if (compare_numeric_values(a, b, COMP_EQ)) {
            RELEASE_TWO_ARGS(a, b);
            return clj_true;
        }
        
        float val_a, val_b;
        if (extract_numeric_values(a, b, &val_a, &val_b)) {
            RELEASE_TWO_ARGS(a, b);
            return clj_false;
        }
        
        bool equal = clj_equal(a, b);
        RELEASE_TWO_ARGS(a, b);
        return equal ? clj_true : clj_false;
    }
    if (op_sym == SYM_LT) return eval_numeric_comparison(list, env, COMP_LT);
    if (op_sym == SYM_GT) return eval_numeric_comparison(list, env, COMP_GT);
    if (op_sym == SYM_LE) return eval_numeric_comparison(list, env, COMP_LE);
    if (op_sym == SYM_GE) return eval_numeric_comparison(list, env, COMP_GE);
    return NULL;
}

// Forward declarations
static ID eval_and_call_native(CljList *list, CljMap *env, ID (*native_func)(ID*, unsigned int), int max_args);
static ID call_function_with_args(ID fn, CljList *list, CljMap *env, EvalState *st);
static ID eval_handle_recur(CljList *list, const EvalContext *ctx);
static CljObject* resolve_list_operator(CljObject *op, CljMap *env, EvalState *st);
static ID eval_special_form_dispatch(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx, CljSymbol *op_sym);
static ID eval_function_call_from_list(CljList *list, CljMap *env, EvalState *st, CljObject *op);

static CljObject* eval_sequence_dispatch(CljList *list, CljMap *env, CljObject *op) {
    CljSymbol *op_sym = (CljSymbol*)op;
    if (op_sym == SYM_FIRST) return eval_and_call_native(list, env, native_first, 1);
    if (op_sym == SYM_REST) {
        return eval_and_call_native(list, env, native_rest, 1);
    }
    if (op_sym == SYM_CONS) return eval_and_call_native(list, env, native_cons, 2);
    if (op_sym == SYM_SEQ) return eval_seq(list, env);
    if (op_sym == SYM_NEXT) return eval_and_call_native(list, env, native_next, 1); // Clojure-compatible: next returns nil if empty
    if (op_sym == SYM_COUNT) return eval_and_call_native(list, env, native_count, 1);
    return NULL;
}

// Thread-local recursion depth tracking for eval_arg and eval_list
static _Thread_local int g_eval_arg_depth = 0;

// Reset eval arg depth (for test isolation)
void reset_eval_arg_depth(void) {
    g_eval_arg_depth = 0;
}

static inline CljObject* eval_loop_dispatch(CljList *list, CljMap *env, CljObject *op, EvalState *st) {
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

// ============================================================================
// Helper functions for eval_list refactoring
// ============================================================================

// Handle recur special form
static ID eval_handle_recur(CljList *list, const EvalContext *ctx) {
    if (!ctx || !ctx->recur) {
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
        return NULL;
    }
    
    // Evaluate recur arguments and store them in recur_args
    int total_count = list_count(list);
    int arg_count = total_count - 1; // -1 for 'recur' itself
    if (arg_count < 0) arg_count = 0;
    if (arg_count > 16) arg_count = 16; // Max 16 arguments
    
    ID recur_args[16] = {NULL};
    
    // Evaluate arguments using eval_body_with_params
    // CRITICAL: Create a new context without RecurContext for argument evaluation
    EvalContext arg_ctx = {.params = ctx->params, .env = ctx->env, .recur = NULL};
    for (int i = 0; i < arg_count; i++) {
        CljObject *arg = list_get_element(list, i + 1); // +1 to skip 'recur'
        if (arg) {
            ID eval_arg = eval_body_with_params(arg, &arg_ctx);
            if (eval_arg) {
                recur_args[i] = RETAIN(eval_arg);
            }
        }
    }
    
    // Store arguments in RecurContext
    if (ctx->recur->recur_args && ctx->recur->recur_arg_count) {
        for (int i = 0; i < arg_count && i < 16; i++) {
            if (ctx->recur->recur_args[i]) {
                RELEASE(ctx->recur->recur_args[i]);
            }
            ctx->recur->recur_args[i] = recur_args[i];
        }
        *ctx->recur->recur_arg_count = arg_count;
    }
    
    // recur returns NULL (indicates tail call)
    return NULL;
}

// Resolve operator symbol from environment or namespace
static CljObject* resolve_list_operator(CljObject *op, CljMap *env, EvalState *st) {
    if (!op || TAG(op) != CLJ_SYMBOL) {
        return op;
    }
    
    // First try local environment (if provided)
    CljObject *resolved = NULL;
    if (env && TAG(env) == CLJ_MAP) {
        ID resolved_id = map_get(env, op, &not_found);
        if (resolved_id != &not_found) {
            resolved = (CljObject*)resolved_id;
        }
    }
    
    if (resolved) {
        return resolved;
    }
    
    // OPTIMIZATION: Check resolve_cache before calling eval_symbol
    // This avoids the overhead of eval_symbol + first check in ns_resolve
    // for repeated function calls
    if (g_runtime.resolve_cache) {
        ID cached = map_get(g_runtime.resolve_cache, op, NULL);
        if (cached) {
            return (CljObject*)cached;
        }
    }
    
    // Fallback to global namespace (will populate cache if found)
    resolved = eval_symbol(as_symbol(op), st);
    return resolved ? resolved : op;
}

// Dispatch to special form handlers
static ID eval_special_form_dispatch(CljList *list, CljMap *env, EvalState *st, 
                                     const EvalContext *ctx, CljSymbol *op_sym) {
    if (op_sym == SYM_IF) {
        CljObject *cond_val = eval_arg(list, 1, env, NULL);
        bool truthy = clj_is_truthy(cond_val);
        RELEASE(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return NULL;
        return eval_body(branch, env, st, ctx);
    }
    
    if (op_sym == SYM_WHEN) {
        CljObject *cond_val = eval_arg(list, 1, env, NULL);
        // nil is valid (represents false) - check truthiness, not NULL
        bool truthy = cond_val ? clj_is_truthy(cond_val) : false;
        RELEASE(cond_val);
        if (!truthy) return NULL;
        
        int list_len = list_count(list);
        CljObject *result = NULL;
        for (int i = 2; i < list_len; i++) {
            CljObject *body_expr = list_get_element(list, i);
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
            CljObject *cond_val = eval_arg(list, 1, env, st);
            if (!cond_val || !clj_is_truthy(cond_val)) {
                RELEASE(cond_val);
                return NULL;
            }
            RELEASE(cond_val);
            
            CljObject *result = NULL;
            for (int i = 2; i < list_len; i++) {
                CljObject *body_expr = list_get_element(list, i);
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
        CljObject *result = NULL;
        for (int i = 1; i < list_len; i++) {
            CljObject *expr = list_get_element(list, i);
            if (expr) {
                ASSIGN(result, eval_body(expr, env, st, ctx));
            }
        }
        return result;
    }
    
    if (op_sym == SYM_AND) {
        int argc = list_count(list);
        if (argc <= 1) return clj_true;
        CljObject *result = clj_true;
        for (int i = 1; i < argc; i++) {
            CljObject *arg = list_get_element(list, i);
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
        CljObject *result = NULL;
        for (int i = 1; i < argc; i++) {
            CljObject *arg = list_get_element(list, i);
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
        return AUTORELEASE(eval_fn(list, fn_env, st));
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
        CljObject *quoted_expr = list_get_element(list, 1);
        if (!quoted_expr) return NULL;
        return RETAIN(quoted_expr);
    }
    
    if (op_sym == SYM_RECUR) {
        return eval_handle_recur(list, ctx);
    }
    
    if (op_sym == SYM_GO) {
        // (go body...)
        int argc = list_count(list);
        CljList *do_list = NULL;
        if (argc > 1) {
            do_list = (CljList*)make_list((CljObject*)SYM_DO, NULL);
            CljList *tail = do_list;
            for (int i = 1; i < argc; i++) {
                CljObject *expr_i = list_get_element(list, i);
                CljList *new_node = (CljList*)make_list(expr_i, NULL);
                if (tail) {
                    tail->rest = (CljObject*)new_node;
                    tail = new_node;
                }
            }
        }
        CljVector* empty_params_vec = make_vector(0, CLJ_VECTOR);
        CljList *fn_list = (CljList*)make_list((CljObject*)SYM_FN, NULL);
        if (!fn_list) return NULL;
        fn_list->rest = (CljObject*)make_list(empty_params_vec, NULL);
        CljList *fn_rest = as_list(fn_list->rest);
        if (fn_rest) {
            CljObject *body_expr = (CljObject*)do_list;
            fn_rest->rest = (CljObject*)make_list(body_expr, NULL);
        }
        CljObject *fn_obj = eval_fn(fn_list, env, st);
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
    
    return NULL; // Not a handled special form
}

// Handle function call from resolved operator
static ID eval_function_call_from_list(CljList *list, CljMap *env, EvalState *st, CljObject *op) {
    if (!op) return NULL;
    
    // Handle keywords as functions (for map lookup)
    if (TAG(op) == CLJ_SYMBOL && IS_KEYWORD(op)) {
        int total_count = list_count(list);
        int argc = total_count - 1;
        if (argc == 1) {
            CljObject *arg = eval_arg(list, 1, env, NULL);
            if (arg && TAG(arg) == CLJ_SYMBOL) {
                CljObject *resolved = eval_symbol(as_symbol(arg), st);
                if (resolved) {
                    RELEASE(arg);
                    arg = resolved;
                }
            }
            if (arg && TAG(arg) == CLJ_MAP) {
                CljObject *result = (CljObject*)map_get((CljValue)arg, (CljValue)op, NULL);
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
            return eval_map_lookup(list, env, fn);
        }
        
        if (fn_tag == CLJ_FUNC || fn_tag == CLJ_CLOSURE) {
            if (g_eval_arg_depth >= MAX_CALL_STACK_DEPTH) {
                throw_exception(EXCEPTION_STACK_OVERFLOW, 
                              "Maximum evaluation depth exceeded in nested function calls", 
                              __FILE__, __LINE__, 0);
                return NULL;
            }
            g_eval_arg_depth++;
            ID result = NULL;
            TRY {
                result = call_function_with_args(fn, list, env, st);
            } CATCH(ex) {
                g_eval_arg_depth--;
                throw_exception_object(ex);
                return NULL;
            } END_TRY
            g_eval_arg_depth--;
            return result;
        }
        
        if (fn_tag == CLJ_LIST) {
            throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call list as a function");
            return NULL;
        }
        
        return AUTORELEASE(RETAIN(fn));
    }
    
    // Check if op is a function
    if (op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE) {
        // No TRY/CATCH needed here - exceptions automatically propagate via longjmp
        // Unlike the symbol case (line 1316), we don't need to manage g_eval_arg_depth here
        // because call_function_with_args handles its own exception propagation
        return call_function_with_args(op, list, env, st);
    }
    
    return NULL; // Not a function
}

// Helper function to call a function with arguments
static ID call_function_with_args(ID fn, CljList *list, CljMap *env, EvalState *st) {
    // Count arguments
    int total_count = list_count(list);
    int argc = total_count - 1; // -1 for the function symbol itself
    if (argc < 0) argc = 0;
    
    // Stack allocate arguments array
    CljObject *args_stack[16];
    ID *args = (ID*)alloc_obj_array(argc, args_stack);
    
    // Evaluate arguments
    // CRITICAL: Use eval_arg instead of eval_body to avoid infinite recursion
    // eval_body calls eval_list for lists, which calls call_function_with_args again,
    // creating an infinite loop. eval_arg has depth protection (g_eval_arg_depth)
    // which prevents stack overflow when evaluating nested function calls like (update {:a 1} :a inc)
    if (!env || TAG(env) != CLJ_MAP) {
        // Environment is NULL or not a map - this shouldn't happen
        // But we should handle it gracefully
        for (int i = 0; i < argc; i++) {
            args[i] = NULL;
        }
    } else {
        for (int i = 0; i < argc; i++) {
            args[i] = eval_arg(list, i + 1, env, st);
            // CRITICAL: eval_arg returns AUTORELEASE objects, but eval_function_call
            // needs to use them, so we must retain them before passing to eval_function_call
            // RETAIN safely handles NULL and immediate values
            args[i] = RETAIN(args[i]);
        }
    }
    
    // Call the function
    ID result = eval_function_call(fn, args, argc, env, st);
    
    // Release arguments before freeing the array
    // eval_function_call copies arguments with ASSIGN, so we need to release our references
    for (int i = 0; i < argc; i++) {
        RELEASE((CljObject*)args[i]);
    }
    
    // Cleanup heap-allocated args if any
    free_obj_array((ID*)args, args_stack);
    
    // Convert SYM_NIL to NULL (nil evaluates to NULL)
    if (result == SYM_NIL) {
        return NULL;
    }
    
    // AUTORELEASE to ensure result is managed by caller's pool
    // AUTORELEASE handles immediates and NULL safely - no check needed
    return AUTORELEASE(result);
}

// List evaluation with context (supports recur via RecurContext)
ID eval_list_with_context(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // Clojure-compatible: Accept NULL environment - falls back to namespace lookup
    if (!list) {
        return NULL;
    }
    
    // Cache ctx->env values early for performance
    CljMap *closure_env = ctx && ctx->env ? ctx->env->closure_env : NULL;
    EvalState *ctx_st = ctx && ctx->env ? ctx->env->st : st;
    
    CljObject *head = LIST_FIRST(list);
    
    // First element is the operator
    CljObject *op = head;
    
    // Cache TAG(op) for performance (used multiple times)
    unsigned char op_tag = op ? TAG(op) : 0;
    
    // If first element is a list, evaluate it first (for nested calls like ((array-map)))
    // CRITICAL: Use eval_list_with_context recursively to preserve RecurContext
    if (op && op_tag == CLJ_LIST) {
        op = eval_list_with_context(as_list(op), env, st, ctx);
        if (!op) {
            return NULL;
        }
        // Update op_tag after evaluation
        op_tag = TAG(op);
        // Now op is the result of evaluating the inner list - continue with it
    }
    
    // Check if op is a symbol and resolve it
    // BUT: Keep the original symbol for comparison before resolving
    // CRITICAL: Check for special forms BEFORE resolving, because special forms
    // like 'time' should not be resolved (they are not in namespaces)
    CljObject *original_op = op;
    
    // Cache symbol pointer (computed once when needed)
    CljSymbol *op_sym = NULL;
    
    // Check for special forms first (before symbol resolution)
    // This ensures that special forms like 'time', 'def', and 'ns' are recognized even if
    // they're not in the namespace or environment
    // CRITICAL: This must happen BEFORE symbol resolution, because special forms
    // are not in namespaces and should not be resolved
    
    // CRITICAL: Check for recur FIRST, before any other special form handling
    // This ensures recur is handled correctly when RecurContext is available
    if (op && op_tag == CLJ_SYMBOL) {
        op_sym = as_symbol(op);
        if (op_sym && op_sym == SYM_RECUR) {
            // recur is valid if RecurContext is available
            if (ctx && ctx->recur) {
                // Evaluate recur arguments and store them in recur_args
                // Similar to call_function_with_args, use list_count and list_get_element
                CljList *recur_list = as_list(list);
                int total_count = list_count(recur_list);
                int arg_count = total_count - 1; // -1 for 'recur' itself
                if (arg_count < 0) arg_count = 0;
                if (arg_count > 16) arg_count = 16; // Max 16 arguments
                
                ID recur_args[16] = {NULL};
                
                // Evaluate arguments using eval_body_with_params (similar to eval_arg)
                // CRITICAL: Create a new context without RecurContext for argument evaluation
                // This ensures that nested function calls get their own RecurContext and don't
                // interfere with the outer function's recur state
                EvalContext arg_ctx = {.params = ctx->params, .env = ctx->env, .recur = NULL};
                for (int i = 0; i < arg_count; i++) {
                    CljObject *arg = list_get_element(recur_list, i + 1); // +1 to skip 'recur'
                    if (arg) {
                        // Evaluate argument with context that has no RecurContext
                        // This prevents nested functions from interfering with outer recur
                        ID eval_arg = eval_body_with_params(arg, &arg_ctx);
                        // CRITICAL: eval_arg can be NULL (nil), which is valid
                        // We still need to count it as an argument
                        if (eval_arg) {
                            recur_args[i] = RETAIN(eval_arg);
                        }
                    }
                }
                
                // Store arguments in RecurContext
                if (ctx->recur->recur_args && ctx->recur->recur_arg_count) {
                    // Copy arguments to recur context
                    for (int i = 0; i < arg_count && i < 16; i++) {
                        if (ctx->recur->recur_args[i]) {
                            RELEASE(ctx->recur->recur_args[i]);
                        }
                        ctx->recur->recur_args[i] = recur_args[i];
                    }
                    *ctx->recur->recur_arg_count = arg_count;
                }
                
                // recur returns NULL (indicates tail call)
                return NULL;
            } else {
                // No RecurContext - recur is invalid here
                throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
                return NULL;
            }
        }
    }
    
    // Continue with normal eval_list logic
    // CRITICAL: We cannot simply call eval_list here because eval_list calls eval_body,
    // which calls eval_list again without RecurContext. This breaks recur in nested calls.
    // Instead, we need to duplicate the eval_list logic but use eval_body_with_params
    // for nested evaluations to preserve RecurContext.
    
    // Handle maps as functions (for key lookup) - must be first
    // Use closure_env instead of env to ensure parameter resolution works correctly
    if (op && op_tag == CLJ_MAP) {
        return eval_map_lookup(list, closure_env ? closure_env : env, op);
    }
    
    // Early dispatch check with original_op (before symbol resolution)
    // This allows early exit for common arithmetic/comparison operations
    // CRITICAL: Use env if available (e.g., let_env), otherwise fall back to closure_env
    // This ensures that let bindings are found in arithmetic operations
    if (op && op_tag == CLJ_SYMBOL) {
        CljObject *result = eval_arithmetic_dispatch(list, env ? env : closure_env, ctx_st, original_op);
        if (result) return result;
        result = eval_comparison_dispatch(list, closure_env ? closure_env : env, original_op);
        if (result) return result;
    }
    
    // Continue with symbol resolution (same as eval_list)
    // Use closure_env instead of env to ensure parameter resolution works correctly
    CljMap *resolve_env = closure_env ? closure_env : env;
    if (op && op_tag == CLJ_SYMBOL) {
        // Compute op_sym if not already computed
        if (!op_sym) {
            op_sym = as_symbol(op);
        }
        ID resolved = NULL;
        if (resolve_env && TAG(resolve_env) == CLJ_MAP) {
            // Use sentinel to distinguish "key not found" from "value is nil"
            ID resolved_id = map_get(resolve_env, op, &not_found);
            if (resolved_id != &not_found) {
                resolved = resolved_id;
            }
        }
        if (resolved) {
            op = resolved;
            op_tag = op ? TAG(op) : 0;  // Update tag after resolution
        } else {
            resolved = eval_symbol(op_sym, ctx_st);
            if (resolved) {
                op = resolved;
                op_tag = op ? TAG(op) : 0;  // Update tag after resolution
            }
        }
    }
    
    // Dispatch to helper functions (same as eval_list)
    // Use closure_env instead of env to ensure parameter resolution works correctly
    CljObject *result = eval_arithmetic_dispatch(list, resolve_env, ctx_st, original_op);
    if (result) return result;
    
    result = eval_comparison_dispatch(list, resolve_env, original_op);
    if (result) return result;
    
    // Special forms that need RecurContext support
    // Use cached op_sym if available and original_op matches op, otherwise compute from original_op
    CljSymbol *original_op_sym = NULL;
    if (original_op) {
        if (op_sym && original_op == op) {
            original_op_sym = op_sym;
        } else if (TAG(original_op) == CLJ_SYMBOL) {
            original_op_sym = as_symbol(original_op);
        }
    }
    if (original_op_sym == SYM_IF) {
        CljObject *cond_val = eval_arg(list, 1, env, ctx_st);
        bool truthy = clj_is_truthy(cond_val);
        RELEASE(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) {
            return NULL;
        }
        // eval_body automatically uses eval_body_with_params if ctx is provided
        return eval_body(branch, closure_env, ctx_st, ctx);
    }
    
    // CRITICAL: Handle other special forms that call eval_body to preserve RecurContext
    // These special forms need to use eval_body_with_params instead of eval_body
    // to preserve RecurContext for recur calls
    
    // Check if this is a special form that needs RecurContext support
    // If so, handle it here instead of delegating to eval_list
    if (original_op_sym == SYM_DO) {
        // (do expr1 expr2 ...)
        // Evaluate all expressions in sequence, return the last one
        int list_len = list_count(list);
        CljObject *result = NULL;
        
        // Direct list traversal for better performance (O(1) per iteration instead of O(n))
        CljList *node = list;
        for (int i = 1; i < list_len; i++) {
            CljObject *rest = LIST_REST(node);
            if (!rest || TAG(rest) != CLJ_LIST) break;
            node = as_list(rest);
            CljObject *expr = LIST_FIRST(node);
            if (expr) {
                // eval_body automatically uses eval_body_with_params if ctx is provided
                ID expr_result = eval_body(expr, closure_env, ctx_st, ctx);
                if (result) {
                    RELEASE(result);
                }
                result = (CljObject*)expr_result;
            }
        }
        
        return result;
    }
    
    // CRITICAL: For all other operations, delegate to eval_list
    // To prevent infinite recursion (eval_list -> eval_body -> eval_body_with_params -> eval_list_with_context -> eval_list),
    // we pass a context with only RecurContext (no params), so eval_body won't call eval_body_with_params
    // This preserves RecurContext for recur calls while avoiding infinite recursion
    // Use env (not closure_env) to match what eval_list_with_context uses for arithmetic operations
    // closure_env is only used for parameter resolution in eval_body_with_params, not for symbol resolution in eval_list
    EvalContext recur_only_ctx = {.params = NULL, .env = ctx->env, .recur = ctx->recur};
    // CRITICAL: NULL is always a valid result (nil). Errors throw exceptions, not return NULL.
    return eval_list(list, env, st, &recur_only_ctx);
}

// Simplified list evaluation (optionally accepts EvalContext for recur support)
ID eval_list(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // Clojure-compatible: Accept NULL environment - falls back to namespace lookup
    if (!list) {
        return NULL;
    }
    
    // CRITICAL: If EvalContext is provided, check if this is a special form that
    // needs context-aware handling (if, do, recur, etc.)
    // We DON'T delegate to eval_list_with_context to avoid infinite recursion
    // Instead, we handle special forms directly here
    
    CljObject *head = LIST_FIRST(list);
    
    // First element is the operator
    CljObject *op = head;
    
    // CRITICAL: Handle recur FIRST, before any other special form handling
    if (ctx && op && TAG(op) == CLJ_SYMBOL) {
        CljSymbol *sym = as_symbol(op);
        if (sym && sym == SYM_RECUR) {
            return eval_handle_recur(list, ctx);
        }
    }
    
    // If first element is a list, evaluate it first (for nested calls like ((array-map)))
    // CRITICAL: Pass ctx to preserve RecurContext
    if (op && TAG(op) == CLJ_LIST) {
        op = eval_list(as_list(op), env, st, ctx);
        if (!op) {
            return NULL;
        }
        // Now op is the result of evaluating the inner list - continue with it
    }
    
    // Handle maps as functions (for key lookup) - must be first
    if (op && TAG(op) == CLJ_MAP) {
        return eval_map_lookup(list, env, op);
    }
    
    // Check if op is a symbol and resolve it
    // BUT: Keep the original symbol for comparison before resolving
    // CRITICAL: Check for special forms BEFORE resolving, because special forms
    // like 'time' should not be resolved (they are not in namespaces)
    CljObject *original_op = op;
    
    // Check for special forms before symbol resolution (def, ns, time, dotimes)
    if (op && TAG(op) == CLJ_SYMBOL) {
        CljSymbol *op_sym = (CljSymbol*)op;
        if (op_sym == SYM_DEF) {
            return eval_def(list, env, st);
        }
        if (op_sym == SYM_NS) {
            return eval_ns(list, env, st);
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
    }
    
    // Resolve operator symbol
    op = resolve_list_operator(op, env, st);
    
    // OPTIMIZED: Dispatch to helper functions for common patterns
    // Tier 1: Arithmetic operations (most frequent)
    // CRITICAL: Use original_op for comparison, not resolved op, because
    // SYM_PLUS, SYM_MINUS, etc. are statically initialized symbols that should
    // match the symbols from the AST (which are interned via intern_symbol_global)
    // CRITICAL: Use env (which may be let_env) to ensure let bindings are found
    CljObject *result = eval_arithmetic_dispatch(list, env, st, original_op);
    if (result) return result;
    
    // Tier 2: Comparison operations
    result = eval_comparison_dispatch(list, env, original_op);
    if (result) return result;
    
    // Try special form dispatch
    CljSymbol *original_op_sym = (CljSymbol*)original_op;
    ID special_result = eval_special_form_dispatch(list, env, st, ctx, original_op_sym);
    // CRITICAL: NULL is always a valid result (nil) for special forms.
    // eval_special_form_dispatch returns NULL if: 1) it was a special form that returns nil, or 2) it wasn't a special form.
    // We need to check if it was actually a special form by checking if original_op_sym is a special form symbol.
    // If it was a special form, return the result (even if NULL/nil). Otherwise, continue to function call handling.
    if (original_op_sym && (original_op_sym == SYM_IF || original_op_sym == SYM_LET || original_op_sym == SYM_DEFN || 
        original_op_sym == SYM_DEF || original_op_sym == SYM_FN || original_op_sym == SYM_DO || 
        original_op_sym == SYM_COND || original_op_sym == SYM_WHEN || original_op_sym == SYM_WHILE || 
        original_op_sym == SYM_QUOTE || original_op_sym == SYM_RECUR || original_op_sym == SYM_AND || 
        original_op_sym == SYM_OR || original_op_sym == SYM_NS || original_op_sym == SYM_TRY || 
        original_op_sym == SYM_CATCH || original_op_sym == SYM_THROW || original_op_sym == SYM_FINALLY || 
        original_op_sym == SYM_VAR || original_op_sym == SYM_LOOP || original_op_sym == SYM_GO || 
        original_op_sym == SYM_TIME || original_op_sym == SYM_DOTIMES)) {
        return special_result; // NULL is valid (nil)
    }
    // Not a special form - continue to function call handling
    
    // Tier 3: Sequence operations
    result = eval_sequence_dispatch(list, env, original_op);
    if (result) return result;
    
    // Tier 4: String and I/O operations
    if (original_op_sym == SYM_STR) {
        int total_count = list_count(as_list(list));
        int argc = total_count - 1;
        if (argc < 0) argc = 0;
        
        CljObject *args_stack[16];
        ID *args = alloc_obj_array(argc, args_stack);
        if (!args) return NULL;
        
        for (int i = 0; i < argc; i++) {
            args[i] = eval_arg(list, i + 1, env, NULL);
            if (!args[i]) {
                free_obj_array((ID*)args, args_stack);
                return NULL;
            }
        }
        
        CljObject *str_result = (CljObject*)native_str((ID*)args, argc);
        free_obj_array((ID*)args, args_stack);
        return str_result;
    }
    
    // Tier 6: Loop operations (for, doseq, dotimes)
    if (original_op_sym == SYM_FOR || original_op_sym == SYM_DOSEQ || original_op_sym == SYM_DOTIMES) {
        CljObject *loop_result = eval_loop_dispatch(list, env, original_op, st);
        return loop_result; // Return even if NULL
    }
    
    // Try function call
    // CRITICAL: Only try to call if op is a symbol or function
    // If op is not a symbol or function, eval_function_call_from_list will return NULL
    // and we should treat it as an error (not a function call)
    unsigned char op_tag = op ? TAG(op) : 0;
    if (op && (op_tag == CLJ_SYMBOL || op_tag == CLJ_FUNC || op_tag == CLJ_CLOSURE)) {
        ID func_result = eval_function_call_from_list(list, env, st, op);
        // CRITICAL: func_result can be NULL if function returns nil, which is valid
        // If op was a symbol, eval_function_call_from_list tried to resolve and call it.
        // If op was already a function, it was called directly.
        // In both cases, if we get here (no exception), the function was called successfully.
        // NULL is always a valid result (nil) - errors throw exceptions, not return NULL.
        return func_result; // NULL is valid (nil return value)
    }
    
    // Error: first element is not a function
    if (IS_IMMEDIATE(op) || (op && TAG(op) == CLJ_STRING)) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call %s as a function", clj_type_name(op->type));
        return NULL;
    }
    
    // Error: op is a list (should have been evaluated earlier)
    if (op && TAG(op) == CLJ_LIST) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call list as a function");
        return NULL;
    }
    
    // Error: first element is not a function and not a symbol
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
            "Cannot call %s as a function", clj_type_name(op->type));
    return NULL;
}

ID eval_def(CljList *list, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
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
        if (value_expr && TAG(value_expr) == CLJ_LIST) {
            value = eval_list(as_list(value_expr), eval_env, st, NULL);
        } else {
            value = (CljObject*)eval_parsed(value_expr, st, eval_env);
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
        if (func && sym && sym->name[0] && !func->name) {
            func->name = strdup(sym->name);
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
    
    // Apply metadata to value (only in DEBUG builds for memory efficiency)
    // In Clojure, metadata from ^#^{...} (def ...) is applied to the value
#ifdef DEBUG
#ifdef ENABLE_META
    // Try to get metadata from the def form (list object)
    ID form_meta = meta_get((CljObject*)list);
    if (form_meta && value) {
        // Metadata found on the form - apply it to the value
        meta_set((CljObject*)value, (CljObject*)form_meta);
    }
#endif // ENABLE_META
#endif // DEBUG
    
    // Return the symbol (Clojure-compatible: def returns the var/symbol, not the value)
    return symbol;
}

ID eval_ns(CljList *list, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    (void)env;  // Not used
    // Assertion: List and EvalState must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    assert(st != NULL);
    
    // Get namespace name (first argument) - use list_get_element like eval_def
    CljObject *ns_name_obj = list_get_element(list, 1);
    if (!ns_name_obj || TAG(ns_name_obj) != CLJ_SYMBOL) {
        eval_error("ns expects a symbol", st);
        return NULL;
    }
    
    CljSymbol *ns_sym = as_symbol(ns_name_obj);
    if (!ns_sym || !ns_sym->name[0]) {
        eval_error("ns symbol has no name", st);
        return NULL;
    }
    
    // Switch to namespace (creates if not exists)
    evalstate_set_ns(st, ns_sym->name);
    
    // Process :require clauses: (ns name (:require [ns :as alias]))
    int list_len = list_count(list);
    for (int i = 2; i < list_len; i++) {
        CljObject *clause = list_get_element(list, i);
        if (!clause || TAG(clause) != CLJ_LIST) continue;
        
        CljList *clause_list = as_list(clause);
        if (!clause_list) continue;
        
        CljObject *first = LIST_FIRST(clause_list);
        if (!first || TAG(first) != CLJ_SYMBOL) continue;
        
        CljSymbol *clause_sym = as_symbol(first);
        if (!clause_sym || !clause_sym->name) continue;
        
        // Check if this is a :require clause
        if (clause_sym->name[0] == ':' && strcmp(clause_sym->name, ":require") == 0) {
            // Process require specs: (:require [ns :as alias] [ns2 :as alias2])
            int clause_len = list_count(clause_list);
            for (int j = 1; j < clause_len; j++) {
                CljObject *spec = list_get_element(clause_list, j);
                if (!spec) continue;
                
                // Process require spec inline (similar to process_require_spec in builtins.c)
                const char *req_ns_name = NULL;
                CljObject *alias_sym = NULL;
                bool ns_name_allocated = false;
                
                // Handle simple Symbol case: (require 'namespace)
                if (TAG(spec) == CLJ_SYMBOL) {
                    CljSymbol *sym = as_symbol(spec);
                    if (sym && sym->name) {
                        req_ns_name = sym->name;
                    }
                }
                // Handle Vector case: [namespace :as alias]
                else if (TAG(spec) == CLJ_VECTOR) {
                    CljVector *vec = as_vector(spec);
                    if (vector_count(vec) >= 1) {
                        CljObject *ns_obj = (CljObject*)vector_nth(vec, 0);
                        if (ns_obj && TAG(ns_obj) == CLJ_SYMBOL) {
                            CljSymbol *ns_sym = as_symbol(ns_obj);
                            if (ns_sym && ns_sym->name) {
                                req_ns_name = ns_sym->name;
                            }
                        }
                        RELEASE(ns_obj);
                        
                        // Parse :as alias
                        int vec_count = vector_count(vec);
                        for (int k = 1; k < vec_count; k++) {
                            CljObject *elem = (CljObject*)vector_nth(vec, k);
                            if (!elem || TAG(elem) != CLJ_SYMBOL) {
                                if (elem) RELEASE(elem);
                                continue;
                            }
                            
                            CljSymbol *kw = as_symbol(elem);
                            if (kw && kw->name && kw->name[0] == ':' && strcmp(kw->name, ":as") == 0) {
                                if (k + 1 < vec_count) {
                                    alias_sym = (CljObject*)vector_nth(vec, k + 1);
                                    // Don't release alias_sym - it's stored for later use
                                    k++; // Skip next element
                                }
                            }
                            RELEASE(elem);
                        }
                    }
                }
                
                if (req_ns_name) {
                    // Load namespace using native_require logic
                    CljNamespace *existing = ns_find(req_ns_name);
                    if (!existing) {
#ifndef ESP32_BUILD
                        // Load namespace (simplified - just call native_require)
                        ID spec_id = spec;
                        ID args[1] = { spec_id };
                        (void)native_require(args, 1);
#endif
                    } else {
                        // Namespace already loaded - just set alias if needed
                        if (alias_sym && TAG(alias_sym) == CLJ_SYMBOL) {
                            CljObject *ns_name_sym = (CljObject*)intern_symbol(NULL, req_ns_name);
                            if (ns_name_sym) {
                                ns_set_alias(st->current_ns, alias_sym, ns_name_sym);
                            }
                        }
                    }
                }
                
                if (ns_name_allocated && req_ns_name) {
                    free((char*)req_ns_name);
                }
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
    if (!sym || !sym->name[0]) {
        eval_error("var symbol has no name", st);
        return NULL;
    }
    
    // Use eval_symbol to handle qualified symbols correctly
    // eval_symbol handles both qualified (clojure.string/trim) and unqualified symbols
    ID value = eval_symbol(sym, st);
    
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
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(is_list(list));
    
    // Get the parameter list (second argument) - don't evaluate it
    CljObject *params_list = list_get_element(list, 1);
    // Parameters can be a vector [a b] or a list (a b)
    if (!params_list || (TAG(params_list) != CLJ_LIST && TAG(params_list) != CLJ_VECTOR)) {
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
    
    CljObject *params_stack[16];
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
            free_obj_array((ID*)params, params_stack);
            return NULL;
        }
    }
    
    // Note: For anonymous functions (fn), we can't easily detect recursive calls
    // because there's no function name. This transformation is mainly for defn.
    // CRITICAL: Don't validate recur positions here - recur is only valid when
    // the function is called, not when it's defined. validate_recur_positions
    // would try to evaluate the body, which would fail because there's no RecurContext.
    
    // CRITICAL: Use env (which may be let_env with namespace mappings) if available,
    // otherwise fall back to namespace mappings
    // This ensures that when fn is evaluated inside let, the closure environment
    // has both local bindings and namespace mappings (like step in filter)
    CljMap *fn_env = env;
    if (!fn_env && st && st->current_ns) {
        fn_env = (CljMap*)st->current_ns->mappings;
    }
    
    // Create function object
    CljObject *fn = AUTORELEASE((CljObject*)make_function(params, param_count, body, fn_env, NULL));
    
    // Cleanup heap-allocated params if any
    free_obj_array((ID*)params, params_stack);
    
    return fn;
}

// Helper: Check if symbol is a special form or builtin (fast pointer comparison)
// Check if symbol is a special form (not a builtin function)
static bool is_special_form(CljSymbol *symbol) {
    if (!symbol) return false;
    
    // Common special forms
    if (symbol == SYM_IF) return true;
    if (symbol == SYM_LET) return true;
    if (symbol == SYM_DEFN) return true;
    if (symbol == SYM_DEF) return true;
    if (symbol == SYM_FN) return true;
    if (symbol == SYM_DO) return true;
    if (symbol == SYM_COND) return true;
    if (symbol == SYM_WHEN) return true;
    if (symbol == SYM_WHILE) return true;
    if (symbol == SYM_QUOTE) return true;
    if (symbol == SYM_RECUR) return true;
    if (symbol == SYM_AND) return true;
    if (symbol == SYM_OR) return true;
    if (symbol == SYM_NS) return true;
    if (symbol == SYM_TRY) return true;
    if (symbol == SYM_CATCH) return true;
    if (symbol == SYM_THROW) return true;
    if (symbol == SYM_FINALLY) return true;
    if (symbol == SYM_VAR) return true;
    if (symbol == SYM_LOOP) return true;
    if (symbol == SYM_GO) return true;
    if (symbol == SYM_TIME) return true;
    
    return false;
}

static bool is_special_form_or_builtin(CljSymbol *symbol) {
    if (!symbol) return false;
    
    // Check special forms first
    if (is_special_form(symbol)) return true;
    
    // Most frequently used symbols first (arithmetic operators)
    if (symbol == SYM_PLUS) return true;
    if (symbol == SYM_MINUS) return true;
    if (symbol == SYM_MULTIPLY) return true;
    if (symbol == SYM_DIVIDE) return true;
    
    // Comparison operators
    if (symbol == SYM_EQUALS) return true;
    if (symbol == SYM_LT) return true;
    if (symbol == SYM_GT) return true;
    if (symbol == SYM_LE) return true;
    if (symbol == SYM_GE) return true;
    
    // Builtin functions
    if (symbol == SYM_PRINT) return true;
    if (symbol == SYM_STR) return true;
    if (symbol == SYM_NTH) return true;
    if (symbol == SYM_FIRST) return true;
    if (symbol == SYM_REST) return true;
    if (symbol == SYM_COUNT) return true;
    if (symbol == SYM_CONS) return true;
    if (symbol == SYM_SEQ) return true;
    if (symbol == SYM_NEXT) return true;
    if (symbol == SYM_FOR) return true;
    if (symbol == SYM_DOSEQ) return true;
    if (symbol == SYM_DOTIMES) return true;
    
    return false;
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
    
    // CRITICAL: Handle qualified symbols (symbol->ns is set during parsing)
    // Parser already splits qualified symbols into name and namespace
    // This avoids string parsing in the hot-path
    if (symbol->ns && symbol->ns->name) {
        // Qualified symbol: find target namespace and resolve symbol
        const char *ns_name = symbol->ns->name;
        
        // CRITICAL: Check if ns_name is an alias in the current namespace
        // Aliases are stored in current_ns->aliases map: alias -> namespace name symbol
        const char *actual_ns_name = ns_name;
        if (st && st->current_ns && st->current_ns->aliases) {
            // Create symbol for alias lookup
            CljSymbol *alias_sym = intern_symbol_global(ns_name);
            if (alias_sym) {
                CljObject *resolved_ns_name_obj = ns_get_alias(st->current_ns, (CljObject*)alias_sym);
                if (resolved_ns_name_obj && TAG(resolved_ns_name_obj) == CLJ_SYMBOL) {
                    // Found alias - use the actual namespace name
                    CljSymbol *resolved_ns_sym = as_symbol(resolved_ns_name_obj);
                    if (resolved_ns_sym && resolved_ns_sym->name) {
                        actual_ns_name = resolved_ns_sym->name;
                    }
                }
            }
        }
        
        CljNamespace *target_ns = ns_find(actual_ns_name);
        if (!target_ns) {
            // Namespace not found - throw exception with qualified name
            const char *name = symbol->name ? symbol->name : "unknown";
            size_t qualified_len = strlen(ns_name) + 1 + strlen(name) + 1;
            char *qualified_name = (char*)malloc(qualified_len);
            if (qualified_name) {
                snprintf(qualified_name, qualified_len, "%s/%s", ns_name, name);
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context (namespace %s not found)", qualified_name, ns_name);
                free(qualified_name);
            } else {
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s/%s in this context (namespace %s not found)", ns_name, name, ns_name);
            }
            return NULL;
        }
        
        if (!target_ns->mappings) {
            // Namespace has no mappings - throw exception with qualified name
            const char *name = symbol->name ? symbol->name : "unknown";
            size_t qualified_len = strlen(ns_name) + 1 + strlen(name) + 1;
            char *qualified_name = (char*)malloc(qualified_len);
            if (qualified_name) {
                snprintf(qualified_name, qualified_len, "%s/%s", ns_name, name);
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context (namespace %s has no mappings)", qualified_name, ns_name);
                free(qualified_name);
            } else {
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s/%s in this context (namespace %s has no mappings)", ns_name, name, ns_name);
            }
            return NULL;
        }
        
        // Create unqualified symbol for lookup (symbol->name is already the name part)
        CljSymbol *name_sym = intern_symbol_global(symbol->name);
        if (!name_sym) {
            // Failed to intern symbol - throw exception
            const char *name = symbol->name ? symbol->name : "unknown";
            size_t qualified_len = strlen(ns_name) + 1 + strlen(name) + 1;
            char *qualified_name = (char*)malloc(qualified_len);
            if (qualified_name) {
                snprintf(qualified_name, qualified_len, "%s/%s", ns_name, name);
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context (failed to intern symbol)", qualified_name);
                free(qualified_name);
            } else {
                throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s/%s in this context (failed to intern symbol)", ns_name, name);
            }
            return NULL;
        }
        
        // Look up symbol in target namespace
        // CRITICAL: Use sentinel to distinguish "key not found" from "value is nil"
        static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
        ID resolved = map_get(target_ns->mappings, name_sym, &not_found_sentinel);
        if (resolved != &not_found_sentinel) {
            // Found in target namespace - return it (can be NULL/nil, which is valid)
            return resolved;
        }
        
        // Symbol not found - try to find it by iterating through mappings
        // This handles cases where symbols have different pointers but are structurally equal
        // For unqualified symbols in namespace mappings, we compare by name only
        // CRITICAL: map_get already uses MAP_FOR_EACH internally, so if it didn't find it,
        // the iteration here should also not find it unless there's a bug in map_get
        // But we try anyway as a fallback for edge cases
        CljMap *mappings = (CljMap*)target_ns->mappings;
        if (mappings && name_sym && name_sym->name) {
            const char *target_name = name_sym->name;
            MAP_FOR_EACH(mappings, key, value) {
                if (key && TAG(key) == CLJ_SYMBOL) {
                    CljSymbol *key_sym = as_symbol(key);
                    if (key_sym && key_sym->name) {
                        // Compare symbol names (for unqualified symbols in namespace mappings)
                        // Namespace mappings store unqualified symbols, so we compare by name only
                        if (strcmp(key_sym->name, target_name) == 0) {
                            // Found symbol with matching name
                            // Return the value directly
                            // CRITICAL: value can be NULL (nil), which is a valid value
                            // Return it even if it's NULL
                            return value;
                        }
                    }
                }
            }
        }
        
        // Qualified symbol not found in target namespace
        const char *name = symbol->name ? symbol->name : "unknown";
        size_t qualified_len = strlen(ns_name) + 1 + strlen(name) + 1;
        char *qualified_name = (char*)malloc(qualified_len);
        if (qualified_name) {
            snprintf(qualified_name, qualified_len, "%s/%s", ns_name, name);
            throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", qualified_name);
            free(qualified_name);
        } else {
            throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s/%s in this context", ns_name, name);
        }
        return NULL;
    }
    
    // OPTIMIZATION: Check special forms FIRST (fast pointer comparison)
    // Special forms return themselves, but builtin functions need to be resolved from namespace
    if (is_special_form(symbol)) {
        return symbol;  // Special forms return themselves
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
    
    // If not found in namespace but is a builtin, it might not be registered yet
    // Check if it's a builtin function (not a special form)
    if (is_special_form_or_builtin(symbol) && !is_special_form(symbol)) {
        // Builtin function not found in namespace - this shouldn't happen
        // but return the symbol as fallback (will be handled by eval_list)
        return symbol;
    }
    
    // Symbol not found
    const char *name = symbol->name ? symbol->name : "unknown";
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", name);
    return NULL;
}

/**
 * @brief Helper function to evaluate arguments and call a native function
 * @param list The function call list
 * @param env The environment
 * @param native_func The native function to call
 * @param max_args Maximum number of arguments expected
 * @return The result of the native function call
 */
static ID eval_and_call_native(CljList *list, CljMap *env, ID (*native_func)(ID*, unsigned int), int max_args) {
    CLJ_ASSERT(env != NULL);
    
    int argc = list_count(list) - 1;  // -1 for function symbol itself
    ID args[max_args];
    
    // Evaluate arguments
    for (int i = 0; i < argc && i < max_args; i++) {
        args[i] = eval_arg(list, i + 1, env, NULL);
    }
    
    // Call native function
    ID result = native_func(args, argc);
    
    // Native functions return mixed results: some already AUTORELEASE, some not
    // For now, return as-is and let caller handle AUTORELEASE
    return result;
}

ID eval_seq(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg(list, 1, env, NULL);
    if (!arg) return NULL;
    
    // If argument is already nil, return nil
    // Note: nil is now represented as NULL, so no special nil check needed
    
    // Check if argument is seqable
    if (!is_seqable(arg)) {
        return NULL;
    }
    
    // For lists, return as-is (lists are already sequences)
    switch (arg->type) {
        case CLJ_LIST: {
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
        // Add new binding (overwrites if key already exists)
        // CRITICAL: map_assoc may return a new map (COW), so we must use the result
        CljMap *updated_env = map_assoc(new_env, var, element);
        ASSIGN(new_env, updated_env);
    }
    return new_env;
}

ID eval_for(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    // (for [binding coll] expr)
    // Returns a lazy sequence of results
    
    if (!list) {
        return NULL;
    }
    
    CljObject *binding_list = eval_arg(list, 1, env, NULL);
    CljObject *body = eval_arg(list, 2, env, NULL);
    
    if (!binding_list || TAG(binding_list) != CLJ_LIST) {
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
    // Assertion: Environment must not be NULL when expected
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
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    (void)env; // Suppress unused parameter warning
    // (list arg1 arg2 ...) - creates a list from the arguments
    // Assertion: List must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    if (!list || TAG(list) != CLJ_LIST) return NULL;
    
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
    
    // Assertion: Environment must not be NULL when expected
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
    
    // Create new environment extending the current one
    // CRITICAL: Also include namespace mappings so functions like reverse are available
    // If no env provided, create new one with namespace mappings
    CljMap *let_env = NULL;
    int namespace_mapping_count = 0;
    if (st && st->current_ns && st->current_ns->mappings) {
        namespace_mapping_count = ((CljMap*)st->current_ns->mappings)->count;
    }
    
    if (!env) {
        // No parent environment - create new one with namespace mappings
        let_env = (CljMap*)make_map(binding_count / 2 + namespace_mapping_count + 4);
    } else {
        // Extend existing environment with namespace mappings
        let_env = (CljMap*)make_map(binding_count / 2 + env->count + namespace_mapping_count);
        if (let_env && env->count > 0) {
            // Copy existing environment bindings
            // CRITICAL: This includes function parameters from closure_env
            // When let is used inside a function, env is closure_env which contains function parameters
            for (int i = 0; i < env->capacity; i++) {
                CljValue key = env->data[i * 2];
                CljValue val = env->data[i * 2 + 1];
                if (key) {
                    // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update let_env
                    CljMap *new_let_env = map_assoc(let_env, key, val);
                    ASSIGN(let_env, new_let_env);
                }
            }
        }
    }
    
    // CRITICAL: Add clojure.core namespace mappings to let_env so functions like reverse are available
    // This ensures that when fn is evaluated inside let, the closure environment has namespace mappings
    // Use clojure.core cache instead of current_ns->mappings to ensure clojure.core functions are available
    extern TinyClJRuntime g_runtime;
    if (let_env && g_runtime.clojure_core_cache) {
        CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
        if (clojure_core && clojure_core->mappings) {
            CljMap *ns_mappings = (CljMap*)clojure_core->mappings;
            for (int i = 0; i < ns_mappings->capacity; i++) {
                CljValue key = ns_mappings->data[i * 2];
                CljValue val = ns_mappings->data[i * 2 + 1];
                if (key) {
                    // Only add if not already in let_env (local bindings take precedence)
                    if (!map_contains((CljValue)let_env, (CljValue)key)) {
                        // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update let_env
                        CljMap *new_let_env = map_assoc(let_env, key, val);
                        ASSIGN(let_env, new_let_env);
                    }
                }
            }
        }
    }
    
    if (!let_env) {
        return NULL;
    }
    
    // Process bindings sequentially (each binding can reference previous ones)
    for (int i = 0; i < binding_count; i += 2) {
        CljValue sym_val = (CljValue)vector_nth(bindings, i);
        CljValue init_val = (CljValue)vector_nth(bindings, i + 1);
        
        if (!sym_val || TAG(sym_val) != CLJ_SYMBOL) {
            RELEASE(let_env);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                           "let binding must be a symbol", 
                           NULL, 0, 0);
            return NULL;
        }
        
        // Evaluate init expression in the current let environment
        // This allows later bindings to reference earlier ones
        // Note: init_val can be NULL (nil), which is a valid value
        CljObject *value = NULL;
        
        if (!init_val) {
            // nil is represented as NULL - this is valid
            value = NULL;
        } else {
            // Check if init_val is an immediate value (doesn't need evaluation)
            if (is_fixnum(init_val) || is_special(init_val)) {
                // Immediate value - use as is
                value = init_val;
                RETAIN(value);  // Retain for consistency
            } else {
                // Complex expression - evaluate it
                // CRITICAL: Pass ctx to preserve RecurContext for recur calls
                value = eval_body(init_val, let_env, st, ctx);
                // value can be NULL if evaluation result is nil
            }
        }
        
        // CRITICAL: If value is a function (closure), update its closure_env to include itself
        // This allows recursive functions defined in let to find themselves
        // For example: (let [step (fn [x] (step x))] ...) - step needs to find itself
        if (value && TAG(value) == CLJ_CLOSURE) {
            CljFunction *func = as_function(value);
            if (func && func->closure_env) {
                // Add the function to its own closure environment so it can find itself recursively
                CljMap *func_env = func->closure_env;
                // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update closure_env
                // Always update closure_env to ensure the function can find itself recursively
                // Check if function is already in closure_env before adding
                CljObject *existing = (CljObject*)map_get(func_env, sym_val, NULL);
                if (existing != value) {
                    // Function not in closure_env - add it
                    CljMap *new_func_env = map_assoc(func_env, sym_val, value);
                    // Update closure_env (map_assoc handles COW and capacity growth)
                    if (new_func_env != func_env) {
                        // Update closure_env if it changed (COW or capacity growth)
                        // Use ASSIGN to safely replace closure_env (releases old, retains new)
                        ASSIGN(func->closure_env, new_func_env);
                    } else {
                        // map_assoc returned same map but should have added the function
                        // Verify it's there now
                        CljObject *verify = (CljObject*)map_get(func_env, sym_val, NULL);
                        if (verify != value) {
                            // Still not there - force update
                            // This shouldn't happen, but handle it
                            CljMap *forced_new = map_assoc(func_env, sym_val, value);
                            if (forced_new != func_env) {
                                // Use ASSIGN to safely replace closure_env (releases old, retains new)
                                ASSIGN(func->closure_env, forced_new);
                            }
                        }
                    }
                }
            }
        }
        
        // Add binding to environment
        // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update let_env
        CljMap *new_let_env = map_assoc(let_env, sym_val, value);
        // ASSIGN handles retain/release automatically and optimizes self-assignment
        ASSIGN(let_env, new_let_env);
        
        // Note: value is retained by map_assoc via RETAIN in map implementation
        // So we need to release our reference
        RELEASE(value);
    }
    
    // Evaluate body expressions with the let environment
    // Body is everything after the bindings vector
    CljObject *result = NULL;
    int list_len = list_count(list);
    
    if (list_len <= 2) {
        // No body expressions - return nil
        result = NULL;
    } else {
        // CRITICAL: Create updated context with let_env as closure_env
        // This ensures that eval_body_with_params uses let_env instead of the original closure_env
        // when ctx->params is present (which happens in recur contexts)
        // Store EvalEnv on stack (it's only used during this function call)
        EvalEnv let_env_ctx;
        EvalContext let_ctx;
        if (ctx) {
            let_ctx = *ctx;  // Copy existing context
            // Update closure_env to use let_env so let bindings are available
            let_env_ctx.closure_env = let_env;
            let_env_ctx.st = ctx->env ? ctx->env->st : st;
            let_ctx.env = &let_env_ctx;
        } else {
            // No ctx - create new one with let_env
            let_env_ctx.closure_env = let_env;
            let_env_ctx.st = st;
            let_ctx.env = &let_env_ctx;
            let_ctx.params = NULL;
            let_ctx.recur = NULL;
        }
        
        // Evaluate all body expressions, return last one
        for (int i = 2; i < list_len; i++) {
            CljObject *body_expr = list_get_element(list, i);
            if (body_expr) {
                if (result) {
                    RELEASE(result);
                }
                
                // Check if body_expr is an immediate value (doesn't need evaluation)
                if (is_fixnum((CljValue)body_expr) || is_special((CljValue)body_expr)) {
                    // Immediate value - use as is
                    result = body_expr;
                    RETAIN(result);  // Retain for consistency
                } else {
                    // Complex expression - evaluate it
                    // CRITICAL: Pass let_ctx to ensure let_env is used as closure_env
                    // The let_env_ctx is on the stack but only used during this function call,
                    // so it's safe as long as we don't store the pointer beyond this scope
                    result = eval_body(body_expr, let_env, st, &let_ctx);
                }
            }
        }
    }
    
    // Clean up environment
    RELEASE(let_env);
    
    // Return the result of the last body expression
    return result;
}

// ============================================================================
// EVAL_DEFN - Function definition macro implementation
// ============================================================================
ID eval_defn(CljList *list, CljMap *env, EvalState *st) {
    // (defn name [params*] body*)
    // Expands to: (def name (fn [params*] body*))
    
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    
    if (!list || !st) {
        return NULL;
    }
    
    // Check if form has metadata (store in variable for later use)
    // In Release builds, metadata is parsed but ignored (for memory efficiency)
    // In DEBUG builds, metadata is stored and can be retrieved via (meta)
    #ifdef ENABLE_META
    ID form_meta = meta_get((CljObject*)list);
    #else
    ID form_meta = NULL;
    #endif // ENABLE_META
    
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
    CljObject *params_stack[16];
    ID *params = alloc_obj_array(param_count, params_stack);
    
    for (int i = 0; i < param_count; i++) {
        params[i] = vector_nth(params_vec_data, i);
        if (!params[i] || TAG(params[i]) != CLJ_SYMBOL) {
            free_obj_array((ID*)params, params_stack);
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
        if (rest_list && rest_list->first && TAG(rest_list->first) == CLJ_LIST) {
            CljList *second_list = as_list(rest_list->first);
            if (second_list && second_list->first && TAG(second_list->first) == CLJ_SYMBOL) {
                CljSymbol *second_sym = as_symbol(second_list->first);
                if (second_sym && second_sym->name && strcmp(second_sym->name, "defn") == 0) {
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
            CljObject *do_args[16]; // Max 16 body expressions
            if (body_count > 15) {
                free_obj_array((ID*)params, params_stack);
                throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                               "defn body has too many expressions (max 15)", 
                               NULL, 0, 0);
                return NULL;
            }
            
            do_args[0] = (CljObject*)SYM_DO;
            current = rest;
            for (int i = 0; i < body_count; i++) {
                if (current && current->first) {
                    do_args[i + 1] = current->first;
                }
                if (!current->rest) break;
                current = as_list(current->rest);
            }
            
            // Create do list
            body_expr_obj = (CljObject*)make_list_from_stack((CljValue*)do_args, body_count + 1);
        }
    }
    
    if (!body_expr_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn body cannot be empty", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Check if body is :native marker (for native function stubs)
    // This must be checked before TCO transformation
    // We can recognize :native at different points:
    // 1. Direct keyword: :native
    // 2. List containing only :native: (:native)
    // 3. Evaluated form that results in :native
    bool is_native_stub = false;
    
    // Check 1: Direct keyword comparison
    if (body_expr_obj == (CljObject*)SYM_KW_NATIVE) {
        is_native_stub = true;
    } else if (IS_KEYWORD(body_expr_obj)) {
        // Also check by pointer comparison for interned symbols
        CljSymbol *body_kw = as_symbol(body_expr_obj);
        if (body_kw == SYM_KW_NATIVE) {
            is_native_stub = true;
        } else if (body_kw && body_kw->name && strcmp(body_kw->name, ":native") == 0) {
            // Fallback: String comparison for cases where :native was parsed before SYM_KW_NATIVE was initialized
            is_native_stub = true;
        }
    } else if (body_expr_obj && TAG(body_expr_obj) == CLJ_LIST) {
        // Check 2: List containing only :native (lazy evaluation case)
        // This handles cases where :native was parsed as a list (e.g., due to metadata parsing issues)
        CljList *body_list = as_list(body_expr_obj);
        if (body_list && body_list->first) {
            CljObject *first = body_list->first;
            // Check if list has only one element (rest is NULL or empty list)
            bool is_single_element = !body_list->rest || 
                                     (TAG(body_list->rest) == CLJ_LIST && 
                                      as_list(body_list->rest)->first == NULL);
            if (is_single_element) {
                // Single element list - check if it's :native
                if (first == (CljObject*)SYM_KW_NATIVE) {
                    is_native_stub = true;
                } else if (IS_KEYWORD(first)) {
                    CljSymbol *first_kw = as_symbol(first);
                    if (first_kw == SYM_KW_NATIVE) {
                        is_native_stub = true;
                    } else if (first_kw && first_kw->name && strcmp(first_kw->name, ":native") == 0) {
                        is_native_stub = true;
                    }
                }
            }
        }
    }
    
    // Handle native function stub
    if (is_native_stub) {
        // Extract Clojure function name
        CljSymbol *name_symbol = as_symbol(name_sym);
        if (!name_symbol || !name_symbol->name) {
            free_obj_array((ID*)params, params_stack);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                           "defn with :native requires a valid function name", 
                           NULL, 0, 0);
            return NULL;
        }
        
        // CRITICAL: Intern the symbol to ensure pointer equality in map lookups
        // The parsed symbol might not be the same instance as the interned symbol
        CljSymbol *interned_name_sym = intern_symbol_global(name_symbol->name);
        if (!interned_name_sym) {
            free_obj_array((ID*)params, params_stack);
            throw_exception(EXCEPTION_RUNTIME, 
                           "Failed to intern function name symbol", 
                           NULL, 0, 0);
            return NULL;
        }
        
        // Lookup native function by Clojure name
        BuiltinFn native_func = native_function_lookup(name_symbol->name);
        if (!native_func) {
            // Log detailed error about missing native implementation
            // Use string representation of the original symbol as printed in Clojure
            CljObject *name_as_obj = (CljObject*)name_sym;
            const char *name_str_repr = to_cstring(name_as_obj);
            if (name_str_repr) {
                fprintf(stderr,
                        "[tiny-clj] ERROR: :native function pointer not found for symbol %s (C name: %s)\n",
                        name_str_repr,
                        name_symbol->name ? name_symbol->name : "<NULL>");
                free((void*)name_str_repr);
            } else {
                fprintf(stderr,
                        "[tiny-clj] ERROR: :native function pointer not found for C name %s\n",
                        name_symbol->name ? name_symbol->name : "<NULL>");
            }

            free_obj_array((ID*)params, params_stack);
            // Use throw_exception_formatted for better error messages
            throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Native function not found for: %s", name_symbol->name);
            return NULL;
        }
        
        // Create native function object
        // Use Clojure function name (already interned symbol name)
        CljCFunc *native_func_obj = (CljCFunc*)make_named_func(native_func, NULL, name_symbol->name);
        if (!native_func_obj) {
            free_obj_array((ID*)params, params_stack);
            throw_exception(EXCEPTION_RUNTIME, 
                           "Failed to create native function object", 
                           NULL, 0, 0);
            return NULL;
        }
        
        // Register native function in namespace using interned symbol (replaces any existing registration)
        ns_define(st->current_ns, (ID)interned_name_sym, (ID)native_func_obj);
        
        // ASSERT: Verify that the registered native function can be found using interned symbol
        static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
        ID found = map_get(st->current_ns->mappings, (ID)interned_name_sym, (ID)&not_found_sentinel);
        CLJ_ASSERT(found != (ID)&not_found_sentinel && "eval_defn: Registered native function must be findable in namespace mappings");
        CLJ_ASSERT(found == (ID)native_func_obj && "eval_defn: Found function must match registered function");
        
        // Apply metadata to native function
        // In Clojure, metadata from ^#^{...} (defn ...) is applied to the function
        // Also add :name and :ns metadata (Clojure-compatible) for all native functions
#ifdef ENABLE_META
        // form_meta already defined at function start
        if (form_meta) {
            // Metadata found on the form - apply it to the function
            meta_set((CljObject*)native_func_obj, (CljObject*)form_meta);
        } else {
            // Try to get metadata from the function name symbol
            ID name_meta = meta_get((CljObject*)name_sym);
            if (name_meta) {
                meta_set((CljObject*)native_func_obj, (CljObject*)name_meta);
            }
        }
        
        // Always add :name and :ns metadata (Clojure-compatible) for native functions
        // This ensures (meta 'clojure.string/trim) returns metadata even without explicit metadata
        init_special_symbols();
        CljMap *existing_meta = (CljMap*)meta_get((CljObject*)native_func_obj);
        CljMap *meta_map = existing_meta ? existing_meta : make_map(4);
        bool meta_changed = false;
        
        if (meta_map) {
            // Add :name (function name as string)
            CljSymbol *kw_name = intern_symbol_global(":name");
            if (kw_name) {
                // Check if :name already exists in metadata
                ID existing_name = map_get(meta_map, (ID)kw_name, NULL);
                if (!existing_name) {
                    CljString *name_str = make_string(name_symbol->name);
                    if (name_str) {
                        CljMap *updated = map_assoc(meta_map, (ID)kw_name, (ID)name_str);
                        if (updated != meta_map) {
                            if (!existing_meta) RELEASE(meta_map);
                            meta_map = updated;
                            meta_changed = true;
                        }
                        RELEASE(name_str);
                    }
                }
            }
            
            // Add :ns (namespace name as symbol)
            if (SYM_KW_NS && st->current_ns && st->current_ns->name) {
                // Check if :ns already exists in metadata
                ID existing_ns = map_get(meta_map, (ID)SYM_KW_NS, NULL);
                if (!existing_ns) {
                    CljMap *updated = map_assoc(meta_map, (ID)SYM_KW_NS, (ID)st->current_ns->name);
                    if (updated != meta_map) {
                        if (!existing_meta) RELEASE(meta_map);
                        meta_map = updated;
                        meta_changed = true;
                    }
                }
            }
            
            // Set metadata on function object (always if we created a new map, or if we modified existing)
            // CRITICAL: Always set metadata if we created a new map (existing_meta was NULL)
            // This ensures native functions always have metadata with :name and :ns
            if (!existing_meta) {
                // We created a new map - always set it, even if it's empty or only partially filled
                // CRITICAL: meta_set retains the meta_map, so we need to release our reference
                meta_set((CljObject*)native_func_obj, (CljObject*)meta_map);
                RELEASE(meta_map);
                
                // Verify that metadata was set correctly
                ID verify_meta = meta_get((CljObject*)native_func_obj);
                CLJ_ASSERT(verify_meta == (ID)meta_map && "meta_set: Metadata should be retrievable immediately after setting");
            } else if (meta_changed) {
                // We modified an existing map - set the updated version
                meta_set((CljObject*)native_func_obj, (CljObject*)meta_map);
            }
        }
#endif // ENABLE_META
        
        free_obj_array((ID*)params, params_stack);
        return (ID)interned_name_sym;  // defn returns the interned symbol
    }
    
    // Transform recursive tail calls to recur (automatic TCO)
    CljObject *transformed_body = transform_recursive_tail_calls(body_expr_obj, name_sym, 
                                                                  (CljObject**)params, param_count, 
                                                                  body_expr_obj);
    if (!transformed_body) {
        // Transformation failed - use original body
        transformed_body = body_expr_obj;
    }
    
    // CRITICAL: Don't validate recur positions here - recur is only valid when
    // the function is called, not when it's defined. validate_recur_positions
    // would try to evaluate the body, which would fail because there's no RecurContext.
    
    // Create function object directly (skip fn list creation to save code)
    // Use current namespace mappings as environment for function evaluation
    // This ensures that builtin functions like + are available in closures
    if (!st || !st->current_ns) {
        free_obj_array((ID*)params, params_stack);
        throw_exception(EXCEPTION_RUNTIME, "defn requires an evaluation state with namespace", NULL, 0, 0);
        return NULL;
    }
    
    // CRITICAL: Create a closure environment that includes both current namespace mappings
    // and clojure.core mappings. This ensures that functions defined in non-core namespaces
    // (like clojure.string) can access clojure.core functions (like reverse, str, empty?, etc.)
    // This is similar to what eval_let does (lines 2698-2719)
    CljMap *fn_env = NULL;
    
    // Start with current namespace mappings
    if (st->current_ns->mappings) {
        // Create a new map that will contain both current_ns and clojure.core mappings
        CljMap *ns_mappings = (CljMap*)st->current_ns->mappings;
        fn_env = (CljMap*)make_map(ns_mappings->count + 32);
        
        // Copy current namespace mappings first
        for (int i = 0; i < ns_mappings->capacity; i++) {
            CljValue key = ns_mappings->data[i * 2];
            CljValue val = ns_mappings->data[i * 2 + 1];
            if (key) {
                CljMap *new_fn_env = map_assoc(fn_env, key, val);
                ASSIGN(fn_env, new_fn_env);
            }
        }
    } else {
        fn_env = make_map(32);
        ns_set_mappings(st->current_ns, fn_env);
    }
    
    // Add clojure.core namespace mappings so functions like reverse, str, empty? are available
    extern TinyClJRuntime g_runtime;
    if (fn_env && g_runtime.clojure_core_cache) {
        CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
        if (clojure_core && clojure_core->mappings) {
            CljMap *core_mappings = (CljMap*)clojure_core->mappings;
            for (int i = 0; i < core_mappings->capacity; i++) {
                CljValue key = core_mappings->data[i * 2];
                CljValue val = core_mappings->data[i * 2 + 1];
                if (key) {
                    // Only add if not already in fn_env (current namespace mappings take precedence)
                    if (!map_contains(fn_env, key)) {
                        CljMap *new_fn_env = map_assoc(fn_env, key, val);
                        ASSIGN(fn_env, new_fn_env);
                    }
                }
            }
        }
    }
    
    // Create function object with extended closure_env that includes both namespaces
    CljFunction *fn_obj = make_function(params, param_count, transformed_body, fn_env, NULL);
    if (!fn_obj) {
        free_obj_array((ID*)params, params_stack);
        return NULL;
    }
    
    // Set function name
    CljFunction *func = fn_obj;
    CljSymbol *sym = as_symbol(name_sym);
    if (func && sym && sym->name[0] && !func->name) {
        func->name = strdup(sym->name);
    }
    
    // Apply metadata to function (only in DEBUG builds for memory efficiency)
    // In Release builds, metadata is parsed but ignored
#ifdef DEBUG
#ifdef ENABLE_META
    // form_meta already defined at function start
    if (form_meta) {
        meta_set((CljObject*)fn_obj, (CljObject*)form_meta);
    } else {
        ID name_meta = meta_get((CljObject*)name_sym);
        if (name_meta) {
            meta_set((CljObject*)fn_obj, (CljObject*)name_meta);
        }
    }
#endif // ENABLE_META
#endif // DEBUG
    
    // Register function in namespace (after creation for recursive calls)
    // This ensures the function is available when the body is evaluated
    // ns_define may update st->current_ns->mappings (COW), so we need to update closure_env
    ns_define(st->current_ns, name_sym, fn_obj); // ns_define does RETAIN internally
    
    // ASSERT: Verify that the registered function can be found
    static CljObject not_found_sentinel_func = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID found_func = map_get(st->current_ns->mappings, name_sym, (ID)&not_found_sentinel_func);
    CLJ_ASSERT(found_func != (ID)&not_found_sentinel_func && "eval_defn: Registered function must be findable in namespace mappings");
    CLJ_ASSERT(found_func == (ID)fn_obj && "eval_defn: Found function must match registered function");
    
    // CRITICAL: After ns_define, we need to ensure the function is in closure_env
    // Since we created an extended closure_env (with clojure.core mappings),
    // we need to add the function to that extended environment, not replace it
    // with just the namespace mappings (which would lose clojure.core access)
    // First, update the extended env with the function so it can find itself recursively
    CljObject *func_in_closure = (CljObject*)map_get((CljValue)func->closure_env, (CljValue)name_sym, NULL);
    if (func_in_closure != (CljObject*)fn_obj) {
        // Function not in closure_env yet - add it to the extended env
        CljMap *new_closure_env = map_assoc(func->closure_env, name_sym, fn_obj);
        ASSIGN(func->closure_env, new_closure_env);
    }
    // Also update the extended env with any new mappings from ns_define (if COW happened)
    // But keep the extended env structure (with clojure.core mappings)
    if (st->current_ns->mappings != func->closure_env) {
        // ns_define may have created a new mappings map (COW)
        // We need to merge the new namespace mappings into our extended env
        // But preserve clojure.core mappings that are already there
        CljMap *current_ns_mappings = (CljMap*)st->current_ns->mappings;
        MAP_FOR_EACH(current_ns_mappings, key, val) {
            // Update or add namespace mappings to extended env
            // Note: key can be NULL (nil) - that's a valid key in Clojure!
            CljMap *new_closure_env = map_assoc(func->closure_env, key, val);
            ASSIGN(func->closure_env, new_closure_env);
        }
    }
    
    RELEASE((CljObject*)fn_obj);
    free_obj_array((ID*)params, params_stack);
    return name_sym;
}

// Helper function for evaluating arguments
ID eval_arg(CljList *list, int index, CljMap *env, EvalState *st) {
    CLJ_ASSERT(list != NULL);
    if (!list || TAG(list) != CLJ_LIST) return NULL;
    
    // Handle NULL environment gracefully
    if (env == NULL) {
        // Return the element as-is if no environment is available
        ID element = list_nth(as_list(list), index);
        // Special case: nil should evaluate to NULL (not SYM_NIL)
        if (element == SYM_NIL) {
            return NULL;  // nil is represented as NULL
        }
        // RETAIN and AUTORELEASE handle immediates and NULL safely - no check needed
        return AUTORELEASE(RETAIN(element));
    }
    
    // Use the existing list_nth function which is safer
    ID element = list_nth(as_list(list), index);
    if (!element) return NULL;
    
    // For simple types (numbers, strings, booleans), return as-is
    if (IS_IMMEDIATE(element) || (element && TAG(element) == CLJ_STRING)) {
        return element; // Don't retain - caller will handle retention
    }
    
    // For symbols, resolve them from environment
    if (element && TAG(element) == CLJ_SYMBOL) {
        // Special case: nil should evaluate to NULL (not SYM_NIL)
        if (element == SYM_NIL) {
            // nil is represented as NULL - return NULL directly
            return NULL;
        }
        // Keywords evaluate to themselves (no resolution needed)
        if (IS_KEYWORD(element)) {
            return element;
        }
        if (env && TAG(env) == CLJ_MAP) {
            // CRITICAL: When let is used inside a function, env should be let_env which contains function parameters
            // This allows eval_arg to resolve function parameters like 'coll' in (rest coll)
            // Use sentinel to distinguish "key not found" from "value is nil"
            ID resolved_id = map_get(env, element, &not_found);
            if (resolved_id != &not_found) {
                // Key exists in map - get the value (which may be NULL/nil)
                // CRITICAL: map_get returns a value that is already retained by the map.
                // eval_arg should return AUTORELEASE objects, so we need to autorelease it.
                // If resolved is NULL (nil), return NULL directly without RETAIN/AUTORELEASE
                CljObject *resolved = (CljObject*)resolved_id;
                if (!resolved) {
                    return NULL;
                }
                // RETAIN and AUTORELEASE handle immediates safely - no check needed
                return AUTORELEASE(RETAIN(resolved));
            }
            // Key doesn't exist in map - try namespace resolution below
        }
        
        // If not found in local environment, try namespace
        // CRITICAL: ns_resolve can handle st == NULL (uses default "user" namespace)
        CljObject *resolved = ns_resolve(st, as_symbol(element));
        if (resolved) {
            // CRITICAL: ns_resolve returns AUTORELEASE objects.
            // eval_arg should return AUTORELEASE objects, so we just return it as-is.
            return resolved;
        }
        
        // Symbol not found - throw exception
        // This happens when:
        // 1. Symbol not found in env AND
        // 2. ns_resolve(st, ...) returned NULL (not found in any namespace)
        CljSymbol *sym_obj = as_symbol(element);
        const char *sym_name = sym_obj && sym_obj->name ? sym_obj->name : "unknown";
        throw_unresolved_symbol_exception(sym_name);
        return NULL;
    }
    
    // For lists, evaluate them directly
    // CRITICAL: eval_list manages recursion depth itself when calling functions,
    // so we don't need to increase g_eval_arg_depth here. This prevents double counting.
    if (element && TAG(element) == CLJ_LIST) {
        // Evaluate nested list directly
        // CRITICAL: Use provided st if available, otherwise create a new one
        // This ensures that eval_list has access to the correct namespace
        EvalState *eval_st = st;
        bool created_st = false;
        if (!eval_st) {
            eval_st = evalstate_new(false);
            created_st = true;
        }
        CljObject *result = NULL;
        // CRITICAL: Always cleanup, even if exception is thrown
        // Use TRY/CATCH to ensure cleanup
        TRY {
            result = eval_list(as_list(element), env, eval_st, NULL);
        } CATCH(ex) {
            // Exception occurred - cleanup and re-throw
            if (created_st) {
                evalstate_free(eval_st);
            }
            throw_exception_object(ex);
            return NULL;
        } END_TRY
        
        // CRITICAL: eval_list returns AUTORELEASE objects.
        // eval_arg should return AUTORELEASE objects, so we just return it as-is.
        
        // Normal path - cleanup
        if (created_st) {
            evalstate_free(eval_st);
        }
        
        return result;
    }
    
    // For maps, evaluate keys and values (similar to eval_body)
    // This is necessary for cases like (get {nil "value"} nil) where nil should be evaluated to NULL
    if (element && TAG(element) == CLJ_MAP) {
        CljMap *map = (CljMap*)element;
        CljMap *result = map_empty();
        
        MAP_FOR_EACH(map, key, value) {
            // Cache TAG values for performance (used multiple times)
            unsigned char key_tag = key ? TAG(key) : 0;
            unsigned char value_tag = value ? TAG(value) : 0;
            
            // Evaluate key and value (nil should evaluate to NULL)
            // Check for SYM_NIL before calling eval_body to avoid symbol resolution
            ID eval_key = (key && key_tag == CLJ_SYMBOL && (CljObject*)key == (CljObject*)SYM_NIL) 
                ? NULL 
                : (key ? eval_body(key, env, st, NULL) : NULL);
            
            ID eval_value = (value && value_tag == CLJ_SYMBOL && (CljObject*)value == (CljObject*)SYM_NIL) 
                ? NULL 
                : (value ? eval_body(value, env, st, NULL) : NULL);
            
            // Add evaluated key-value pair to result map
            // eval_body returns AUTORELEASE objects; map_assoc will retain them
            ASSIGN(result, map_assoc(result, eval_key, eval_value));
        }
        
        return AUTORELEASE(result);
    }
    
    // For vectors, etc., return as-is
    return element; // Don't retain - caller will handle retention
}

// is_symbol is already defined in namespace.c

ID eval_dotimes(CljList *list, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
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
    
    if (list_data->rest && TAG(list_data->rest) == CLJ_LIST) {
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
    } else if (binding_list && TAG(binding_list) == CLJ_LIST) {
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
        ? eval_symbol_in_env(n_obj, env, st) 
        : n_obj;
    
    if (!n_evaluated || TAG(n_evaluated) != CLJ_INT) {
        return NULL;
    }
    
    int n = as_fixnum((CljValue)n_evaluated);
    
    // Pre-add namespace mappings to base environment (only once, not in loop)
    // This avoids repeated map operations in the hot path
    CljMap *base_env = env;
    if (base_env && TAG(base_env) == CLJ_MAP) {
        CljMap *env_with_ns = env_add_namespace_mappings(base_env, st);
        if (env_with_ns != base_env) {
            base_env = env_with_ns;
            RETAIN(base_env);
        }
    }
    
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
            
            EvalState *eval_st = st;
            bool created_st = false;
            if (!eval_st) {
                eval_st = evalstate_new(false);
                created_st = true;
            }
            
            // Evaluate body expressions
            CljObject *body_result = NULL;
            if (body_list && TAG(body_list) == CLJ_LIST) {
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
            if (created_st) {
                evalstate_free(eval_st);
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
    // Clojure-compatible: Accept NULL environment - falls back to namespace lookup
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
    if (expr && TAG(expr) == CLJ_LIST) {
        result = eval_list(as_list(expr), eval_env, st, NULL);
    } else if (expr && TAG(expr) == CLJ_SYMBOL) {
        // For symbols, look up in environment (if provided) or namespace
        if (eval_env && TAG(eval_env) == CLJ_MAP) {
            CljObject *resolved = (CljObject*)map_get((CljValue)eval_env, (CljValue)expr, NULL);
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
        fprintf(stderr, "Elapsed time: %.2f msecs\n", elapsed_ms);
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

ID clj_call_function(ID fn, int argc, ID *argv) {
    if (!fn || TAG(fn) != CLJ_FUNC) return NULL;
    
    // Arity check
    CljFunction *func = as_function(fn);
    if (!func) {
        return make_exception("Error", "Invalid function object", NULL, 0, 0);
    }
    int param_count_clj_call = func->params ? vector_count(func->params) : 0;
    if (argc != param_count_clj_call) {
        return make_exception("Error", "Arity mismatch in function call", NULL, 0, 0);
    }
    
    // Heap-allocated parameter array
    ID *heap_params = (ID*)malloc(sizeof(ID) * argc);
    for (int i = 0; i < argc; i++) {
        heap_params[i] = RETAIN(argv[i]);
    }
    
    // Extend environment with parameters
    // Get parameter array pointer directly (no copying needed)
    ID *params_array_clj_ptr = vector_as_array(func->params);
    CljMap *call_env = env_extend_stack(func->closure_env, params_array_clj_ptr, heap_params, argc);
    if (!call_env) {
        free(heap_params);
        return make_exception("Error", "Failed to create function environment", NULL, 0, 0);
    }
    
    // Evaluate function body (simplified; would normally call eval())
    // RETAIN handles NULL safely - no check needed
    ID result = RETAIN(func->body);
    
    // Release environment and parameter array
    RELEASE(call_env);
    free(heap_params);
    
    return result;
}

ID clj_apply_function(ID fn, ID *args, int argc, ID env) {
    if (!fn || TAG(fn) != CLJ_FUNC) return NULL;
    (void)env;
    
    // Evaluate arguments (simplified; would normally call eval())
    ID *eval_args = STACK_ALLOC(ID, argc);
    for (int i = 0; i < argc; i++) {
        eval_args[i] = RETAIN(args[i]);
    }
    
    return clj_call_function(fn, argc, eval_args);
}

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
        throw_exception("ParseError", "Failed to parse expression", __FILE__, __LINE__, 0);
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