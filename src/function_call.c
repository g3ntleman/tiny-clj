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
#include "function.h"

#include "error_messages.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "clj_strings.h"
#include "seq.h"
#include "namespace.h"
#include "memory.h"
#include "error_messages.h"
#include "list.h"
#include "builtins.h"
#include "value.h"
#include "environment.h"
#include "clj_strings.h"
#include "vector.h"

// Global state for stack-based recur implementation - statically initialized
static _Thread_local CljObject* g_recur_args[16] = {0};  // Max 16 arguments, initialized to NULL
static _Thread_local int g_recur_arg_count = 0;
static _Thread_local bool g_recur_detected = false;

#include "map.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>


// Forward declarations  
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);

CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);

// Forward declarations for loop evaluation
CljObject* eval_body_with_env(CljObject *body, CljMap *env);
CljObject* eval_list_with_env(CljList *list, CljMap *env);


// ============================================================================
// COMPARISON OPERATORS REFACTORING - Type Promotion and Generic Functions
// ============================================================================

// Macros for common argument evaluation patterns
#define EVAL_TWO_ARGS(list, env, a, b) do { \
    (a) = eval_arg_retained(as_list((ID)(list)), 1, (env)); \
    (b) = eval_arg_retained(as_list((ID)(list)), 2, (env)); \
    if (!(a) || !(b)) { \
        if (a && !is_immediate((CljValue)(a))) RELEASE(a); \
        if (b && !is_immediate((CljValue)(b))) RELEASE(b); \
        return NULL; \
    } \
} while(0)

#define RELEASE_TWO_ARGS_SAFE(a, b) do { \
    if (!is_immediate((CljValue)(a))) RELEASE(a); \
    if (!is_immediate((CljValue)(b))) RELEASE(b); \
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
    if (is_fixnum((CljValue)a)) {
        *val_a = (float)as_fixnum((CljValue)a);
    } else if (is_fixed((CljValue)a)) {
        *val_a = as_fixed((CljValue)a);
    } else {
        return false; // Invalid type
    }
    
    // Extract value from second object
    if (is_fixnum((CljValue)b)) {
        *val_b = (float)as_fixnum((CljValue)b);
    } else if (is_fixed((CljValue)b)) {
        *val_b = as_fixed((CljValue)b);
    } else {
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
    return result ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}

/** @brief Compare symbol name directly (works for non-interned symbols) */
// DEPRECATED: Use pointer comparison (op == SYM_*) instead for O(1) performance
// This function is kept for backward compatibility but should not be used in new code
// Function removed - use pointer comparison (op == SYM_*) instead

/** @brief Allocate array with stack optimization (size <= 16 on stack, else heap) */
static inline ID* alloc_obj_array(int size, CljObject **stack_buffer) {
    return size <= 16 ? (ID*)stack_buffer : (ID*)malloc(sizeof(CljObject*) * size);
}

/** @brief Free array allocated with alloc_obj_array */
static inline void free_obj_array(CljObject **array, CljObject **stack_buffer) {
    if (array != stack_buffer) free(array);
}

/** @brief Get raw nth element from a list (0=head). Returns NULL if out of bounds */
static CljObject* list_get_element(CljList *list, int index) {
    if (!list || index < 0) return NULL;
        CljList *node = list;
    if (index == 0) return LIST_FIRST(node);
    int i = 0;
    while (i < index) {
        CljObject *rest = LIST_REST(node);
        if (!rest || !is_type(rest, CLJ_LIST)) return NULL;
        node = as_list((ID)rest);
        i++;
    }
    return LIST_FIRST(node);
}

// Arithmetic operation types
typedef enum {
    ARITH_ADD, ARITH_SUB, ARITH_MUL, ARITH_DIV
} ArithOp;

// Arithmetic operation functions
// Arithmetic helper functions removed - now using inline switch statements in eval_arithmetic_generic_with_substitution

// Error messages - removed unused array

// Helper function to check if a type is numeric
static bool is_numeric_type(CljObject *obj) {
    if (!obj) return false;
    return IS_IMMEDIATE(obj);
}

/** @brief Generic arithmetic function (variadic version) */
CljObject* eval_arithmetic_generic(CljList *list, CljMap *env, ArithOp op, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    (void)st; // Suppress unused parameter warning
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
        args[i] = eval_arg_retained(list, i + 1, env);
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
            result = (CljObject*)native_add_variadic((ID*)args, argc);
            break;
        case ARITH_SUB:
            result = (CljObject*)native_sub_variadic((ID*)args, argc);
            break;
        case ARITH_MUL:
            result = (CljObject*)native_mul_variadic((ID*)args, argc);
            break;
        case ARITH_DIV:
            result = (CljObject*)native_div_variadic((ID*)args, argc);
            break;
    }
    
    // Clean up arguments
    for (int i = 0; i < argc; i++) {
        RELEASE(args[i]);
    }
    free(args);
    
    return result ? AUTORELEASE(result) : NULL;
}

// Generic arithmetic function (with parameter substitution)
CljObject* eval_arithmetic_generic_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, ArithOp op, CljObject *closure_env) {
    CljObject *a = eval_body_with_params(list_get_element(as_list((ID)list), 1), params, values, param_count, closure_env);
    CljObject *b = eval_body_with_params(list_get_element(as_list((ID)list), 2), params, values, param_count, closure_env);
    
    // Check for nil arguments
    if (!a || !b) {
        throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "String cannot be used as a Number");
        return NULL;
    }
    
    // Check for non-numeric types
    if (!is_numeric_type(a) || !is_numeric_type(b)) {
        throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "String cannot be used as a Number");
        return NULL;
    }
    
    // Division by zero check
    if (op == ARITH_DIV) {
        if (is_fixnum((CljValue)b) && as_fixnum((CljValue)b) == 0) {
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                    "Division by zero: %d / %d", as_fixnum((CljValue)a), as_fixnum((CljValue)b));
            return NULL;
        }
        if (is_fixed((CljValue)b) && as_fixed((CljValue)b) == 0.0) {
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                    "Division by zero: %f / %f", as_fixed((CljValue)a), as_fixed((CljValue)b));
            return NULL;
        }
    }
    
    // For now, only support integer arithmetic
    // TODO: Add mixed int/float support
    if (!is_fixnum((CljValue)a) || !is_fixnum((CljValue)b)) {
        throw_exception_formatted("NotImplementedError", __FILE__, __LINE__, 0,
                "Mixed int/float arithmetic not yet implemented");
        return NULL;
    }
    
    int a_val = as_fixnum((CljValue)a);
    int b_val = as_fixnum((CljValue)b);
    int result;
    
    switch (op) {
        case ARITH_ADD:
            // Check for integer overflow before addition
            if (a_val > 0 && b_val > INT_MAX - a_val) {
                throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0,
                    ERR_INTEGER_OVERFLOW_ADDITION, a_val, b_val);
                return NULL;
            } else if (a_val < 0 && b_val < INT_MIN - a_val) {
                throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0,
                    ERR_INTEGER_UNDERFLOW_ADDITION, a_val, b_val);
                return NULL;
            }
            result = a_val + b_val;
            break;
        case ARITH_SUB:
            // Check for integer overflow/underflow before subtraction
            if (a_val > 0 && b_val < a_val - INT_MAX) {
                throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0,
                    ERR_INTEGER_OVERFLOW_SUBTRACTION, a_val, b_val);
                return NULL;
            } else if (a_val < 0 && b_val > a_val - INT_MIN) {
                throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0,
                    ERR_INTEGER_UNDERFLOW_SUBTRACTION, a_val, b_val);
                return NULL;
            }
            result = a_val - b_val;
            break;
        case ARITH_MUL:
            // Check for integer overflow before multiplication
            if (a_val != 0 && b_val != 0) {
                if (a_val > INT_MAX / b_val || a_val < INT_MIN / b_val) {
                    throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0,
                        ERR_INTEGER_OVERFLOW_MULTIPLICATION, a_val, b_val);
                    return NULL;
                }
            }
            result = a_val * b_val;
            break;
        case ARITH_DIV:
            result = a_val / b_val;
            break;
        default:
            return NULL;
    }
    
    return fixnum(result);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env) {
    // Note: env parameter is used for environment context, but closure_env takes precedence
    // for Clojure functions. For native functions, env is not used.
    
    if (!is_type((CljObject*)fn, CLJ_FUNC) && !is_type((CljObject*)fn, CLJ_CLOSURE)) {
        throw_exception("TypeError", "Attempt to call non-function value", NULL, 0, 0);
        return NULL;
    }
    
    // Check if it's a native function (CljFunc) or Clojure function (CljFunction)
    if (is_native_fn((CljObject*)fn)) {
        // It's a native function (CljFunc)
        CljFunc *native_func = (CljFunc*)fn;
        if (!native_func || !native_func->fn) {
            throw_exception("TypeError", "Invalid native function", NULL, 0, 0);
            return NULL;
        }
        return native_func->fn((CljObject**)args, argc);
    }
    
    // It's a Clojure function (CljFunction)
    CljFunction *func = (CljFunction*)fn;
    if (!func) {
        return (CljObject*)make_exception("Error", "Invalid function object", NULL, 0, 0);
    }
    
    // Arity check
    if (argc != func->param_count) {
        throw_exception("ArityError", "Arity mismatch in function call", NULL, 0, 0);
        return NULL;
    }
    
    // Clojure functions with parameters are now supported
    
    // TCO Loop for tail-call optimization with recur
    CljObject *current_args[16];
    int current_argc = argc;
    
    // Copy initial arguments
    for (int i = 0; i < argc && i < 16; i++) {
        current_args[i] = RETAIN(args[i]);
    }
    
    // TCO Loop - iterate on recur
    CljObject *result = NULL;
    do {
        g_recur_detected = false;
        g_recur_arg_count = 0;
        
        // Evaluate function body
        CljObject *new_result = eval_body_with_params(func->body, func->params, 
                                                       current_args, current_argc, 
                                                       func->closure_env);
        
        if (g_recur_detected) {
            // ✅ CRITICAL: Release intermediate result from recur iteration
            // RELEASE/RETAIN macros handle immediates automatically - no guard needed
            RELEASE(new_result);
            
            // Release old arguments and copy new ones
            for (int i = 0; i < current_argc; i++) {
                RELEASE(current_args[i]);
            }
            
            // Update argc and copy new arguments from g_recur_args
            current_argc = g_recur_arg_count;
            for (int i = 0; i < current_argc; i++) {
                current_args[i] = g_recur_args[i]; // Already retained in recur evaluation
                g_recur_args[i] = NULL; // Clear global to prevent double-release
            }
            
            // Continue loop
            continue;
        }
        
        // No recur - this is the final result
        // Use ASSIGN for proper refcounting (handles retain/release automatically)
        ASSIGN(result, new_result);
        break;
    } while (true);
    
    // Cleanup arguments
    for (int i = 0; i < current_argc; i++) {
        RELEASE(current_args[i]);
    }
    
    return result;
}


