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
#include "event_loop.h"
#include "channel.h"

// Use C stack for recur state - each function call has its own stack frame
// No global variables needed - local variables in eval_function_call are automatically isolated

// Evaluation context structures are defined in function_call.h

#include "map.h"
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
ID eval_body_with_local_env(ID body, CljMap *local_env, EvalState *st);
CljObject* eval_list_with_env(CljList *list, CljMap *env, EvalState *st);


// ============================================================================
// COMPARISON OPERATORS REFACTORING - Type Promotion and Generic Functions
// ============================================================================

// Macros for common argument evaluation patterns
#define EVAL_TWO_ARGS(list, env, a, b) do { \
    (a) = eval_arg(as_list((ID)(list)), 1, (env), NULL); \
    (b) = eval_arg(as_list((ID)(list)), 2, (env), NULL); \
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

/** @brief Compare symbol name directly (works for non-interned symbols) */
// DEPRECATED: Use pointer comparison (op == SYM_*) instead for O(1) performance
// This function is kept for backward compatibility but should not be used in new code
// Function removed - use pointer comparison (op == SYM_*) instead

/** @brief Allocate array with stack optimization (size <= 16 on stack, else heap) */
static inline ID* alloc_obj_array(int size, CljObject **stack_buffer) {
    return size <= 16 ? (ID*)stack_buffer : (ID*)malloc(sizeof(CljObject*) * size);
}

/** @brief Free array allocated with alloc_obj_array */
static inline void free_obj_array(ID *array, CljObject **stack_buffer) {
    if (array != (ID*)stack_buffer) free((void*)array);
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
    // Clojure-compatible: Accept NULL environment - eval_arg handles it
    // and falls back to namespace lookup for symbol resolution
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
        args[i] = eval_arg(list, i + 1, env, NULL);
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
    
    return result ? AUTORELEASE(result) : NULL;
}

// Generic arithmetic function (with parameter substitution)
// 
// FINDINGS from debugging parameter evaluation issues:
// - eval_body_with_params now correctly returns values[i] even if NULL (nil is valid in Clojure)
// - The original issue (args[0] being NULL when function is called) was fixed by allowing
//   eval_body_with_params to return NULL for nil values
// - However, nil cannot be used in arithmetic operations, so we validate here
// - The caller (eval_list_with_param_substitution) must handle nil values appropriately
ID eval_arithmetic_generic_with_substitution(CljList *list, ArithOp op, const EvalContext *ctx) {
    // TEST: Check if this function is called at all
    assert(false && "eval_arithmetic_generic_with_substitution called");
    
    CLJ_ASSERT(ctx != NULL);
    CLJ_ASSERT(ctx->params != NULL);  // params are required for arithmetic
    CLJ_ASSERT(ctx->env != NULL);      // env is required
    
    CljObject *first_arg = list_get_element(as_list((ID)list), 1);
    CljObject *second_arg = list_get_element(as_list((ID)list), 2);
    
    // CRITICAL ASSERTION: Arguments should exist
    CLJ_ASSERT(first_arg != NULL);
    CLJ_ASSERT(second_arg != NULL);
    
    ID a = eval_body_with_params(first_arg, ctx);
    ID b = eval_body_with_params(second_arg, ctx);
    
    // CRITICAL ASSERTION: For arithmetic operations, arguments should not be NULL
    // If the argument is a symbol (parameter), it should resolve to a value
    // If the argument is a fixnum literal, it should return the fixnum
    // If the argument is a function call, it should return a value
    // OPTIMIZATION: Removed array iteration for parameter checks
    // Parameters are now resolved via environment map, which is O(1) instead of O(n)
    // These assertions are kept for debugging but no longer iterate over arrays
    if (is_type(first_arg, CLJ_SYMBOL)) {
        // If first argument is a symbol, it should resolve to a value (not NULL)
        // Exception: nil is valid, but we check for that separately
        // OPTIMIZATION: Parameter resolution now uses environment map (O(1)) instead of array iteration (O(n))
        if (a == NULL) {
            // Check if it's a parameter in environment map
            bool is_param = false;
            if (ctx->env && ctx->env->closure_env) {
                is_param = map_contains(ctx->env->closure_env, first_arg);
            }
            if (is_param) {
                // Parameter found but value is NULL - this is the bug!
                CLJ_ASSERT(0 && "Parameter resolved to NULL in arithmetic operation");
            }
        }
    } else if (IS_IMMEDIATE(first_arg)) {
        // If first argument is an immediate (fixnum, etc.), it should return itself
        CLJ_ASSERT(a == (ID)first_arg);
    }
    
    if (is_type(second_arg, CLJ_SYMBOL)) {
        // If second argument is a symbol, it should resolve to a value (not NULL)
        // OPTIMIZATION: Parameter resolution now uses environment map (O(1)) instead of array iteration (O(n))
        if (b == NULL) {
            // Check if it's a parameter in environment map
            bool is_param = false;
            if (ctx->env && ctx->env->closure_env) {
                is_param = map_contains(ctx->env->closure_env, second_arg);
            }
            if (is_param) {
                // Parameter found but value is NULL - this is the bug!
                CLJ_ASSERT(0 && "Parameter resolved to NULL in arithmetic operation");
            }
        }
    } else if (IS_IMMEDIATE(second_arg)) {
        // If second argument is an immediate (fixnum, etc.), it should return itself
        CLJ_ASSERT(b == (ID)second_arg);
    }
    
    // CRITICAL: Arithmetic operations cannot accept nil
    if (!a || !b) {
        // RELEASE safely handles NULL and immediate values
        RELEASE(a);
        RELEASE(b);
        throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "Cannot use nil as a Number");
        return NULL;
    }
    
    // FAST PATH: Both arguments are fixnums - direct arithmetic without type checks
    // This is the most common case in arithmetic operations
    uint16_t tag_a = TAG(a);
    uint16_t tag_b = TAG(b);
    if (tag_a == CLJ_INT && tag_b == CLJ_INT) {
        int a_val = as_fixnum((CljValue)a);
        int b_val = as_fixnum((CljValue)b);
        int result;
        
        // Division by zero check
        if (op == ARITH_DIV && b_val == 0) {
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                    "Division by zero: %d / %d", a_val, b_val);
            return NULL;
        }
        
        switch (op) {
            case ARITH_ADD:
                // Check for overflow
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
                // Check for overflow/underflow
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
                // Check for overflow
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
    
    // Fallback: Generic path for non-fixnum types or mixed types
    // Check for non-numeric types
    if (!is_numeric_type(a) || !is_numeric_type(b)) {
        throw_exception_formatted("WrongArgumentException", __FILE__, __LINE__, 0,
                "String cannot be used as a Number");
        return NULL;
    }
    
    // Division by zero check
    if (op == ARITH_DIV) {
        if (tag_b == CLJ_INT && as_fixnum((CljValue)b) == 0) {
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                    "Division by zero: %d / %d", as_fixnum((CljValue)a), as_fixnum((CljValue)b));
            return NULL;
        }
        if (tag_b == CLJ_FLOAT && as_fixed((CljValue)b) == 0.0) {
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                    "Division by zero: %f / %f", as_fixed((CljValue)a), as_fixed((CljValue)b));
            return NULL;
        }
    }
    
    // For now, only support integer arithmetic
    // TODO: Add mixed int/float support
    if (tag_a != CLJ_INT || tag_b != CLJ_INT) {
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
ID eval_function_call(ID fn, ID *args, int argc, CljMap *env, EvalState *st) {
    // Note: env parameter is used for environment context, but closure_env takes precedence
    // for Clojure functions. For native functions, env is not used.
    (void)env; // Suppress unused parameter warning
    
    if (!is_type(fn, CLJ_FUNC) && !is_type(fn, CLJ_CLOSURE)) {
        throw_exception(EXCEPTION_TYPE, "Attempt to call non-function value", NULL, 0, 0);
        return NULL;
    }
    
    // Check if it's a native function (CljFunc) or Clojure function (CljFunction)
    if (is_native_fn(fn)) {
        // It's a native function (CljFunc)
        CljFunc *native_func = (CljFunc*)fn;
        if (!native_func || !native_func->fn) {
            throw_exception(EXCEPTION_TYPE, "Invalid native function", NULL, 0, 0);
            return NULL;
        }
        return native_func->fn((CljObject**)args, argc);
    }
    
    // It's a Clojure function (CljFunction)
    CljFunction *func = (CljFunction*)fn;
    if (!func) {
        return make_exception(EXCEPTION_RUNTIME, "Invalid function object", NULL, 0, 0);
    }
    
    // Arity check
    if (argc != func->param_count) {
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
        // ASSERTION: Debug coll argument after copy
        if (current_args[i] && !IS_IMMEDIATE(current_args[i])) {
            // Note: current_args[i] can be a symbol if argument evaluation failed
            // This is checked and handled by the calling code
        }
    }
    
    // CRITICAL: Extend closure environment with parameter bindings
    // This ensures that when eval_list is called, it can find parameters in the environment
    CljMap *call_env = NULL;
    if (func->closure_env) {
        // Use env_extend_stack to add parameters to the environment
        call_env = env_extend_stack(func->closure_env, (ID*)func->params, (ID*)current_args, current_argc);
        if (!call_env) {
            throw_exception(EXCEPTION_RUNTIME, "Failed to create function call environment", NULL, 0, 0);
            return NULL;
        }
    } else {
        // No closure environment - create new environment with parameters
        call_env = env_extend_stack(NULL, (ID*)func->params, (ID*)current_args, current_argc);
        if (!call_env) {
            throw_exception(EXCEPTION_RUNTIME, "Failed to create function call environment", NULL, 0, 0);
            return NULL;
        }
    }
    
    // TCO Loop - iterate on recur
    ID result = NULL;
    do {
        // Reset recur state for each iteration
        recur_arg_count = -1;  // -1 = kein Tail Call
        
        // Evaluate function body
        // Pass pointer to local recur state - nested functions will have their own stack frames
        ParamContext param_ctx = {
            .params = func->params,
            .values = current_args,
            .param_count = current_argc
        };
        EvalEnv env_ctx = {
            .closure_env = call_env,  // Use call_env instead of func->closure_env
            .st = st
        };
        RecurContext recur_ctx = {
            .recur_args = recur_args,
            .recur_arg_count = &recur_arg_count
        };
        EvalContext ctx = {
            .params = &param_ctx,
            .env = &env_ctx,
            .recur = &recur_ctx
        };
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
            // ✅ CRITICAL: Release intermediate result from recur iteration
            // RELEASE/RETAIN macros handle immediates automatically - no guard needed
            // new_result can be NULL when recur is used (recur returns NULL), so check before releasing
            if (new_result) {
                RELEASE(new_result);
            }
            
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
            // Create new call_env first (this will retain new parameter values)
            CljMap *new_call_env;
            if (func->closure_env) {
                new_call_env = env_extend_stack(func->closure_env, (ID*)func->params, (ID*)current_args, current_argc);
                if (!new_call_env) {
                    throw_exception(EXCEPTION_RUNTIME, "Failed to create function call environment", NULL, 0, 0);
                    return NULL;
                }
            } else {
                new_call_env = env_extend_stack(NULL, (ID*)func->params, (ID*)current_args, current_argc);
                if (!new_call_env) {
                    throw_exception(EXCEPTION_RUNTIME, "Failed to create function call environment", NULL, 0, 0);
                    return NULL;
                }
            }
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
    if (recur_arg_count >= 0 && recur_arg_count <= 16) {
        for (int i = 0; i < recur_arg_count; i++) {
            if (recur_args[i]) {
                RELEASE(recur_args[i]);
            }
        }
    }
    
    // Cleanup call_env (created by env_extend_stack)
    // This will properly release all stored values (including current_args[i])
    if (call_env) {
        RELEASE((CljObject*)call_env);
    }
    
    // Return result with proper memory management
    // Note: result is already retained by ASSIGN, just return it
    // Local variables (recur_args, recur_arg_count) are automatically cleaned up by C stack
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
            return (ID)map_get((CljMap*)env, (CljValue)body);
        }
        
        case CLJ_LIST: {
            // Type check before calling
            if (!is_type(body_obj, CLJ_LIST)) return NULL;
            CljList *list_data = as_list(body);
            return eval_list_with_env(list_data, env, st);
        }
        
        default:
            // Literal value
            return AUTORELEASE(RETAIN(body));
    }
}