// Evaluate body with environment lookup (for loops)
CljObject* eval_body_with_env(CljObject *body, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(body != NULL);
    
    switch (body->type) {
        case CLJ_SYMBOL: {
            // Look up symbol in environment
            return (CljObject*)env_get_stack((CljObject*)env, body);
        }
        
        case CLJ_LIST: {
            // Type check before calling
            if (!is_type(body, CLJ_LIST)) return NULL;
            CljList *list_data = as_list((ID)body);
            return eval_list_with_env(list_data, env);
        }
        
        default:
            // Literal value
            return AUTORELEASE(RETAIN(body));
    }
}

// Evaluate list with environment (for loops)
CljObject* eval_list_with_env(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    // Assertion: List must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    
    CljObject *head = list->first;
    
    // First element is the operator
    CljObject *op = head;
    
    // For symbols, look up in environment
    if (is_type(op, CLJ_SYMBOL)) {
        CljObject *resolved = (CljObject*)env_get_stack((CljObject*)env, op);
        if (resolved) {
            // If it's a function, call it
            if (is_type(resolved, CLJ_FUNC)) {
                // Count arguments
                int total_count = list_count(as_list((ID)list));
                int argc = total_count - 1;
                if (argc < 0) argc = 0;
                
                // Evaluate arguments
                CljObject *args_stack[16];
                ID *args = alloc_obj_array(argc, args_stack);
                
                for (int i = 0; i < argc; i++) {
                    args[i] = eval_body_with_env(list_get_element(list, i + 1), env);
                    if (!args[i]) args[i] = NULL;
                }
                
                // Call the function
                CljObject *result = eval_function_call(resolved, args, argc, env);
                
                free_obj_array((CljObject**)args, args_stack);
                
                return result;
            }
            // Otherwise, return the resolved value
            return RETAIN(resolved);
        }
    }
    
    // Fallback: return first element
    return AUTORELEASE(RETAIN(head));
}

// Simplified body evaluation with parameter binding
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    CLJ_ASSERT(body != NULL);
    
    // Assertion: Parameters and values must not be NULL when param_count > 0
    if (param_count > 0) {
        assert(params != NULL);
        assert(values != NULL);
    }
    
    if (is_type(body, CLJ_SYMBOL)) {
        // Resolve symbol - check parameters first
        for (int i = 0; i < param_count; i++) {
            if (params[i] && body == params[i]) {
                return AUTORELEASE(RETAIN(values[i]));
            }
            // Also try name comparison for non-interned symbols
            if (params[i] && is_type(params[i], CLJ_SYMBOL) && is_type(body, CLJ_SYMBOL)) {
                CljSymbol *param_sym = as_symbol((ID)params[i]);
                CljSymbol *body_sym = as_symbol((ID)body);
                if (param_sym && body_sym && strcmp(param_sym->name, body_sym->name) == 0) {
                    return AUTORELEASE(RETAIN(values[i]));
                }
            }
        }
        // If not a parameter, try to resolve from closure_env (namespace map)
        if (closure_env && is_type(closure_env, CLJ_MAP)) {
            CljObject *resolved = (CljObject*)map_get((CljValue)closure_env, (CljValue)body);
            if (resolved) {
                return AUTORELEASE(RETAIN(resolved));
            }
        }
        // If not found in parameters or closure_env, try to resolve from global namespace
        // This is the "Lazy Binding" fallback for global symbols
        // For now, return the symbol itself - it will be resolved in eval_list_with_param_substitution
        return AUTORELEASE(RETAIN(body));
    }
    
    // Check for NULL body
    if (!body) {
        return NULL;
    }
    
    // Check if body is an immediate value (fixnum, char, special, fixed)
    if (IS_IMMEDIATE(body)) {
        // Immediate values don't need retain/release
        return (CljObject*)body;
    }
    
    // Check if body is a valid pointer (not pointing to invalid memory)
    if ((uintptr_t)body < 0x1000) {
        return NULL;
    }
    
    // For lists, evaluate them with parameter substitution
    switch (body->type) {
        case CLJ_LIST: {
            return eval_list_with_param_substitution(body, params, values, param_count, closure_env);
        }
        
        default:
            // Literal value
            return AUTORELEASE(RETAIN(body));
    }
}