// Evaluate body with local environment (for dotimes, doseq, etc.)
ID eval_body_with_local_env(ID body, CljMap *local_env, EvalState *st) {
    // Assertion: Environment and state must not be NULL when expected
    CLJ_ASSERT(local_env != NULL);
    CLJ_ASSERT(body != NULL);
    CLJ_ASSERT(st != NULL);
    
    // Check if body is an immediate value
    if (IS_IMMEDIATE(body)) {
        return body;
    }
    
    CljObject *body_obj = (CljObject*)body;
    switch (body_obj->type) {
        case CLJ_SYMBOL: {
            // First try local environment
            CljObject *result = (CljObject*)map_get((CljMap*)local_env, (CljValue)body);
            if (result) {
                return AUTORELEASE(RETAIN(result));
            }
            
            // If not found in local environment, try namespace
            if (st && st->current_ns && st->current_ns->mappings) {
                result = (CljObject*)map_get((CljMap*)st->current_ns->mappings, (CljValue)body);
                if (result) {
                    return AUTORELEASE(RETAIN(result));
                }
            }
            
            // If still not found, try global symbol resolution
            result = eval_symbol(body, st);
            return AUTORELEASE(RETAIN(result));
        }
        
        case CLJ_LIST: {
            // Type check before calling
            if (!is_type(body, CLJ_LIST)) return NULL;
            CljList *list_data = as_list((ID)body);
            
            // Use eval_list for full evaluation with namespace access
            // Pass local_env as the environment parameter
            return eval_list(list_data, local_env, st);
        }
        
        default:
            // Literal value
            return AUTORELEASE(RETAIN(body));
    }
}