// Evaluate list with parameter substitution
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    // Assertion: List must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    assert(param_count >= 0); // param_count should be non-negative
    if (list->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list((ID)list);
    if (!list_data) return NULL;
    
    CljObject *head = list_data->first;
    if (!head) return NULL;
    
    // First element is the operator
    CljObject *op = head;
    
    // OPTIMIZED: Ordered by frequency (Tier 1: Most common operations first)
    // Tier 1: Very frequent (90%+ of calls)
    if (op == SYM_PLUS) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_ADD, closure_env);
    }
    
    if (op == SYM_MINUS) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_SUB, closure_env);
    }
    
    if (op == SYM_EQUALS || op == SYM_EQUAL) {
        CljObject *a = eval_body_with_params(list_get_element(as_list((ID)list), 1), params, values, param_count, closure_env);
        CljObject *b = eval_body_with_params(list_get_element(as_list((ID)list), 2), params, values, param_count, closure_env);
        
        if (!a || !b) return NULL;
        
        // Simple equality check for numbers (immediate values)
        if (is_fixnum((CljValue)a) && is_fixnum((CljValue)b)) {
            bool equal = (as_fixnum((CljValue)a) == as_fixnum((CljValue)b));
            return equal ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
        }
        
        // For other types, use clj_equal
        bool equal = clj_equal(a, b);
        return equal ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
    }
    
    if (op == SYM_IF) {
        // (if cond then else?)
        CljObject *cond_val = eval_body_with_params(list_get_element(as_list((ID)list), 1), params, values, param_count, closure_env);
        if (!cond_val) return NULL;
        
        bool truthy = clj_is_truthy(cond_val);
        CljObject *branch = truthy ? list_get_element(as_list((ID)list), 2) : list_get_element(as_list((ID)list), 3);
        if (!branch) return NULL;
        return eval_body_with_params(branch, params, values, param_count, closure_env);
    }
    
    // Tier 2: Frequent (70-90% of calls)
    if (op == SYM_MULTIPLY) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_MUL, closure_env);
    }
    
    if (op == SYM_DIVIDE) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_DIV, closure_env);
    }
    
    if (op == SYM_RECUR) {
        // Evaluate all recur arguments
        int recur_argc = list_count(as_list((ID)list)) - 1;
        
        // Arity check
        if (recur_argc != param_count) {
            throw_exception_formatted("ArityError", __FILE__, __LINE__, 0,
                "Mismatched argument count to recur (expected: %d, got: %d)", 
                param_count, recur_argc);
            return NULL;
        }
        
        // Evaluate new argument values
        for (int i = 0; i < recur_argc; i++) {
            g_recur_args[i] = eval_body_with_params(
                list_get_element(as_list((ID)list), i + 1), 
                params, values, param_count, closure_env);
            if (!g_recur_args[i]) {
                // Cleanup on error
                for (int j = 0; j < i; j++) {
                    RELEASE(g_recur_args[j]);
                }
                return NULL;
            }
            RETAIN(g_recur_args[i]);
        }
        
        g_recur_arg_count = recur_argc;
        g_recur_detected = true;
        return NULL; // Signal to loop to continue
    }
    
    if (op == SYM_PRINTLN) {
        return eval_println(as_list((ID)list), NULL);  // Simplified - no parameter substitution for now
    }
    
    // Handle maps as functions (for key lookup)
    if (is_type(op, CLJ_MAP)) {
        int total_count = list_count(as_list((ID)list));
        int argc = total_count - 1;
        
        if (argc != 1) {
            throw_exception_formatted("ArityException", __FILE__, __LINE__, 0,
                "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
            return NULL;
        }
        
        CljObject *key = eval_body_with_params(list_get_element(as_list((ID)list), 1), params, values, param_count, closure_env);
        if (!key) return NULL;
        
        CljObject *result = (CljObject*)map_get((CljValue)op, (CljValue)key);
        return result ? RETAIN(result) : NULL;
    }
    
    // Check if head is a parameter or in closure_env
    CljObject *resolved_op = NULL;
    for (int i = 0; i < param_count; i++) {
        if (params[i] && op == params[i]) {
            resolved_op = values[i];
            break;
        }
        if (params[i] && is_type(params[i], CLJ_SYMBOL) && is_type(op, CLJ_SYMBOL)) {
            CljSymbol *param_sym = as_symbol((ID)params[i]);
            CljSymbol *op_sym = as_symbol((ID)op);
            if (param_sym && op_sym && strcmp(param_sym->name, op_sym->name) == 0) {
                resolved_op = values[i];
                break;
            }
        }
    }
    
    // If not found in parameters, try closure_env
    if (!resolved_op && is_type(op, CLJ_SYMBOL) && is_type(closure_env, CLJ_MAP)) {
        resolved_op = (CljObject*)map_get((CljValue)closure_env, (CljValue)op);
    }
    
    // If still not found, try global namespace lookup (for recursive calls)
    if (!resolved_op && is_type(op, CLJ_SYMBOL)) {
        EvalState *st = evalstate();
        // Use TRY/CATCH to handle exceptions from eval_symbol
        TRY {
            resolved_op = eval_symbol(op, st);
        } CATCH(ex) {
            // If symbol resolution fails, continue without resolving
            // This allows the function to fall through to the fallback case
            resolved_op = NULL;
        } END_TRY
    }
    
    // If op was resolved to a function, call it
    if (resolved_op && (is_type(resolved_op, CLJ_FUNC) || is_type(resolved_op, CLJ_CLOSURE))) {
        // Count arguments
        int total_count = list_count(as_list((ID)list));
        int argc = total_count - 1;
        if (argc < 0) argc = 0;
        
        // Evaluate arguments with substitution
        CljObject *args_stack[16];
        ID *args = (ID*)alloc_obj_array(argc, args_stack);
        
        for (int i = 0; i < argc; i++) {
            args[i] = eval_body_with_params(list_get_element(as_list((ID)list), i + 1), params, values, param_count, closure_env);
            if (!args[i]) args[i] = NULL;
        }
        
        // Call the function
        CljObject *result = eval_function_call(resolved_op, args, argc, NULL);
        
        free_obj_array((CljObject**)args, args_stack);
        
        return result;
    }
    
    // If we have a resolved_op but it's not a function, that's an error
    if (resolved_op && !is_type(resolved_op, CLJ_FUNC) && !is_type(resolved_op, CLJ_CLOSURE)) {
        const char *op_name = "unknown";
        if (is_type(op, CLJ_SYMBOL)) {
            CljSymbol *sym = as_symbol((ID)op);
            if (sym && sym->name) {
                op_name = sym->name;
            }
        }
        throw_exception_formatted("TypeError", __FILE__, __LINE__, 0,
            "Value is not a function: %s", op_name);
        return NULL;
    }
    
    // If we couldn't resolve the operator and it's a symbol, that's an error
    if (is_type(op, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol((ID)op);
        const char *op_name = sym && sym->name ? sym->name : "unknown";
        throw_exception_formatted("UndefinedFunctionError", __FILE__, __LINE__, 0,
            "Function not found: %s", op_name);
        return NULL;
    }
    
    // Fallback: return first element (for non-symbol operators)
    return AUTORELEASE(RETAIN(head));
}

// Simplified body evaluation (basic implementation)
/** @brief Evaluate function body expressions */
ID eval_body(ID body, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(body != NULL);
    
    // Simplified implementation - would normally evaluate the AST
    switch (((CljObject*)body)->type) {
        case CLJ_LIST: {
            // Evaluate list
            return eval_list(as_list((ID)body), env, st);
        }
        
        case CLJ_SYMBOL: {
            // Resolve symbol
            return (CljObject*)env_get_stack((CljObject*)env, body);
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
    
    CljObject *key = eval_arg_retained(list, 1, env);
    if (!key) return NULL;
    
    CljObject *result = (CljObject*)map_get((CljValue)map, (CljValue)key);
    RELEASE(key);
    return result ? AUTORELEASE(RETAIN(result)) : NULL;
}

static CljObject* eval_arithmetic_dispatch(CljList *list, CljMap *env, EvalState *st, CljObject *op) {
    if (op == SYM_PLUS) return eval_arithmetic_generic(list, env, ARITH_ADD, st);
    if (op == SYM_MINUS) return eval_arithmetic_generic(list, env, ARITH_SUB, st);
    if (op == SYM_MULTIPLY) return eval_arithmetic_generic(list, env, ARITH_MUL, st);
    if (op == SYM_DIVIDE) return eval_arithmetic_generic(list, env, ARITH_DIV, st);
    return NULL;
}

static CljObject* eval_comparison_dispatch(CljList *list, CljMap *env, CljObject *op) {
    if (op == SYM_EQUALS || op == SYM_EQUAL) {
        CljObject *a, *b;
        EVAL_TWO_ARGS(list, env, a, b);
        
        if (compare_numeric_values(a, b, COMP_EQ)) {
            RELEASE_TWO_ARGS(a, b);
            return make_special(SPECIAL_TRUE);
        }
        
        float val_a, val_b;
        if (extract_numeric_values(a, b, &val_a, &val_b)) {
            RELEASE_TWO_ARGS(a, b);
            return make_special(SPECIAL_FALSE);
        }
        
        bool equal = clj_equal(a, b);
        RELEASE_TWO_ARGS(a, b);
        return equal ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
    }
    if (op == SYM_LT) return eval_numeric_comparison(list, env, COMP_LT);
    if (op == SYM_GT) return eval_numeric_comparison(list, env, COMP_GT);
    if (op == SYM_LE) return eval_numeric_comparison(list, env, COMP_LE);
    if (op == SYM_GE) return eval_numeric_comparison(list, env, COMP_GE);
    return NULL;
}

static CljObject* eval_sequence_dispatch(CljList *list, CljMap *env, CljObject *op) {
    if (op == SYM_FIRST) return eval_first(list, env);
    if (op == SYM_REST) return eval_rest(list, env);
    if (op == SYM_CONS) return eval_cons(list, env);
    if (op == SYM_SEQ) return eval_seq(list, env);
    if (op == SYM_NEXT) return eval_rest(list, env); // next is alias for rest
    if (op == SYM_COUNT) return eval_count(list, env);
    return NULL;
}

static CljObject* eval_loop_dispatch(CljList *list, CljMap *env, CljObject *op) {
    if (op == SYM_FOR) return AUTORELEASE(eval_for(list, env));
    if (op == SYM_DOSEQ) return AUTORELEASE(eval_doseq(list, env));
    if (op == SYM_DOTIMES) return AUTORELEASE(eval_dotimes(list, env));
    return NULL;
}

// Helper function to call a function with arguments
static ID call_function_with_args(ID fn, CljList *list, CljMap *env) {
    // Count arguments
    int total_count = list_count(list);
    int argc = total_count - 1; // -1 for the function symbol itself
    if (argc < 0) argc = 0;
    
    // Stack allocate arguments array
    CljObject *args_stack[16];
    ID *args = (ID*)alloc_obj_array(argc, args_stack);
    
    // Evaluate arguments
    for (int i = 0; i < argc; i++) {
        args[i] = eval_arg_retained(list, i + 1, env);
        if (!args[i]) args[i] = NULL;
    }
    
    // Call the function
    ID result = eval_function_call(fn, args, argc, env);
    
    // Cleanup heap-allocated args if any
    free_obj_array((CljObject**)args, args_stack);
    
    return result;
}

// Simplified list evaluation
ID eval_list(CljList *list, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    if (!list) {
        return NULL;
    }
    
    CljObject *head = LIST_FIRST(list);
    
    // First element is the operator
    CljObject *op = head;
    
    
    // If first element is a list, evaluate it first (for nested calls like ((array-map)))
    if (is_type(op, CLJ_LIST)) {
        op = eval_list(as_list((ID)op), env, st);
        if (!op) {
            return NULL;
        }
        // Now op is the result of evaluating the inner list - continue with it
    }
    
    // Handle maps as functions (for key lookup) - must be first
    if (is_type(op, CLJ_MAP)) {
        return eval_map_lookup(list, env, op);
    }
    
    // Check if op is a symbol and resolve it
    // BUT: Keep the original symbol for comparison before resolving
    CljObject *original_op = op;
    if (is_type(op, CLJ_SYMBOL)) {
        CljObject *resolved = eval_symbol(op, st);
        if (resolved) {
            op = resolved;
        }
    }
    
    // OPTIMIZED: Dispatch to helper functions for common patterns
    // Tier 1: Arithmetic operations (most frequent)
    CljObject *result = eval_arithmetic_dispatch(list, env, st, original_op);
    if (result) return result;
    
    // Tier 2: Comparison operations
    result = eval_comparison_dispatch(list, env, original_op);
    if (result) return result;
    
    if (original_op == SYM_IF) {
        // (if cond then else?)
        CljObject *cond_val = eval_arg_retained(list, 1, env);
        bool truthy = clj_is_truthy(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return NULL;
        return eval_body(branch, env, st);
    }
    
    // Tier 3: Sequence operations
    result = eval_sequence_dispatch(list, env, original_op);
    if (result) return result;
    
    // Tier 4: String and I/O operations
    if (original_op == SYM_STR) {
        int total_count = list_count(as_list((ID)list));
        int argc = total_count - 1;
        if (argc < 0) argc = 0;
        
        CljObject *args_stack[16];
        ID *args = alloc_obj_array(argc, args_stack);
        if (!args) return NULL;
        
        for (int i = 0; i < argc; i++) {
            args[i] = eval_arg_retained(list, i + 1, env);
            if (!args[i]) {
                free_obj_array((CljObject**)args, args_stack);
                return NULL;
            }
        }
        
        CljObject *result = (CljObject*)native_str((ID*)args, argc);
        free_obj_array((CljObject**)args, args_stack);
        return result;
    }
    
    if (original_op == SYM_PRINTLN) {
        return eval_println(list, env);
    }
    
    // Tier 4: Less frequent (10-30% of calls)
    if (original_op == SYM_AND) {
        // (and expr1 expr2 ...) - short circuit evaluation
        // Returns first falsy value or last value
        int argc = list_count(list);
        if (argc <= 1) return make_special(SPECIAL_TRUE); // (and) => true
        
        CljObject *result = make_special(SPECIAL_TRUE);
        for (int i = 1; i < argc; i++) {
            CljObject *arg = list_get_element(list, i);
            if (!arg) continue;
            
            result = eval_body(arg, env, st);
            if (!clj_is_truthy(result)) {
                return result; // Short-circuit on false
            }
        }
        return result; // Return last value
    }
    
    if (original_op == SYM_OR) {
        // (or expr1 expr2 ...) - short circuit evaluation
        // Returns first truthy value or last value
        int argc = list_count(list);
        if (argc <= 1) return NULL; // (or) => nil
        
        CljObject *result = NULL;
        for (int i = 1; i < argc; i++) {
            CljObject *arg = list_get_element(list, i);
            if (!arg) continue;
            
            result = eval_body(arg, env, st);
            if (clj_is_truthy(result)) {
                return result; // Short-circuit on true
            }
        }
        return result; // Return last value
    }
    
    // Tier 5: Special forms and definitions
    if (original_op == SYM_DEF) {
        return eval_def(list, env, st);
    }
    
    if (original_op == SYM_NS) {
        return eval_ns(list, env, st);
    }
    
    if (original_op == SYM_FN) {
        return AUTORELEASE(eval_fn(list, env));
    }
    
    if (original_op == SYM_LET) {
        // (let [bindings*] body*)
        return eval_let(list, env, st);
    }
    
    if (original_op == SYM_DEFN) {
        // (defn name [params*] body*)
        return eval_defn(list, env, st);
    }
    
    if (original_op == SYM_QUOTE) {
        // (quote expr) - return expr without evaluating
        CljObject *quoted_expr = list_get_element(list, 1);
        if (!quoted_expr) return NULL;
        return RETAIN(quoted_expr), quoted_expr;
    }
    
    // recur is only valid inside function bodies, not in top-level lists
    if (original_op == SYM_RECUR) {
        throw_exception("SyntaxError", "recur can only be used inside function bodies", NULL, 0, 0);
        return NULL;
    }
    
    // Tier 6: Loop operations
    result = eval_loop_dispatch(list, env, original_op);
    if (result) return result;
    
    if (original_op == SYM_LIST) {
        return AUTORELEASE(eval_list_function(list, env));
    }
    
    // Check if op (after resolution) is a function
    if (is_type(op, CLJ_FUNC) || is_type(op, CLJ_CLOSURE)) {
        return call_function_with_args(op, list, env);
    }
    
    // Fallback: try to resolve symbol and call as function
    if (is_type(op, CLJ_SYMBOL)) {
        // Handle keywords as functions (for map lookup)
        CljSymbol *sym = as_symbol((ID)op);
        if (sym->name[0] == ':') {
            // Keyword as function - perform map lookup
            int total_count = list_count(as_list((ID)list));
            int argc = total_count - 1;
            
            if (argc == 1) {
                CljObject *arg = eval_arg_retained(list, 1, env);
                
                // If argument is a symbol, resolve it to get the actual value
                if (is_type(arg, CLJ_SYMBOL)) {
                    CljObject *resolved = eval_symbol(arg, st);
                    if (resolved) {
                        RELEASE(arg);  // Release the symbol
                        arg = resolved;  // Use the resolved value
                    }
                }
                
                if (is_type(arg, CLJ_MAP)) {
                    CljObject *result = (CljObject*)map_get((CljValue)arg, (CljValue)op);
                    return result ? RETAIN(result) : NULL;
                }
            }
            
            // Invalid usage - fall through to error handling
        }
        // Resolve the symbol to get the function
        CljObject *fn = eval_symbol(op, st);
        if (!fn) {
            return NULL;
        }
        
        // Check if it's a map (for map lookup)
        if (is_type(fn, CLJ_MAP)) {
            return eval_map_lookup(list, env, fn);
        }
        
        // Check if it's a function (native or interpreted)
        if (is_type(fn, CLJ_FUNC) || is_type(fn, CLJ_CLOSURE)) {
            return call_function_with_args(fn, list, env);
        }
        
        // Not a function, just return the resolved value
        return AUTORELEASE(RETAIN(fn));
    }
    
    // Error: first element is not a function
    if (IS_IMMEDIATE(op) || is_type(op, CLJ_STRING)) {
        throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                "Cannot call %s as a function", clj_type_name(op->type));
        return NULL;
    }
    
    // Fallback: return first element (for other types)
    return AUTORELEASE(RETAIN(head));
}


ID eval_equal(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *a = eval_arg_retained(list, 1, env);
    CljObject *b = eval_arg_retained(list, 2, env);
    
    if (!a || !b) return NULL;
    
    bool equal = clj_equal(a, b);
    return equal ? make_special(SPECIAL_TRUE) : make_special(SPECIAL_FALSE);
}


CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    CljObject *arg = eval_body_with_params(list_get_element(as_list((ID)list), 1), params, values, param_count, closure_env);
    if (arg) {
        char *str = pr_str(arg);
        printf("println: %s\n", str);
        free(str);
    }
    return NULL;
}


ID eval_println(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (arg) {
        char *str = pr_str(arg);
        printf("println: %s\n", str);
        free(str);
        RELEASE(arg); // Release after use
    }
    return NULL;
}

ID eval_def(CljList *list, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(is_list(list));
    
    // Get the symbol name (second argument) - don't evaluate it, just get the symbol
    CljObject *symbol = list_get_element(list, 1);
    if (!symbol || symbol->type != CLJ_SYMBOL) {
        return NULL;
    }
    
    // Get the value (third argument) - evaluate this
    CljObject *value_expr = list_get_element(list, 2);
    if (!value_expr) {
        return NULL;
    }
    
    // Evaluate the value expression
    CljObject *value = NULL;
    if (is_type(value_expr, CLJ_LIST)) {
        value = eval_list(as_list((ID)value_expr), env, st);
    } else {
        value = eval_expr_simple(value_expr, st);
    }
    if (!value) {
        return NULL;
    }
    
    // If the value is a function, set its name
    if (is_type(value, CLJ_FUNC)) {
        CljFunction *func = as_function((ID)value);
        CljSymbol *sym = as_symbol((ID)symbol);
        if (func && sym && sym->name[0] && !func->name) {
            func->name = strdup(sym->name);
        }
    }
    
    // Store the symbol-value binding in the environment
    if (st) {
        // Store in namespace
        ns_define(st, symbol, value);
    }
    
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
    if (!ns_name_obj || ns_name_obj->type != CLJ_SYMBOL) {
        eval_error("ns expects a symbol", st);
        return NULL;
    }
    
    CljSymbol *ns_sym = as_symbol((ID)ns_name_obj);
    if (!ns_sym || !ns_sym->name[0]) {
        eval_error("ns symbol has no name", st);
        return NULL;
    }
    
    // Switch to namespace (creates if not exists)
    evalstate_set_ns(st, ns_sym->name);
    
    return NULL;
}

ID eval_fn(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(is_list(list));
    
    // Get the parameter list (second argument) - don't evaluate it
    CljObject *params_list = list_get_element(list, 1);
    // Parameters can be a vector [a b] or a list (a b)
    if (!params_list || (params_list->type != CLJ_LIST && params_list->type != CLJ_VECTOR)) {
        return NULL;
    }
    
    // Get the body (third argument) - don't evaluate it
    CljObject *body = list_get_element(list, 2);
    if (!body) {
        return NULL;
    }
    
    // Convert parameter list/vector to array
    int param_count = 0;
    if (is_type(params_list, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector((ID)params_list);
        param_count = vec ? vec->count : 0;
    } else {
        param_count = list_count(as_list(params_list));
    }
    
    CljObject *params_stack[16];
    ID *params = alloc_obj_array(param_count, params_stack);
    
    for (int i = 0; i < param_count; i++) {
        if (is_type(params_list, CLJ_VECTOR)) {
            CljPersistentVector *vec = as_vector((ID)params_list);
            params[i] = vec->data[i];
        } else {
            params[i] = list_get_element(as_list((ID)params_list), i);
        }
        if (!params[i] || ((CljObject*)params[i])->type != CLJ_SYMBOL) {
            // Invalid parameter
            free_obj_array((CljObject**)params, params_stack);
            return NULL;
        }
    }
    
    // Create function object
    CljObject *fn = AUTORELEASE(make_function((CljObject**)params, param_count, body, (CljObject*)env, NULL));
    
    // Cleanup heap-allocated params if any
    free_obj_array((CljObject**)params, params_stack);
    
    return fn;
}

ID eval_symbol(ID symbol, EvalState *st) {
    if (!symbol) {
        return NULL;
    }
    
    CljSymbol *sym = as_symbol((ID)symbol);
    
    // Keywords evaluate to themselves
    if (sym && sym->name[0] == ':') {
        return AUTORELEASE(RETAIN(symbol));
    }
    
    // Special handling for *ns* - return current namespace name as symbol
    if (sym && strcmp(sym->name, "*ns*") == 0) {
        if (st && st->current_ns && st->current_ns->name) {
            return st->current_ns->name;  // Return the namespace symbol (e.g., 'user')
        }
        return intern_symbol(NULL, "user");  // Default namespace
    }
    
    // Lookup im aktuellen Namespace
    CljObject *value = ns_resolve(st, symbol);
    if (value) {
        return AUTORELEASE(RETAIN(value));  // Gefunden - retain the value
    }
    
    
    // Fallback: Try global namespace lookup for special forms and builtins
    if (sym) {
        const char *name = sym->name;
        
        // Check against cached symbol pointers for O(1) lookup (only if initialized)
        if ((SYM_IF && symbol == SYM_IF) || (SYM_DEF && symbol == SYM_DEF) || 
            (SYM_DEFN && symbol == SYM_DEFN) || (SYM_FN && symbol == SYM_FN) || 
            (SYM_QUOTE && symbol == SYM_QUOTE) || 
            (SYM_RECUR && symbol == SYM_RECUR) || (SYM_AND && symbol == SYM_AND) || 
            (SYM_OR && symbol == SYM_OR) || (SYM_NS && symbol == SYM_NS) || 
            (SYM_TRY && symbol == SYM_TRY) || (SYM_CATCH && symbol == SYM_CATCH) || 
            (SYM_THROW && symbol == SYM_THROW) || (SYM_FINALLY && symbol == SYM_FINALLY) ||
            (SYM_DO && symbol == SYM_DO) || (SYM_LOOP && symbol == SYM_LOOP) || 
            (SYM_LET && symbol == SYM_LET) || (SYM_PLUS && symbol == SYM_PLUS) || 
            (SYM_MINUS && symbol == SYM_MINUS) || (SYM_MULTIPLY && symbol == SYM_MULTIPLY) || 
            (SYM_DIVIDE && symbol == SYM_DIVIDE) || (SYM_EQUALS && symbol == SYM_EQUALS) || 
            (SYM_LT && symbol == SYM_LT) || (SYM_GT && symbol == SYM_GT) || 
            (SYM_LE && symbol == SYM_LE) || (SYM_GE && symbol == SYM_GE) ||
            (SYM_PRINTLN && symbol == SYM_PRINTLN) || (SYM_PRINT && symbol == SYM_PRINT) || 
            (SYM_STR && symbol == SYM_STR) || (SYM_CONJ && symbol == SYM_CONJ) || 
            (SYM_NTH && symbol == SYM_NTH) || (SYM_FIRST && symbol == SYM_FIRST) || 
            (SYM_REST && symbol == SYM_REST) || (SYM_COUNT && symbol == SYM_COUNT) || 
            (SYM_CONS && symbol == SYM_CONS) || (SYM_SEQ && symbol == SYM_SEQ) || 
            (SYM_NEXT && symbol == SYM_NEXT) || (SYM_LIST && symbol == SYM_LIST) ||
            (SYM_FOR && symbol == SYM_FOR) || (SYM_DOSEQ && symbol == SYM_DOSEQ) || 
            (SYM_DOTIMES && symbol == SYM_DOTIMES)) {
            return AUTORELEASE(RETAIN(symbol));  // Return the symbol itself for special forms
        }
        
        // Fallback: String-based lookup for special forms (slower but works)
        if (strcmp(name, "def") == 0 || strcmp(name, "if") == 0 || strcmp(name, "fn") == 0 ||
            strcmp(name, "quote") == 0 || strcmp(name, "recur") == 0 || strcmp(name, "and") == 0 ||
            strcmp(name, "or") == 0 || strcmp(name, "ns") == 0 || strcmp(name, "try") == 0 ||
            strcmp(name, "catch") == 0 || strcmp(name, "throw") == 0 || strcmp(name, "finally") == 0 ||
            strcmp(name, "do") == 0 || strcmp(name, "loop") == 0 || strcmp(name, "let") == 0 ||
            strcmp(name, "+") == 0 || strcmp(name, "-") == 0 || strcmp(name, "*") == 0 ||
            strcmp(name, "/") == 0 || strcmp(name, "=") == 0 || strcmp(name, "<") == 0 ||
            strcmp(name, ">") == 0 || strcmp(name, "<=") == 0 || strcmp(name, ">=") == 0 ||
            strcmp(name, "println") == 0 || strcmp(name, "print") == 0 || strcmp(name, "str") == 0 ||
            strcmp(name, "conj") == 0 || strcmp(name, "nth") == 0 || strcmp(name, "first") == 0 ||
            strcmp(name, "rest") == 0 || strcmp(name, "count") == 0 || strcmp(name, "cons") == 0 ||
            strcmp(name, "seq") == 0 || strcmp(name, "next") == 0 || strcmp(name, "list") == 0 ||
            strcmp(name, "for") == 0 || strcmp(name, "doseq") == 0 || strcmp(name, "dotimes") == 0) {
            return AUTORELEASE(RETAIN(symbol));  // Return the symbol itself for special forms
        }
    }
    
    // Fehler: Symbol kann nicht aufgelöst werden
    const char *name = sym ? sym->name : "unknown";
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", name);
    return NULL;
}

ID eval_str(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (!arg) return AUTORELEASE(make_string_impl(""));
    
    char *str = pr_str(arg);
    CljObject *result = AUTORELEASE(make_string_impl(str));
    free(str);
    return result;
}

ID eval_prn(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (arg) {
        char *str = pr_str(arg);
        printf("%s\n", str);
        free(str);
    }
    return NULL;
}

ID eval_count(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg_retained(list, 1, env);
    // Handle nil (represented as NULL) - return 0
    if (!arg) return fixnum(0);
    
    // Note: nil is now represented as NULL, so no special nil check needed
    
    switch (arg->type) {
        case CLJ_VECTOR: {
            CljPersistentVector *vec = as_vector((ID)arg);
            return vec ? fixnum(vec->count) : fixnum(0);
        }
        
        case CLJ_LIST: {
            int count = list_count(as_list(arg));
            return fixnum(count);
        }
        
        case CLJ_MAP: {
            CljMap *map = as_map((ID)arg);
            return map ? fixnum(map->count) : fixnum(0);
        }
        
        case CLJ_STRING: {
            // String data is stored directly after CljObject header
            char **str_ptr = (char**)((char*)arg + sizeof(CljObject));
            char *str = *str_ptr;
            // For empty string singleton, str_ptr points directly to the string
            if (arg == empty_string_singleton) {
                return fixnum(0);
            }
            // For other strings, check if str is valid
            if (str) {
                return fixnum(strlen(str));
            } else {
                return fixnum(0);
            }
        }
        
        default:
            // For other types (int, bool, symbol, etc.), return 1
            return fixnum(1);
    }
}

ID eval_first(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (!arg) return NULL;
    
    // Use new seq implementation for unified behavior
    CljObject *seq = seq_create(arg);
    if (!seq) return NULL;
    
    CljObject *result = (CljObject*)seq_first(seq);
    seq_release(seq);
    
    return result ? AUTORELEASE(result) : NULL;
}

/**
 * @brief Evaluate the rest of a sequence (everything except the first element)
 * @param list The function call list containing the argument
 * @param env The environment for variable lookup
 * @return The rest of the sequence as a SeqIterator (autoreleased) or empty list if no rest
 * 
 * Memory Policy:
 * - Returns autoreleased objects to prevent memory leaks
 * - Uses single return pattern for cleaner code structure
 * - SeqIterator objects are managed by the caller
 */
ID eval_rest(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *result = NULL;  // Single result variable for clean return pattern
    CljObject *arg = eval_arg_retained(list, 1, env);  // Get the sequence argument
    
    // Handle NULL argument case - return empty list
    if (!arg) {
        result = make_list(NULL, NULL);  // Create empty list for nil/empty case
    } else {
        // Use new seq implementation for unified behavior across all sequence types
        CljObject *seq = seq_create(arg);  // Create sequence iterator
        if (!seq) {
            // Seq creation failed - return empty list as fallback
            result = make_list(NULL, NULL);
        } else {
            // Get the rest of the sequence (everything except first element)
            CljObject *rest_seq = (CljObject*)seq_rest(seq);
            seq_release(seq);  // Clean up the original sequence iterator
            
            if (!rest_seq) {
                // No rest elements - return empty list
                result = make_list(NULL, NULL);
            } else {
                // Return SeqIterator directly (no conversion needed)
                // SeqIterator is already a CljObject, so we can cast it
                result = (CljObject*)rest_seq;
            }
        }
    }
    
    // Single return point with autorelease for consistent memory management
    return AUTORELEASE(result);
}

ID eval_cons(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *elem = eval_arg_retained(list, 1, env);
    CljObject *coll = eval_arg_retained(list, 2, env);
    
    if (!elem) elem = NULL;
    
    CljObject *result = NULL;
    
    // Fall 1: nil oder leer
    if (!coll) {
        result = (CljObject*)make_list(elem, NULL);
        RELEASE(elem);
        return AUTORELEASE(result);
    }
    
    // Fall 2 & 3: Typ-basierte Behandlung
    switch (coll->type) {
        case CLJ_LIST:
        case CLJ_SEQ: {
            // Bereits CLJ_LIST oder CLJ_SEQ → direkt als rest verwenden
                result = make_list(elem, (CljList*)coll);
            RELEASE(elem);   // Balance: make_list macht RETAIN
            RELEASE(coll);   // Balance: make_list macht RETAIN
            return AUTORELEASE(result);
        }
        
        default: {
            // Fall 3: Vektor oder andere → zu Seq konvertieren
            CljObject *seq = seq_create(coll);  // Heap-Objekt 1
            RELEASE(coll);  // Balance aus eval_arg_retained
            
            if (!seq) {
                // Leere Seq → nur Element
                result = make_list(elem, NULL);
            } else {
                // Seq als rest → make_list macht RETAIN auf seq
                result = make_list(elem, (CljList*)seq);  // Heap-Objekt 2
                RELEASE(seq);  // Balance: make_list macht RETAIN
            }
            
            RELEASE(elem);
            return AUTORELEASE(result);
        }
    }
}

ID eval_seq(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *arg = eval_arg_retained(list, 1, env);
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
            CljObject *seq = AUTORELEASE(seq_create(arg));
            if (!seq) return NULL;
            
            return seq;
        }
    }
}

// ============================================================================
// FOR-LOOP IMPLEMENTATIONS
// ============================================================================

ID eval_for(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    // (for [binding coll] expr)
    // Returns a lazy sequence of results
    
    if (!list) {
        return NULL;
    }
    
    CljObject *binding_list = eval_arg_retained(list, 1, env);
    CljObject *body = eval_arg_retained(list, 2, env);
    
    if (!binding_list || binding_list->type != CLJ_LIST) {
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
    CljObject *result = make_list(NULL, NULL);
    
    // Iterate over collection using seq
    CljObject *seq = seq_create(collection);
    if (seq) {
        while (!seq_empty(seq)) {
            CljObject *element = (CljObject*)seq_first(seq);
            
            // Create new environment with binding using map_assoc
            CljMap *new_env = (CljMap*)make_map(4); // Small capacity for loop environment
            if (new_env) {
                // Copy existing environment bindings
                if (env) {
                    // For now, just use the existing environment
                    // TODO: Implement proper environment copying
                }
                // Add new binding
                map_assoc((CljObject*)new_env, var, element);
                
                // Evaluate body with new binding
                CljObject *body_result = eval_body_with_env(body, new_env);
                if (body_result) {
                    RELEASE(body_result);
                }
                
                // Clean up environment
                RELEASE(new_env);
            }
            
            // Move to next element
            CljObject *next = (CljObject*)seq_next(seq);
            seq_release(seq);
            seq = next;
        }
        // Clean up final seq iterator (not returned as value)
        seq_release(seq);
    }
    
    RELEASE(collection);
    return AUTORELEASE((CljObject*)result);
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
    CljObject *seq = seq_create(collection);
    if (seq) {
        while (!seq_empty(seq)) {
            CljObject *element = (CljObject*)seq_first(seq);
            
            // Create new environment with binding using map_assoc
            CljMap *new_env = (CljMap*)make_map(4); // Small capacity for loop environment
            if (new_env) {
                // Copy existing environment bindings
                if (env) {
                    // For now, just use the existing environment
                    // TODO: Implement proper environment copying
                }
                // Add new binding
                map_assoc((CljObject*)new_env, var, element);
                
                // Evaluate body with new binding
                CljObject *body_result = eval_body_with_env(body, new_env);
                if (body_result) {
                    RELEASE(body_result);
                }
                
                // Clean up environment
                RELEASE(new_env);
            }
            
            
            CljObject *next = (CljObject*)seq_next(seq);
            seq_release(seq);
            seq = next;
        }
        // Clean up final seq iterator
        seq_release(seq);
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
    if (((CljObject*)list)->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list((ID)list);
    
    // Create new list starting from the second element (skip 'list' symbol)
    CljObject *args_list = (CljObject*)LIST_REST(list_data);
    if (!args_list) return NULL;
    
    // Simply return the arguments as a list (they're already evaluated by eval_list)
    return AUTORELEASE(args_list);
}

ID eval_dotimes(CljList *list, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    // (dotimes [var n] expr)
    // Executes expr n times with var bound to 0, 1, ..., n-1
    
    if (!list) {
        return NULL;
    }
    
    // Parse arguments directly without evaluation
    CljList *list_data = as_list((ID)list);
    if (!list_data->rest) {
        return NULL;
    }
    
    CljObject *binding_list = list_data->rest && is_type(list_data->rest, CLJ_LIST) ? as_list(list_data->rest)->first : NULL;
    CljObject *body = list_data->rest && is_type(list_data->rest, CLJ_LIST) && as_list(list_data->rest)->rest && is_type(as_list(list_data->rest)->rest, CLJ_LIST) ? as_list(as_list(list_data->rest)->rest)->first : NULL;
    
    if (!binding_list || binding_list->type != CLJ_LIST) {
        return NULL;
    }
    
    // Parse binding: [var n]
    CljList *binding_data = as_list(binding_list);
    if (!binding_data->first || !binding_data->rest) {
        return NULL;
    }
    
    CljObject *var = binding_data->first;
    CljObject *n_expr = binding_data->rest;
    
    // Get number of iterations
    CljList *n_data = as_list(n_expr);
    if (!n_data->first) {
        return NULL;
    }
    
    CljObject *n_obj = n_data->first; // Simple: just use the expression directly
    if (!is_fixnum((CljValue)n_obj)) {
        RELEASE(n_obj);
        return NULL;
    }
    
    int n = as_fixnum((CljValue)n_obj);
    RELEASE(n_obj);
    
    // Execute body n times
    for (int i = 0; i < n; i++) {
        // Create new environment with binding using map_assoc
        CljMap *new_env = (CljMap*)make_map(4); // Small capacity for loop environment
        if (new_env) {
            // Copy existing environment bindings
            if (env) {
                // For now, just use the existing environment
                // TODO: Implement proper environment copying
            }
            // Add new binding
            map_assoc((CljObject*)new_env, var, fixnum(i));
            
            // Evaluate body with new binding
            CljObject *body_result = eval_body_with_env(body, new_env);
            if (body_result) {
                RELEASE(body_result);
            }
            
            // Clean up environment
            RELEASE(new_env);
        }
    }
    
    return AUTORELEASE(NULL); // dotimes always returns nil
}

// ============================================================================
// EVAL_LET - Let bindings implementation
// ============================================================================
ID eval_let(CljList *list, CljMap *env, EvalState *st) {
    // (let [bindings*] body*)
    // bindings* => binding-form init-expr
    
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    
    if (!list || !st) {
        return NULL;
    }
    
    // Get bindings vector (second element): (let [x 10 y 20] ...)
    CljObject *bindings_vec = list_get_element(list, 1);
    if (!bindings_vec || !is_type(bindings_vec, CLJ_VECTOR)) {
        throw_exception("IllegalArgumentException", 
                       "let requires a vector for bindings", 
                       NULL, 0, 0);
        return NULL;
    }
    
    CljPersistentVector *bindings = as_vector((CljValue)bindings_vec);
    if (!bindings) {
        throw_exception("IllegalArgumentException", 
                       "let bindings must be a valid vector", 
                       NULL, 0, 0);
        return NULL;
    }
    int binding_count = bindings->count;
    
    // Bindings must come in pairs (symbol value symbol value ...)
    if (binding_count % 2 != 0) {
        throw_exception("IllegalArgumentException", 
                       "let requires an even number of forms in binding vector", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Create new environment extending the current one
    // If no env provided, create empty environment
    CljMap *let_env = NULL;
    if (!env) {
        // No parent environment - create new one
        let_env = (CljMap*)make_map(binding_count / 2 + 4);
    } else {
        // Extend existing environment
        let_env = (CljMap*)make_map(binding_count / 2 + env->count);
        if (let_env && env->count > 0) {
            // Copy existing environment bindings
            for (int i = 0; i < env->capacity; i++) {
                CljValue key = env->data[i * 2];
                CljValue val = env->data[i * 2 + 1];
                if (key) {
                    map_assoc((CljObject*)let_env, (CljObject*)key, (CljObject*)val);
                }
            }
        }
    }
    
    if (!let_env) {
        return NULL;
    }
    
    // Process bindings sequentially (each binding can reference previous ones)
    for (int i = 0; i < binding_count; i += 2) {
        CljValue sym_val = bindings->data[i];
        CljValue init_val = bindings->data[i + 1];
        
        if (!sym_val || !is_type((CljObject*)sym_val, CLJ_SYMBOL)) {
            RELEASE(let_env);
            throw_exception("IllegalArgumentException", 
                           "let binding must be a symbol", 
                           NULL, 0, 0);
            return NULL;
        }
        
        if (!init_val) {
            RELEASE(let_env);
            throw_exception("IllegalArgumentException", 
                           "let binding init expression cannot be null", 
                           NULL, 0, 0);
            return NULL;
        }
        
        // Evaluate init expression in the current let environment
        // This allows later bindings to reference earlier ones
        CljObject *value = NULL;
        
        // Check if init_val is an immediate value (doesn't need evaluation)
        if (is_fixnum(init_val) || is_special(init_val)) {
            // Immediate value - use as is
            value = (CljObject*)init_val;
            RETAIN(value);  // Retain for consistency
        } else {
            // Complex expression - evaluate it
            value = eval_body((ID)init_val, let_env, st);
            if (!value) {
                RELEASE(let_env);
                return NULL;
            }
        }
        
        // Add binding to environment
        map_assoc((CljObject*)let_env, (CljObject*)sym_val, value);
        
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
                    result = eval_body(body_expr, let_env, st);
                }
            }
        }
    }
    
    // Clean up environment
    RELEASE(let_env);
    
    return AUTORELEASE(result);
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
    
    // Parse arguments using rest traversal: (defn name [params] body...)
    CljList *rest = list->rest;
    if (!rest) {
        throw_exception("IllegalArgumentException", 
                       "defn requires function name", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Get function name (first element after defn)
    CljObject *name_sym = rest->first;
    if (!name_sym || !is_type(name_sym, CLJ_SYMBOL)) {
        throw_exception("IllegalArgumentException", 
                       "defn requires a symbol for function name", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Get parameter vector (second element after defn)
    rest = rest->rest;
    if (!rest) {
        throw_exception("IllegalArgumentException", 
                       "defn requires parameter vector", 
                       NULL, 0, 0);
        return NULL;
    }
    
    CljObject *params_vec = rest->first;
    if (!params_vec || !is_type(params_vec, CLJ_VECTOR)) {
        throw_exception("IllegalArgumentException", 
                       "defn requires a vector for parameters", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Get body expressions (everything after params)
    rest = rest->rest;
    if (!rest) {
        throw_exception("IllegalArgumentException", 
                       "defn requires at least one body expression", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Extract parameters from vector
    CljPersistentVector *params_vec_data = as_vector((CljValue)params_vec);
    if (!params_vec_data) {
        throw_exception("IllegalArgumentException", 
                       "defn requires a valid parameter vector", 
                       NULL, 0, 0);
        return NULL;
    }
    
    int param_count = params_vec_data->count;
    CljObject *params_stack[16];
    CljObject **params = alloc_obj_array(param_count, params_stack);
    
    for (int i = 0; i < param_count; i++) {
        params[i] = params_vec_data->data[i];
        if (!params[i] || !is_type(params[i], CLJ_SYMBOL)) {
            free_obj_array(params, params_stack);
            throw_exception("IllegalArgumentException", 
                           "defn parameters must be symbols", 
                           NULL, 0, 0);
            return NULL;
        }
    }
    
    // Create fn expression: (fn [params*] body*)
    // We'll create this as a list structure
    
    // Create body list with all expressions
    // For multiple body expressions, we need to create a do block
    CljList *body_list = NULL;
    
    // rest now points to the body expressions
    if (rest->rest == NULL) {
        // Single body expression - use directly
        CljObject *body_expr = rest->first;
        if (body_expr) {
            body_list = make_list(body_expr, NULL);
        }
    } else {
        // Multiple body expressions - just use the last one for now
        // TODO: Implement proper do block or let sequencing
        CljList *current = rest;
        while (current->rest) {
            current = current->rest;
        }
        CljObject *last_expr = current->first;
        if (last_expr) {
            body_list = make_list(last_expr, NULL);
        }
    }
    
    if (!body_list) {
        throw_exception("IllegalArgumentException", 
                       "defn body cannot be empty", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Create fn list: (fn params_vec body_list)
    CljList *fn_list = make_list((CljObject*)SYM_FN, NULL);
    if (!fn_list) return NULL;
    
    // Add params vector as second element
    fn_list->rest = (CljObject*)make_list(params_vec, NULL);
    
    // Add body as third element
    CljList *fn_rest = as_list(fn_list->rest);
    if (fn_rest) {
        fn_rest->rest = (CljObject*)body_list;
    }
    
    // Create def expression: (def name_sym fn_list)
    CljList *def_list = make_list((CljObject*)SYM_DEF, NULL);
    if (!def_list) return NULL;
    
    def_list->rest = (CljObject*)make_list(name_sym, NULL);
    CljList *def_rest = as_list(def_list->rest);
    if (def_rest) {
        def_rest->rest = (CljObject*)make_list((CljObject*)fn_list, NULL);
    }
    
    // Create function object directly
    CljObject *fn_obj = make_function((CljObject**)params, param_count, body_list, (CljObject*)env, NULL);
    if (!fn_obj) {
        RELEASE(fn_list);
        RELEASE(def_list);
        return NULL;
    }
    
    // CRITICAL: Add function to its own closure_env for recursive calls
    // This allows recursive functions to find themselves
    if (is_type(fn_obj, CLJ_CLOSURE)) {
        CljFunction *func = as_function((ID)fn_obj);
        if (func && func->closure_env) {
            // map_assoc_cow returns a new map, so we need to update the function's closure_env
            CljObject *new_closure_env = map_assoc_cow(func->closure_env, name_sym, fn_obj);
            if (new_closure_env) {
                RELEASE(func->closure_env);
                func->closure_env = RETAIN(new_closure_env);
            }
        }
    }
    
    // Now evaluate the def expression (which will also add to environment)
    CljObject *result = eval_def(def_list, env, st);
    
    RELEASE(fn_obj);
    RELEASE(fn_list);
    RELEASE(def_list);
    free_obj_array(params, params_stack);
    return AUTORELEASE(result);
}

// Helper function for evaluating arguments with automatic retention
ID eval_arg_retained(CljList *list, int index, CljMap *env) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CljObject *result = eval_arg(list, index, env);
    if (result) RETAIN(result);
    return result;
}

// Thread-local recursion depth tracking for eval_arg
static _Thread_local int g_eval_arg_depth = 0;

// Helper function for evaluating arguments
ID eval_arg(CljList *list, int index, CljMap *env) {
    // Assertion: List must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    if (((CljObject*)list)->type != CLJ_LIST) return NULL;
    
    // Handle NULL environment gracefully
    if (env == NULL) {
        // Return the element as-is if no environment is available
        CljObject *element = (CljObject*)list_nth(as_list((ID)list), index);
        return element ? RETAIN(element) : NULL;
    }
    
    // Use the existing list_nth function which is safer
    CljObject *element = (CljObject*)list_nth(as_list((ID)list), index);
    if (!element) return NULL;
    
    // For simple types (numbers, strings, booleans), return as-is
    if (IS_IMMEDIATE(element) || is_type(element, CLJ_STRING)) {
        return element; // Don't retain - caller will handle retention
    }
    
    // For symbols, resolve them from environment
    if (is_type(element, CLJ_SYMBOL)) {
        if (env) {
            CljObject *resolved = (CljObject*)env_get_stack((CljObject*)env, element);
            if (resolved) return resolved;
        }
        // If not found, return the symbol as-is
        return element;
    }
    
    // For lists, evaluate them recursively with depth protection
    if (is_type(element, CLJ_LIST)) {
        // Check recursion depth
        if (g_eval_arg_depth >= MAX_CALL_STACK_DEPTH) {
            throw_exception("StackOverflowError", 
                          "Maximum evaluation depth exceeded in nested function calls", 
                          __FILE__, __LINE__, 0);
            return NULL;
        }
        
        // Evaluate nested list with depth tracking
        g_eval_arg_depth++;
        EvalState *st = evalstate();
        CljObject *result = eval_list(as_list((ID)element), env, st);
        g_eval_arg_depth--;
        
        return result;
    }
    
    // For vectors, maps, etc., return as-is
    return element; // Don't retain - caller will handle retention
}


// is_symbol is already defined in namespace.c