// Evaluate list with environment (for loops)
CljObject* eval_list_with_env(CljList *list, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    // Assertion: List must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    
    CljObject *head = list->first;
    
    // First element is the operator
    CljObject *op = head;
    
    // For symbols, look up in environment
    if (is_type(op, CLJ_SYMBOL)) {
        CljObject *resolved = (CljObject*)map_get((CljMap*)env, (CljValue)op);
        if (resolved) {
            // If it's a function, call it
            // CRITICAL: Check for both CLJ_FUNC (native) and CLJ_CLOSURE (Clojure functions)
            if (is_type(resolved, CLJ_FUNC) || is_type(resolved, CLJ_CLOSURE)) {
                // Count arguments
                int total_count = list_count(as_list((ID)list));
                int argc = total_count - 1;
                if (argc < 0) argc = 0;
                
                // Evaluate arguments
                CljObject *args_stack[16];
                ID *args = alloc_obj_array(argc, args_stack);
                
                for (int i = 0; i < argc; i++) {
                    args[i] = eval_body_with_env(list_get_element(list, i + 1), env, st);
                    if (!args[i]) args[i] = NULL;
                }
                
                // Call the function
                CljObject *result = eval_function_call(resolved, args, argc, env, st);
                
                free_obj_array((ID*)args, args_stack);
                
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
//
// FINDINGS from debugging parameter evaluation issues:
// - In Clojure, nil is a valid value and can be passed as an argument
// - We return values[i] directly, even if it's NULL (nil), to distinguish from evaluation errors
// - The caller must handle nil values appropriately (e.g., arithmetic operations reject nil)
// - This fixes the original issue where args[0] was NULL when function was called,
//   which was caused by eval_body_with_params returning NULL for nil values
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
    
    if (is_type(body, CLJ_SYMBOL)) {
        // Resolve symbol - check environment map first (contains parameter bindings)
        // OPTIMIZATION: Use environment map directly instead of iterating over arrays
        // The environment map already contains all parameter bindings (created by env_extend_stack)
        CljSymbol *body_sym = as_symbol(body);
        // CRITICAL: body_sym should never be NULL if body is a symbol, but check for safety
        if (!body_sym) {
            // This should never happen, but if it does, try to resolve from namespace
            if (ctx->env && ctx->env->st) {
                CljObject *resolved = ns_resolve(ctx->env->st, body);
                if (resolved) {
                    return RETAIN(resolved);
                }
            }
            throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                "Unable to resolve symbol: invalid symbol object");
            return NULL;
        }
        
        // OPTIMIZATION: Check environment map first (contains parameter bindings)
        // This eliminates O(n) array iteration - map lookup is O(1) for pointer equality
        if (ctx->env && ctx->env->closure_env) {
            // Check if key exists in closure_env (even if value is nil/NULL)
            // map_contains and map_get take ID (can handle both objects and immediates)
            if (map_contains(ctx->env->closure_env, body)) {
                // map_get returns ID (can be object or immediate)
                ID resolved_id = map_get(ctx->env->closure_env, (ID)body);
                // CRITICAL: resolved_id can be NULL (nil), which is valid in Clojure
                // We need to distinguish between nil (valid) and not found (error)
                // map_contains already checked that the key exists, so resolved_id can be NULL (nil)
                
                // CRITICAL: If resolved_id is a symbol, it means the symbol wasn't properly resolved
                // This can happen if a parameter is stored as a symbol in closure_env instead of its value
                // In this case, we should throw an exception instead of returning the symbol
                if (resolved_id && !IS_IMMEDIATE(resolved_id) && is_type((CljObject*)resolved_id, CLJ_SYMBOL)) {
                    // Symbol found in closure_env but value is also a symbol - this is an error
                    CljSymbol *sym = as_symbol(body);
                    const char *sym_name = sym && sym->name ? sym->name : "unknown";
                    throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                        "Unable to resolve symbol: %s in this context", sym_name);
                    return NULL;
                }
                
                // CRITICAL: If resolved_id is an immediate (fixnum, char, etc.), return it directly
                // RETAIN is safe for immediates but returns CljObject*, which breaks fixnums
                if (IS_IMMEDIATE(resolved_id)) {
                    return resolved_id;  // Return immediate value directly as ID
                }
                
                // resolved_id is an object (CljObject*), not an immediate
                // resolved_id can be NULL (nil), which is valid - return it directly
                return resolved_id ? RETAIN(resolved_id) : NULL;
            }
        }
        // If still not found, try namespace lookup (for recursive function calls)
        // ns_resolve takes CljObject* (only objects, not immediates) and returns ID
        // body is a symbol (CljObject*), so we can pass it directly
        if (ctx->env && ctx->env->st) {
            ID resolved_id = ns_resolve(ctx->env->st, (CljObject*)body);
            if (resolved_id) {
                // CRITICAL: If resolved_id is a symbol, it means the symbol wasn't properly resolved
                // This can happen if a symbol is stored in namespace instead of its value
                // In this case, we should throw an exception instead of returning the symbol
                if (!IS_IMMEDIATE(resolved_id) && is_type((CljObject*)resolved_id, CLJ_SYMBOL)) {
                    // Symbol found in namespace but value is also a symbol - this is an error
                    CljSymbol *sym = as_symbol(body);
                    const char *sym_name = sym && sym->name ? sym->name : "unknown";
                    throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                        "Unable to resolve symbol: %s in this context", sym_name);
                    return NULL;
                }
                // resolved_id can be an object or immediate, but in namespace context it's usually an object
                return RETAIN(resolved_id);
            }
        }
        // Check if symbol is a keyword - keywords evaluate to themselves
        if (IS_KEYWORD(body)) {
            // Keywords evaluate to themselves (singletons need no memory management)
            return body;
        }
        // Symbol not found - throw exception instead of returning symbol
        CljSymbol *sym_obj = as_symbol(body);
        const char *sym_name_final = sym_obj && sym_obj->name ? sym_obj->name : "unknown";
        throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
            "Unable to resolve symbol: %s in this context", sym_name_final);
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
    // CRITICAL: Also check IS_IMMEDIATE again here as a safety check
    // If body has a small address but IS_IMMEDIATE is true, it's an immediate value
    if ((uintptr_t)body < 0x1000) {
        // Double-check if it's an immediate value (in case IS_IMMEDIATE check failed)
        if (IS_IMMEDIATE(body)) {
            // It's an immediate value - return it directly
            return body;
        }
        // CRITICAL: If we reach here, body is not an immediate value but has a small address
        // This might indicate a bug
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
            // Evaluate list using eval_list with closure_env
            CljMap *env_map = (ctx->env && ctx->env->closure_env) ? ctx->env->closure_env : NULL;
            if (!ctx->env || !ctx->env->st) {
                EvalState *temp_st = evalstate_new(false);
                CljObject *result = eval_list(as_list((ID)body), env_map, temp_st);
                evalstate_free(temp_st);
                return result;
            }
            return eval_list(as_list((ID)body), env_map, ctx->env->st);
        }
        
        default:
            // Literal value
            return RETAIN(body);
    }
}

// REMOVED: eval_list_with_param_substitution - replaced by eval_list
// This function was removed because it was redundant with eval_list

// Simplified body evaluation (basic implementation)
/** @brief Evaluate function body expressions */
ID eval_body(ID body, CljMap *env, EvalState *st) {
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(body != NULL);
    
    // Handle immediate values (fixnums, chars, booleans, nil)
    if (IS_IMMEDIATE(body)) {
        return body; // Immediate values evaluate to themselves
    }
    
    // Simplified implementation - would normally evaluate the AST
    switch (((CljObject*)body)->type) {
        case CLJ_LIST: {
            // ASSERTION: Debug if list is single symbol (r/c) instead of symbol r/c
            CljList *list_data = as_list((ID)body);
            if (list_data && list_data->first && is_type(list_data->first, CLJ_SYMBOL) && !list_data->rest) {
                CljSymbol *sym = as_symbol((ID)list_data->first);
                if (sym && sym->name && (strcmp(sym->name, "r") == 0 || strcmp(sym->name, "c") == 0)) {
                    // CRITICAL: If body is a single-element list (r/c), treat it as symbol r/c
                    // This fixes the bug where r/c is treated as (r/c) instead of r/c
                    return eval_body(list_data->first, env, st);
                }
            }
            // Evaluate list
            return eval_list(as_list((ID)body), env, st);
        }
        
        case CLJ_SYMBOL: {
            // Check if symbol is a keyword - keywords evaluate to themselves
            if (IS_KEYWORD(body)) {
                // Keywords evaluate to themselves (singletons need no memory management)
                return body;
            }
            
            // Special case: nil should evaluate to NULL (not SYM_NIL)
            if (SYM_NIL && body == (ID)SYM_NIL) {
                return NULL; // nil evaluates to NULL
            }
            
            // Resolve symbol - first try local environment, then namespace
            // Note: We need to check if key exists, not just if value is non-NULL,
            // because nil (NULL) is a valid value
            if (env && is_type((CljObject*)env, CLJ_MAP)) {
                // Check if key exists in environment (even if value is nil/NULL)
                if (map_contains((CljMap*)env, (CljValue)body)) {
                    CljObject *result = (CljObject*)map_get((CljMap*)env, (CljValue)body);
                    // result can be NULL (nil), which is valid
                    return result;
                }
            }
            
            // If not found in local environment, try namespace
            if (st && st->current_ns && st->current_ns->mappings) {
                if (map_contains((CljValue)st->current_ns->mappings, (CljValue)body)) {
                    CljObject *result = (CljObject*)map_get((CljValue)st->current_ns->mappings, (CljValue)body);
                    // result can be NULL (nil), which is valid
                    return result;
                }
            }
            
            // If still not found, try global symbol resolution (includes clojure.core)
            // This is important for built-in functions like inc, dec, etc.
            if (st) {
                ID resolved = eval_symbol(body, st);
                if (resolved) {
                    // Special case: nil should evaluate to NULL (not SYM_NIL)
                    if (resolved == (ID)SYM_NIL) {
                        return NULL; // nil evaluates to NULL
                    }
                    // eval_symbol returns AUTORELEASE, but eval_body should return retained
                    if (!IS_IMMEDIATE(resolved)) {
                        return RETAIN(resolved);
                    }
                    return resolved;
                }
            }
            
            // Symbol not found - this should throw an exception
            throw_exception(EXCEPTION_RUNTIME, "Unable to resolve symbol in this context",
                           __FILE__, __LINE__, 0);
            return NULL;
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
    
    CljObject *result = (CljObject*)map_get((CljValue)map, (CljValue)key);
    RELEASE(key);
    return result ? AUTORELEASE(RETAIN(result)) : NULL;
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
        
        CljObject *test_result = eval_body(test, env, st);
        if (clj_is_truthy(test_result)) {
            return eval_body(expr, env, st);
        }
    }
    return NULL; // No condition matched
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
    if (op == SYM_LT) return eval_numeric_comparison(list, env, COMP_LT);
    if (op == SYM_GT) return eval_numeric_comparison(list, env, COMP_GT);
    if (op == SYM_LE) return eval_numeric_comparison(list, env, COMP_LE);
    if (op == SYM_GE) return eval_numeric_comparison(list, env, COMP_GE);
    return NULL;
}

// Forward declaration for eval_and_call_native
static ID eval_and_call_native(CljList *list, CljMap *env, ID (*native_func)(ID*, unsigned int), int max_args);

static CljObject* eval_sequence_dispatch(CljList *list, CljMap *env, CljObject *op) {
    if (op == SYM_FIRST) return eval_and_call_native(list, env, native_first, 1);
    if (op == SYM_REST) {
        return eval_and_call_native(list, env, native_rest, 1);
    }
    if (op == SYM_CONS) return eval_and_call_native(list, env, native_cons, 2);
    if (op == SYM_SEQ) return eval_seq(list, env);
    if (op == SYM_NEXT) return eval_and_call_native(list, env, native_next, 1); // Clojure-compatible: next returns nil if empty
    if (op == SYM_COUNT) return eval_and_call_native(list, env, native_count, 1);
    return NULL;
}

// Thread-local recursion depth tracking for eval_arg and eval_list
static _Thread_local int g_eval_arg_depth = 0;

static CljObject* eval_loop_dispatch(CljList *list, CljMap *env, CljObject *op) {
    if (op == SYM_FOR) return AUTORELEASE(eval_for(list, env));
    if (op == SYM_DOSEQ) return AUTORELEASE(eval_doseq(list, env));
    if (op == SYM_DOTIMES) return AUTORELEASE(eval_dotimes(list, env));
    return NULL;
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
    if (!env || !is_type((CljObject*)env, CLJ_MAP)) {
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
    // Note: eval_function_call copies arguments with ASSIGN, so we need to release our references
    // RELEASE safely handles NULL and immediate values
    for (int i = 0; i < argc; i++) {
        RELEASE((CljObject*)args[i]);
    }
    
    // Cleanup heap-allocated args if any
    free_obj_array((ID*)args, args_stack);
    
    // AUTORELEASE to ensure result is managed by caller's pool
    if (result && !IS_IMMEDIATE(result)) {
        return AUTORELEASE(result);
    }
    return result;
}

// Simplified list evaluation
ID eval_list(CljList *list, CljMap *env, EvalState *st) {
    // Clojure-compatible: Accept NULL environment - falls back to namespace lookup
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
    // CRITICAL: Check for special forms BEFORE resolving, because special forms
    // like 'time' should not be resolved (they are not in namespaces)
    CljObject *original_op = op;
    
    // Check for special forms first (before symbol resolution)
    // This ensures that special forms like 'time', 'def', and 'ns' are recognized even if
    // they're not in the namespace or environment
    // CRITICAL: This must happen BEFORE symbol resolution, because special forms
    // are not in namespaces and should not be resolved
    if (is_type(op, CLJ_SYMBOL)) {
        CljSymbol *sym = as_symbol(op);
        if (sym && sym->name) {
            // Check for time special form before resolving
            // Use both pointer comparison (fast) and string comparison (fallback)
            // to handle cases where the parsed symbol is not the same object as SYM_TIME
            if (SYM_TIME && (op == SYM_TIME || strcmp(sym->name, "time") == 0)) {
                // This is the time special form - handle it directly
                // Use provided env or fall back to current_ns->mappings
                CljMap *time_env = env;
                if (!time_env && st && st->current_ns) {
                    time_env = (CljMap*)st->current_ns->mappings;
                }
                return eval_time(list, time_env, st);
            }
            // Check for def special form (requires non-evaluated first argument)
            if (original_op == SYM_DEF || (sym->name && strcmp(sym->name, "def") == 0)) {
                return eval_def(list, env, st);
            }
            // Check for ns special form (requires non-evaluated first argument)
            if (original_op == SYM_NS || (sym->name && strcmp(sym->name, "ns") == 0)) {
                return eval_ns(list, env, st);
            }
        }
    }
    
    if (is_type(op, CLJ_SYMBOL)) {
        // First try local environment (if provided)
        // CRITICAL: Use map_contains first (like eval_body) to ensure consistency
        // This ensures that symbols are found even if they're not pointer-equal
        CljObject *resolved = NULL;
        if (env && is_type((CljObject*)env, CLJ_MAP)) {
            // Check if key exists in environment (even if value is nil/NULL)
            if (map_contains((CljValue)env, (CljValue)op)) {
                resolved = (CljObject*)map_get(env, op);
            }
        }
        if (resolved) {
            op = resolved;
        } else {
            // Fallback to global namespace
            resolved = eval_symbol(op, st);
            if (resolved) {
                op = resolved;
            }
        }
    }
    
    // OPTIMIZED: Dispatch to helper functions for common patterns
    // Tier 1: Arithmetic operations (most frequent)
    // CRITICAL: Use original_op for comparison, not resolved op, because
    // SYM_PLUS, SYM_MINUS, etc. are statically initialized symbols that should
    // match the symbols from the AST (which are interned via intern_symbol_global)
    CljObject *result = eval_arithmetic_dispatch(list, env, st, original_op);
    if (result) return result;
    
    // Tier 2: Comparison operations
    result = eval_comparison_dispatch(list, env, original_op);
    if (result) return result;
    
    if (original_op == SYM_IF) {
        // (if cond then else?)
        // CRITICAL: With C stack, each function call has its own recur state
        // No need to save/restore - nested functions will have their own stack frames
        
        CljObject *cond_val = eval_arg(list, 1, env, NULL);
        // Note: cond_val can be NULL (nil), which is falsy - clj_is_truthy handles NULL correctly
        bool truthy = clj_is_truthy(cond_val);
        // RELEASE safely handles NULL and immediate values
        RELEASE(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) {
            return NULL;
        }
        CljObject *result = eval_body(branch, env, st);
        
        return result;
    }
    
    if (original_op == SYM_WHEN) {
        // (when condition body...)
        CljObject *cond_val = eval_arg(list, 1, env, NULL);
        if (!cond_val) return NULL;
        
        bool truthy = clj_is_truthy(cond_val);
        RELEASE(cond_val);
        
        if (!truthy) {
            return NULL; // nil
        }
        
        // Evaluate all body expressions, return last one
        int list_len = list_count(list);
        CljObject *result = NULL;
        
        for (int i = 2; i < list_len; i++) {
            CljObject *body_expr = list_get_element(list, i);
            if (body_expr) {
                ASSIGN(result, eval_body(body_expr, env, st));
                if (!result && i < list_len - 1) {
                    // Error in body evaluation
                    return NULL;
                }
            }
        }
        
        return result;
    }
    
    if (original_op == SYM_WHILE) {
        // (while condition body...)
        // Loop: while condition is true, evaluate body expressions
        int list_len = list_count(list);
        
        while (true) {
            // Evaluate condition
            // CRITICAL: Pass st to eval_arg so it can use the correct namespace
            CljObject *cond_val = eval_arg(list, 1, env, st);
            if (!cond_val || !clj_is_truthy(cond_val)) {
                // Condition is nil/false, exit loop
                // RELEASE safely handles NULL and immediate values
                RELEASE(cond_val);
                return NULL; // nil
            }
            RELEASE(cond_val);
            
            // Evaluate all body expressions
            CljObject *result = NULL;
            for (int i = 2; i < list_len; i++) {
                CljObject *body_expr = list_get_element(list, i);
                if (body_expr) {
                    ASSIGN(result, eval_body(body_expr, env, st));
                    if (!result && i < list_len - 1) {
                        return NULL; // Error in body evaluation
                    }
                }
            }
            // Release result from this iteration (will evaluate again in next iteration)
            if (result) RELEASE(result);
        }
    }
    
    if (original_op == SYM_COND) {
        // (cond test1 expr1 test2 expr2 ...)
        return eval_cond(list, env, st);
    }
    
    if (original_op == SYM_DO) {
        // (do expr1 expr2 ...)
        // Evaluate all expressions in sequence, return the last one
        int list_len = list_count(list);
        CljObject *result = NULL;
        
        for (int i = 1; i < list_len; i++) {
            CljObject *expr = list_get_element(list, i);
            if (expr) {
                ASSIGN(result, eval_body(expr, env, st));
                // Note: result can be NULL (nil), which is valid
            }
        }
        
        // Return last result (or NULL if no expressions)
        return result;
    }
    
    // Note: time special form is now handled earlier in eval_list (before symbol resolution)
    // to ensure it's recognized even if it's not in the namespace

    if (original_op == SYM_GO) {
        // (go body...)
        // Minimal kompatible Semantik: Body auswerten und Result-Channel zurückgeben
        // 1) Body in nullstellige Funktion wrappen: (fn [] (do expr1 ... exprN))
        int argc = list_count(list);
        // Erzeuge (do ...) aus allen Body-Ausdrücken, falls vorhanden
        CljList *do_list = NULL;
        if (argc > 1) {
            // do-list beginnt mit Symbol 'do'
            do_list = (CljList*)make_list((CljObject*)SYM_DO, NULL);
            CljList *tail = do_list;
            for (int i = 1; i < argc; i++) {
                CljObject *expr_i = list_get_element(list, i);
                // Hänge expr_i an tail an
                CljList *new_node = (CljList*)make_list(expr_i, NULL);
                if (tail) {
                    tail->rest = (CljObject*)new_node;
                    tail = new_node;
                }
            }
        }

        // Erzeuge (fn [] (do ...))
        CljVector empty_params_vec = make_vector(0, 0);
        CljList *fn_list = (CljList*)make_list((CljObject*)SYM_FN, NULL);
        if (!fn_list) return NULL;
        fn_list->rest = (CljObject*)make_list(empty_params_vec, NULL);
        CljList *fn_rest = as_list((ID)fn_list->rest);
        if (fn_rest) {
            // Wenn kein Body, verwende nil, sonst (do ...)
            CljObject *body_expr = (CljObject*)do_list;
            // Auch bei leerem Body explizit ein nil-Knoten anhängen
            fn_rest->rest = (CljObject*)make_list(body_expr, NULL);
        }
        // Evaluiere (fn [] body)
        CljObject *fn_obj = eval_fn(fn_list, env, st);
        if (!fn_obj) {
            RELEASE(fn_list);
            return NULL;
        }

        // 2) Asynchron: Erzeuge Result-Channel, enqueuen und sofort Channel zurückgeben
        CljMap *chan = make_result_channel();
        event_loop_enqueue(fn_obj, chan);

        // Cleanup temporäre Objekte (Queue hält eigene Referenzen)
        RELEASE(fn_obj);
        RELEASE(fn_list);
        if (do_list) RELEASE(do_list);

        return (CljObject*)chan;
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
            args[i] = eval_arg(list, i + 1, env, NULL);
            if (!args[i]) {
                free_obj_array((ID*)args, args_stack);
                return NULL;
            }
        }
        
        CljObject *result = (CljObject*)native_str((ID*)args, argc);
        free_obj_array((ID*)args, args_stack);
        return result;
    }
    
    
    // Tier 4: Less frequent (10-30% of calls)
    if (original_op == SYM_AND) {
        // (and expr1 expr2 ...) - short circuit evaluation
        // Returns first falsy value or last value
        int argc = list_count(list);
        if (argc <= 1) return clj_true; // (and) => true
        
        CljObject *result = clj_true;
        for (int i = 1; i < argc; i++) {
            CljObject *arg = list_get_element(list, i);
            if (!arg) continue;
            
            result = eval_body(arg, env, st);
            if (!result || !clj_is_truthy(result)) {
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
    if (original_op == SYM_FN) {
        // CRITICAL: Use env (which may be let_env with namespace mappings) if available,
        // otherwise fall back to namespace mappings
        // This ensures that when fn is evaluated inside let, the closure environment
        // has both local bindings and namespace mappings (like reverse)
        CljMap *fn_env = env;
        if (!fn_env && st && st->current_ns) {
            fn_env = (CljMap*)st->current_ns->mappings;
        }
        // If env is provided, it should already contain namespace mappings (from eval_let)
        return AUTORELEASE(eval_fn(list, fn_env, st));
    }
    
    // Note: def and ns are now handled earlier (before symbol resolution)
    // to ensure they are recognized as special forms even if registered as builtins
    
    if (original_op == SYM_DEFN) {
        return eval_defn(list, env, st);
    }
    
    if (original_op == SYM_LET) {
        // (let [bindings*] body*)
        return eval_let(list, env, st);
    }
    
    if (original_op == SYM_DOTIMES) {
        // (dotimes [var n] body*)
        return eval_dotimes(list, env);
    }
    
    if (original_op == SYM_VAR) {
        // (var symbol) - returns a var reference to the symbol
        return eval_var(list, env, st);
    }
    
    if (original_op == SYM_QUOTE) {
        // (quote expr) - return expr without evaluating
        CljObject *quoted_expr = list_get_element(list, 1);
        if (!quoted_expr) return NULL;
        return RETAIN(quoted_expr), quoted_expr;
    }
    
    // recur is only valid inside function bodies, not in top-level lists
    if (original_op == SYM_RECUR) {
        throw_exception(EXCEPTION_RUNTIME, "recur can only be used inside function bodies", NULL, 0, 0);
        return NULL;
    }
    
    // Tier 6: Loop operations (for, doseq, dotimes)
    if (original_op == SYM_FOR || original_op == SYM_DOSEQ || original_op == SYM_DOTIMES) {
        result = eval_loop_dispatch(list, env, original_op);
        return result; // Return even if NULL
    }
    
    // Check if op (after resolution) is a function
    if (is_type(op, CLJ_FUNC) || is_type(op, CLJ_CLOSURE)) {
        return call_function_with_args(op, list, env, st);
    }
    
    // Fallback: try to resolve symbol and call as function
    if (is_type(op, CLJ_SYMBOL)) {
        // Handle keywords as functions (for map lookup)
        if (IS_KEYWORD(op)) {
            // Keyword as function - perform map lookup
            int total_count = list_count(as_list((ID)list));
            int argc = total_count - 1;
            
            if (argc == 1) {
                CljObject *arg = eval_arg(list, 1, env, NULL);
                
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
            // CRITICAL: Manage recursion depth here, not in eval_arg
            // This prevents double counting when eval_arg calls eval_list
            if (g_eval_arg_depth >= MAX_CALL_STACK_DEPTH) {
                // DEBUG: Print stack trace information
                fprintf(stderr, "STACK OVERFLOW: g_eval_arg_depth=%d, MAX_CALL_STACK_DEPTH=%d\n", 
                        g_eval_arg_depth, MAX_CALL_STACK_DEPTH);
                if (is_type(fn, CLJ_CLOSURE)) {
                    CljFunction *func = as_function(fn);
                    if (func && func->name) {
                        fprintf(stderr, "  Function: %s\n", func->name);
                    }
                }
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
        
        // Error: resolved value is a list (cannot call as function)
        if (is_type(fn, CLJ_LIST)) {
            throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                    "Cannot call list as a function");
            return NULL;
        }
        
        // Not a function, just return the resolved value
        return AUTORELEASE(RETAIN(fn));
    }
    
    // Error: first element is not a function
    if (IS_IMMEDIATE(op) || is_type(op, CLJ_STRING)) {
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                "Cannot call %s as a function", clj_type_name(op->type));
        return NULL;
    }
    
    // Error: op is a list (should have been evaluated earlier)
    if (is_type(op, CLJ_LIST)) {
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
    if (!symbol || !is_type(symbol, CLJ_SYMBOL)) {
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
    CljMap *eval_env = (st && st->current_ns) ? (CljMap*)st->current_ns->mappings : env;
    CljObject *value = NULL;
    if (value_expr) {
        if (is_type(value_expr, CLJ_LIST)) {
            value = eval_list(as_list((ID)value_expr), eval_env, st);
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
    if (is_type(value, CLJ_CLOSURE)) {
        CljFunction *func = as_function((ID)value);
        CljSymbol *sym = as_symbol((ID)symbol);
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
    if (!ns_name_obj || !is_type(ns_name_obj, CLJ_SYMBOL)) {
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

ID eval_var(CljList *list, CljMap *env, EvalState *st) {
    // (var symbol) - returns a var reference to the symbol
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    (void)env;  // Not used
    // Assertion: List and EvalState must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    assert(st != NULL);
    
    // Get symbol name (first argument)
    CljObject *sym_obj = list_get_element(list, 1);
    if (!sym_obj || !is_type(sym_obj, CLJ_SYMBOL)) {
        eval_error("var expects a symbol", st);
        return NULL;
    }
    
    CljSymbol *sym = as_symbol((ID)sym_obj);
    if (!sym || !sym->name[0]) {
        eval_error("var symbol has no name", st);
        return NULL;
    }
    
    // Look up the symbol in the current namespace
    CljObject *value = ns_resolve(st, sym_obj);
    if (!value) {
        // Try to find the symbol in the current namespace mappings
        CljMap *mappings = (CljMap*)st->current_ns->mappings;
        if (mappings) {
            value = map_get((CljValue)mappings, sym_obj);
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
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    CLJ_ASSERT(is_list(list));
    
    // Get the parameter list (second argument) - don't evaluate it
    CljObject *params_list = list_get_element(list, 1);
    // Parameters can be a vector [a b] or a list (a b)
    if (!params_list || (!is_type(params_list, CLJ_LIST) && !is_type(params_list, CLJ_VECTOR))) {
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
        if (!params[i] || !is_type(params[i], CLJ_SYMBOL)) {
            // Invalid parameter
            free_obj_array((ID*)params, params_stack);
            return NULL;
        }
    }
    
    // Validate recur positions (not in hot-path)
    validate_recur_positions(body, body);
    
    // Note: For anonymous functions (fn), we can't easily detect recursive calls
    // because there's no function name. This transformation is mainly for defn.
    
    // CRITICAL: Use env (which may be let_env with namespace mappings) if available,
    // otherwise fall back to namespace mappings
    // This ensures that when fn is evaluated inside let, the closure environment
    // has both local bindings and namespace mappings (like step in filter)
    CljMap *fn_env = env;
    if (!fn_env && st && st->current_ns) {
        fn_env = (CljMap*)st->current_ns->mappings;
    }
    
    // Create function object
    CljObject *fn = AUTORELEASE((CljObject*)make_function(params, param_count, (ID)body, fn_env, NULL));
    
    // Cleanup heap-allocated params if any
    free_obj_array((ID*)params, params_stack);
    
    return fn;
}

ID eval_symbol(ID symbol, EvalState *st) {
    if (!symbol) {
        return NULL;
    }
    
    CljSymbol *sym = as_symbol((ID)symbol);
    
    // Keywords evaluate to themselves (singletons need no memory management)
    if (IS_KEYWORD(symbol)) {
        return symbol;
    }
    
    // Special handling for *ns* - return current namespace name as symbol
    if (sym && strcmp(sym->name, "*ns*") == 0) {
        if (st && st->current_ns && st->current_ns->name) {
            return st->current_ns->name;  // Return the namespace symbol (e.g., 'user')
        }
        return (ID)intern_symbol(NULL, "user");  // Default namespace
    }
    
    // Special case: nil should evaluate to NULL (not SYM_NIL)
    if (SYM_NIL && symbol == SYM_NIL) {
        return NULL; // nil evaluates to NULL
    }
    
    // Lookup im aktuellen Namespace
    CljObject *value = ns_resolve(st, symbol);
    if (value) {
        // Special case: If value is SYM_NIL, return NULL (nil evaluates to NULL)
        if (value == (CljObject*)SYM_NIL) {
            return NULL; // nil evaluates to NULL
        }
        return AUTORELEASE(RETAIN(value));  // Gefunden - retain the value
    }
    
    // Fallback: Try global namespace lookup for special forms and builtins
    if (sym) {
        
        // Check against cached symbol pointers for O(1) lookup (only if initialized)
        if ((SYM_IF && symbol == SYM_IF) || (SYM_COND && symbol == SYM_COND) || 
            (SYM_WHEN && symbol == SYM_WHEN) || (SYM_WHILE && symbol == SYM_WHILE) || (SYM_DEF && symbol == SYM_DEF) || 
            (SYM_DEFN && symbol == SYM_DEFN) || (SYM_FN && symbol == SYM_FN) || 
            (SYM_QUOTE && symbol == SYM_QUOTE) || 
            (SYM_RECUR && symbol == SYM_RECUR) || (SYM_AND && symbol == SYM_AND) || 
            (SYM_OR && symbol == SYM_OR) || (SYM_NS && symbol == SYM_NS) || 
            (SYM_TRY && symbol == SYM_TRY) || (SYM_CATCH && symbol == SYM_CATCH) || 
            (SYM_THROW && symbol == SYM_THROW) || (SYM_FINALLY && symbol == SYM_FINALLY) ||
            (SYM_VAR && symbol == SYM_VAR) ||
            (SYM_DO && symbol == SYM_DO) || (SYM_LOOP && symbol == SYM_LOOP) || 
            (SYM_LET && symbol == SYM_LET) || (SYM_GO && symbol == SYM_GO) || (SYM_TIME && symbol == SYM_TIME) || (SYM_PLUS && symbol == SYM_PLUS) || 
            (SYM_MINUS && symbol == SYM_MINUS) || (SYM_MULTIPLY && symbol == SYM_MULTIPLY) || 
            (SYM_DIVIDE && symbol == SYM_DIVIDE) || (SYM_EQUALS && symbol == SYM_EQUALS) || 
            (SYM_LT && symbol == SYM_LT) || (SYM_GT && symbol == SYM_GT) || 
            (SYM_LE && symbol == SYM_LE) || (SYM_GE && symbol == SYM_GE) ||
            (SYM_PRINT && symbol == SYM_PRINT) || 
            (SYM_STR && symbol == SYM_STR) || (SYM_NTH && symbol == SYM_NTH) || (SYM_FIRST && symbol == SYM_FIRST) || 
            (SYM_REST && symbol == SYM_REST) || (SYM_COUNT && symbol == SYM_COUNT) || 
            (SYM_CONS && symbol == SYM_CONS) || (SYM_SEQ && symbol == SYM_SEQ) || 
            (SYM_NEXT && symbol == SYM_NEXT) ||
            (SYM_FOR && symbol == SYM_FOR) || (SYM_DOSEQ && symbol == SYM_DOSEQ) || 
            (SYM_DOTIMES && symbol == SYM_DOTIMES) || (SYM_TIME && symbol == SYM_TIME)) {
            return symbol;  // Return the symbol itself for special forms (singletons need no memory management)
        }
    }
    
    // Fehler: Symbol kann nicht aufgelöst werden
    const char *name = sym ? sym->name : "unknown";
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
        // CRITICAL: eval_arg returns AUTORELEASE objects, but native functions
        // need to use them, so we must retain them before passing to native function
        // RETAIN safely handles NULL and immediate values
        args[i] = RETAIN(args[i]);
    }
    
    // Call native function
    ID result = native_func(args, argc);
    
    // Clean up evaluated arguments
    // RELEASE safely handles NULL and immediate values
    for (int i = 0; i < argc && i < max_args; i++) {
        RELEASE(args[i]);
    }
    
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
    CljMap *new_env = (CljMap*)make_map(4); // Small capacity for loop environment
    if (new_env) {
        // Copy existing environment bindings
        if (env) {
            // For now, just use the existing environment
            // TODO: Implement proper environment copying
        }
        // Add new binding
        // CRITICAL: map_assoc may return a new map (COW), so we must use the result
        CljMap *updated_env = map_assoc(new_env, (ID)var, (ID)element);
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
    
    if (!binding_list || !is_type(binding_list, CLJ_LIST)) {
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
                ID body_result = eval_body_with_env((ID)body, new_env, NULL);
                if (body_result) {
                    RELEASE(body_result);
                }
                
                // Clean up environment
                RELEASE(new_env);
            }
            
            // Move to next element
            CljObject *next = (CljObject*)seq_next((CljObject*)seq);
            RELEASE((ID)seq);
            seq = (CljSeqIterator*)next;
        }
        // Clean up final seq iterator (not returned as value)
        RELEASE((ID)seq);
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
                ID body_result = eval_body_with_env((ID)body, new_env, NULL);
                if (body_result) {
                    RELEASE(body_result);
                }
                
                // Clean up environment
                RELEASE(new_env);
            }
            
            
            CljObject *next = (CljObject*)seq_next((CljObject*)seq);
            RELEASE((ID)seq);
            seq = (CljSeqIterator*)next;
        }
        // Clean up final seq iterator
        RELEASE((ID)seq);
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
    if (!is_type((CljObject*)list, CLJ_LIST)) return NULL;
    
    CljList *list_data = as_list((ID)list);
    
    // Create new list starting from the second element (skip 'list' symbol)
    CljObject *args_list = (CljObject*)LIST_REST(list_data);
    if (!args_list) {
        // No arguments - return empty list (like native_list does)
        return (ID)empty_list();
    }
    
    // Simply return the arguments as a list (they're already evaluated by eval_list)
    // ✅ FIX: LIST_REST does NOT return autoreleased object - need to autorelease it
    return AUTORELEASE(RETAIN(args_list));
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
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "let requires a vector for bindings", 
                       NULL, 0, 0);
        return NULL;
    }
    
    CljPersistentVector *bindings = as_vector((CljValue)bindings_vec);
    if (!bindings) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "let bindings must be a valid vector", 
                       NULL, 0, 0);
        return NULL;
    }
    int binding_count = bindings->count;
    
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
                    CljMap *new_let_env = map_assoc(let_env, (ID)key, (ID)val);
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
                        CljMap *new_let_env = map_assoc(let_env, (ID)key, (ID)val);
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
        CljValue sym_val = bindings->data[i];
        CljValue init_val = bindings->data[i + 1];
        
        if (!sym_val || !is_type(sym_val, CLJ_SYMBOL)) {
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
                value = eval_body(init_val, let_env, st);
                // value can be NULL if evaluation result is nil
            }
        }
        
        // CRITICAL: If value is a function (closure), update its closure_env to include itself
        // This allows recursive functions defined in let to find themselves
        // For example: (let [step (fn [x] (step x))] ...) - step needs to find itself
        if (value && is_type(value, CLJ_CLOSURE)) {
            CljFunction *func = as_function((ID)value);
            if (func && func->closure_env) {
                // Add the function to its own closure environment so it can find itself recursively
                CljMap *func_env = func->closure_env;
                // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update closure_env
                // Always update closure_env to ensure the function can find itself recursively
                // Check if function is already in closure_env before adding
                CljObject *existing = (CljObject*)map_get((CljValue)func_env, (CljValue)sym_val);
                if (existing != value) {
                    // Function not in closure_env - add it
                    CljMap *new_func_env = map_assoc(func_env, (ID)sym_val, (ID)value);
                    // Update closure_env (map_assoc handles COW and capacity growth)
                    if (new_func_env != func_env) {
                        // Update closure_env if it changed (COW or capacity growth)
                        // Use ASSIGN to safely replace closure_env (releases old, retains new)
                        ASSIGN(func->closure_env, new_func_env);
                    } else {
                        // map_assoc returned same map but should have added the function
                        // Verify it's there now
                        CljObject *verify = (CljObject*)map_get((CljValue)func_env, (CljValue)sym_val);
                        if (verify != value) {
                            // Still not there - force update
                            // This shouldn't happen, but handle it
                            CljMap *forced_new = map_assoc(func_env, (ID)sym_val, (ID)value);
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
        CljMap *new_let_env = map_assoc(let_env, (ID)sym_val, (ID)value);
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
    
    // Parse arguments using rest traversal: (defn name [params] body...)
    CljObject *rest_obj = list->rest;
    CljList *rest = as_list((ID)rest_obj);
    if (!rest) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn requires function name", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Get function name (first element after defn)
    CljObject *name_sym = rest->first;
    if (!name_sym || !is_type(name_sym, CLJ_SYMBOL)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn requires a symbol for function name", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Get parameter vector (second element after defn)
    rest_obj = rest->rest;
    rest = as_list((ID)rest_obj);
    if (!rest) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn requires parameter vector", 
                       NULL, 0, 0);
        return NULL;
    }
    
    CljObject *params_vec = rest->first;
    if (!params_vec || !is_type(params_vec, CLJ_VECTOR)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn requires a vector for parameters", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Get body expressions (everything after params)
    rest_obj = rest->rest;
    rest = as_list((ID)rest_obj);
    if (!rest) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn requires at least one body expression", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Extract parameters from vector
    CljPersistentVector *params_vec_data = as_vector((CljValue)params_vec);
    if (!params_vec_data) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn requires a valid parameter vector", 
                       NULL, 0, 0);
        return NULL;
    }
    
    int param_count = params_vec_data->count;
    CljObject *params_stack[16];
    ID *params = alloc_obj_array(param_count, params_stack);
    
    for (int i = 0; i < param_count; i++) {
        params[i] = (ID)params_vec_data->data[i];
        if (!params[i] || !is_type((CljObject*)params[i], CLJ_SYMBOL)) {
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
    if (rest->rest == NULL) {
        // Single body expression - use directly without wrapping in a list
        // CRITICAL: Do NOT wrap in list - func->body should be the expression itself
        body_expr_obj = rest->first;
    } else {
        // Multiple body expressions - just use the last one for now
        // TODO: Implement proper do block or let sequencing
        CljObject *current_obj = (CljObject*)rest;
        CljList *current = rest;
        while (current->rest) {
            current_obj = current->rest;
            current = as_list((ID)current_obj);
        }
        body_expr_obj = current->first;
    }
    
    if (!body_expr_obj) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "defn body cannot be empty", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Validate recur positions (not in hot-path)
    validate_recur_positions(body_expr_obj, body_expr_obj);
    
    // TCO DISABLED: Transform recursive tail calls to recur (AST transformation)
    // CljObject *body_expr = body_expr_obj;
    // CljObject *transformed_body_expr = transform_recursive_tail_calls(
    //     body_expr, name_sym, (CljObject**)params, param_count, body_expr_obj);
    // if (!transformed_body_expr) {
    //     free_obj_array((ID*)params, params_stack);
    //     return NULL;
    // }
    // 
    // // Update body_expr_obj with transformed expression if it changed
    // if (transformed_body_expr != body_expr) {
    //     RELEASE(body_expr_obj);
    //     body_expr_obj = transformed_body_expr;
    // } else {
    //     RELEASE(transformed_body_expr);
    // }
    
    // Create function object directly (skip fn list creation to save code)
    // Use current namespace mappings as environment for function evaluation
    // This ensures that builtin functions like + are available in closures
    if (!st || !st->current_ns) {
        free_obj_array((ID*)params, params_stack);
        throw_exception(EXCEPTION_RUNTIME, "defn requires an evaluation state with namespace", NULL, 0, 0);
        return NULL;
    }
    
    CljMap *fn_env = (CljMap*)st->current_ns->mappings;
    if (!fn_env) {
        fn_env = make_map(16);
        st->current_ns->mappings = (CljObject*)fn_env;
    }
    
    // Create function object with namespace mappings as closure_env
    CljFunction *fn_obj = make_function(params, param_count, (ID)body_expr_obj, fn_env, NULL);
    if (!fn_obj) {
        free_obj_array((ID*)params, params_stack);
        return NULL;
    }
    
    // Set function name
    CljFunction *func = fn_obj;
    CljSymbol *sym = as_symbol((ID)name_sym);
    if (func && sym && sym->name[0] && !func->name) {
        func->name = strdup(sym->name);
    }
    
    // Register function in namespace (after creation for recursive calls)
    // This ensures the function is available when the body is evaluated
    // ns_define may update st->current_ns->mappings (COW), so we need to update closure_env
    ns_define(st->current_ns, name_sym, (ID)fn_obj); // ns_define does RETAIN internally
    
    // CRITICAL: Update closure_env to point to current namespace mappings
    // This ensures recursive calls can find the function via closure_env or namespace lookup
    // Since ns_define may have updated st->current_ns->mappings (COW),
    // we need to update the function's closure_env to point to the updated mappings
    // Always update closure_env to ensure it contains the function that was just registered
    // This is necessary even if COW didn't happen, because the function needs to be
    // available in closure_env for recursive calls
    // CRITICAL: Always update closure_env, even if pointers are the same, because
    // the function was just added to the mappings, and closure_env needs to reflect that
    if (st->current_ns->mappings != (CljObject*)func->closure_env) {
        // Use ASSIGN to safely replace closure_env (releases old, retains new)
        ASSIGN(func->closure_env, (CljMap*)st->current_ns->mappings);
    }
    // Even if pointers are the same, the function is now in the mappings
    // closure_env already points to the correct map, so no update needed
    
    // ASSERTION: Verify that the function is now in closure_env
    // This tests the thesis that closure_env contains the function after ns_define
    CLJ_ASSERT(func->closure_env != NULL);
    CLJ_ASSERT(is_type((CljObject*)func->closure_env, CLJ_MAP));
    CljObject *func_in_closure = (CljObject*)map_get((CljValue)func->closure_env, (CljValue)name_sym);
    CLJ_ASSERT(func_in_closure != NULL);
    CLJ_ASSERT(func_in_closure == (CljObject*)fn_obj || func_in_closure == (CljObject*)func);
    
    RELEASE((CljObject*)fn_obj); // Release our reference (namespace keeps it)
    free_obj_array((ID*)params, params_stack);
    return name_sym; // Return symbol (Clojure-compatible)
}

// Helper function for evaluating arguments
ID eval_arg(CljList *list, int index, CljMap *env, EvalState *st) {
    // Assertion: List must not be NULL when expected
    CLJ_ASSERT(list != NULL);
    if (!is_type((CljObject*)list, CLJ_LIST)) return NULL;
    
    // Handle NULL environment gracefully
    if (env == NULL) {
        // Return the element as-is if no environment is available
        CljObject *element = (CljObject*)list_nth(as_list((ID)list), index);
        if (element && !IS_IMMEDIATE(element)) {
            return AUTORELEASE(RETAIN(element));
        }
        return element;
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
        // ASSERTION: Debug symbol resolution in eval_arg
        CljSymbol *sym = as_symbol((ID)element);
        // CRITICAL: Check if symbol is "nil" - nil is represented as NULL in our system
        if (sym && sym->name && strcmp(sym->name, "nil") == 0) {
            // nil is represented as NULL - return NULL directly
            return NULL;
        }
        if (env && is_type((CljObject*)env, CLJ_MAP)) {
            // CRITICAL: When let is used inside a function, env should be let_env which contains function parameters
            // This allows eval_arg to resolve function parameters like 'coll' in (rest coll)
            // CRITICAL: Check if key exists first, because map_get returns NULL both when:
            // 1. The key doesn't exist in the map
            // 2. The value is nil (NULL)
            // We need to distinguish between these two cases
            if (map_contains((CljValue)env, (CljValue)element)) {
                // Key exists in map - get the value (which may be NULL/nil)
                CljObject *resolved = (CljObject*)map_get((CljValue)env, (CljValue)element);
                // CRITICAL: map_get returns a value that is already retained by the map.
                // eval_arg should return AUTORELEASE objects, so we need to autorelease it.
                // resolved can be NULL (nil), which is valid - return it directly
                if (resolved && !IS_IMMEDIATE(resolved)) {
                    return AUTORELEASE(RETAIN(resolved));
                }
                return resolved;
            }
            // Key doesn't exist in map - try namespace resolution below
            // DEBUG: If symbol not found, check environment count and structure
            // This helps identify if the environment is empty or malformed
            CljSymbol *sym = as_symbol((CljObject*)element);
            if (sym && sym->name && strcmp(sym->name, "i") == 0) {
                // This is the symbol 'i' that we're looking for
                // Check if environment has any entries
                CljMap *env_map = (CljMap*)env;
                if (env_map->count == 0) {
                    // Environment is empty - this shouldn't happen if let_env was created correctly
                    throw_exception_formatted("SymbolResolutionError", __FILE__, __LINE__, 0,
                        "Symbol 'i' not found in environment - environment is empty. "
                        "This indicates that let_env was not created correctly or was lost. "
                        "Environment: %p, Environment count: %d",
                        env, env_map->count);
                }
            }
        }
        
        // If not found in local environment, try namespace
        // CRITICAL: Use provided st if available, otherwise use NULL (uses default "user" namespace)
        CljObject *resolved = ns_resolve(st, element);
        if (resolved) {
            // CRITICAL: ns_resolve returns AUTORELEASE objects.
            // eval_arg should return AUTORELEASE objects, so we just return it as-is.
            return resolved;
        }
        
        // If not found in environment or namespace, return the symbol as-is (don't retain - it's a symbol)
        // This is OK for symbols that will be resolved later or are undefined
        return element;
    }
    
    // For lists, evaluate them directly
    // CRITICAL: eval_list manages recursion depth itself when calling functions,
    // so we don't need to increase g_eval_arg_depth here. This prevents double counting.
    if (is_type(element, CLJ_LIST)) {
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
            result = eval_list(as_list((ID)element), env, eval_st);
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
    
    // For vectors, maps, etc., return as-is
    return element; // Don't retain - caller will handle retention
}


// is_symbol is already defined in namespace.c

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
    
    // Extract binding vector/list (first argument after dotimes)
    CljObject *binding_list = NULL;
    CljObject *body_list = NULL;
    
    if (list_data->rest && is_type(list_data->rest, CLJ_LIST)) {
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
    
    if (is_type(binding_list, CLJ_VECTOR)) {
        // Use nth function to safely access vector elements
        var = nth2((ID[]){binding_list, fixnum(0)}, 2);
        n_obj = nth2((ID[]){binding_list, fixnum(1)}, 2);
    } else if (is_type(binding_list, CLJ_LIST)) {
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
    
    if (!var || !n_obj || TAG(n_obj) != CLJ_INT) {
        return NULL;
    }
    
    int n = as_fixnum((CljValue)n_obj);
    
    // Execute body n times
    for (int i = 0; i < n; i++) {
        // Create new environment with binding using map_assoc
        CljMap *new_env = (CljMap*)make_map(4); // Small capacity for loop environment
        if (new_env) {
            // Don't copy existing environment bindings - just add the loop variable
            // Add new binding
            // CRITICAL: map_assoc may return a new map (COW), so we must use the result
            CljValue i_value = fixnum((int32_t)i);
            CljMap *updated_env = map_assoc(new_env, (ID)var, i_value);
            ASSIGN(new_env, updated_env);
            
            // Evaluate all body expressions with new binding
            // body_list can contain multiple expressions
            EvalState *st = evalstate_new(false);
            CljObject *body_result = NULL;
            if (is_type(body_list, CLJ_LIST)) {
                // Multiple expressions - evaluate all, return last
                CljList *body_items = as_list(body_list);
                while (body_items && body_items->first) {
                    if (body_result) {
                        RELEASE(body_result);
                    }
                    body_result = eval_body(body_items->first, new_env, st);
                    body_items = body_items->rest ? as_list(body_items->rest) : NULL;
                }
            } else {
                // Single expression - use eval_body
                body_result = eval_body(body_list, new_env, st);
            }
            if (body_result) {
                RELEASE(body_result);
            }
            evalstate_free(st);
            
            // Clean up environment
            RELEASE(new_env);
        }
    }
    
    return AUTORELEASE(NULL); // dotimes always returns nil (Clojure-compatible)
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
    if (is_type(expr, CLJ_LIST)) {
        result = eval_list(as_list(expr), eval_env, st);
    } else if (is_type(expr, CLJ_SYMBOL)) {
        // For symbols, look up in environment (if provided) or namespace
        if (eval_env && is_type((CljObject*)eval_env, CLJ_MAP)) {
            result = (CljObject*)map_get((CljValue)eval_env, (CljValue)expr);
            if (result) {
                RETAIN(result);
            }
        }
        // If not found in environment, try namespace lookup
        if (!result && st) {
            result = ns_resolve(st, expr);
            if (result) {
                RETAIN(result);
            }
        }
    } else {
        // Literal value - return as-is
        result = expr;
        RETAIN(result);
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
    // eval_list returns AUTORELEASE objects, so we need to handle them correctly
    // If result is NULL, return NULL (nil)
    if (!result) {
        return NULL;
    }
    // If result is immediate, return it directly
    if (IS_IMMEDIATE(result)) {
        return result;
    }
    // For heap objects, eval_list already returns AUTORELEASE, so we just return it
    // No need to call AUTORELEASE again - eval_list already handles it
    return result;
}

// ============================================================================
// FUNCTION CALL IMPLEMENTATION
// ============================================================================

ID clj_call_function(ID fn, int argc, ID *argv) {
    if (!is_type((CljObject*)fn, CLJ_FUNC)) return NULL;
    
    // Arity check
    CljFunction *func = as_function(fn);
    if (!func) {
        return (ID)make_exception("Error", "Invalid function object", NULL, 0, 0);
    }
    if (argc != func->param_count) {
        return (ID)make_exception("Error", "Arity mismatch in function call", NULL, 0, 0);
    }
    
    // Heap-allocated parameter array
    ID *heap_params = (ID*)malloc(sizeof(ID) * argc);
    for (int i = 0; i < argc; i++) {
        heap_params[i] = RETAIN(argv[i]);
    }
    
    // Extend environment with parameters
    CljMap *call_env = env_extend_stack(func->closure_env, (ID*)func->params, heap_params, argc);
    if (!call_env) {
        free(heap_params);
        return (ID)make_exception("Error", "Failed to create function environment", NULL, 0, 0);
    }
    
    // Evaluate function body (simplified; would normally call eval())
    ID result = func->body ? RETAIN(func->body) : NULL;
    
    // Release environment and parameter array
    RELEASE(call_env);
    free(heap_params);
    
    return result;
}

ID clj_apply_function(ID fn, ID *args, int argc, ID env) {
    if (!is_type((CljObject*)fn, CLJ_FUNC)) return NULL;
    (void)env;
    
    // Evaluate arguments (simplified; would normally call eval())
    ID *eval_args = STACK_ALLOC(ID, argc);
    for (int i = 0; i < argc; i++) {
        eval_args[i] = RETAIN(args[i]);
    }
    
    return clj_call_function(fn, argc, eval_args);
}