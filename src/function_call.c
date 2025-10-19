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

#include "object.h"
#include "function_call.h"
#include <string.h>
#include <assert.h>
#include "clj_string.h"
#include "seq.h"
#include "namespace.h"
#include "memory.h"
#include "list_operations.h"
#include "builtins.h"
#include "map.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>


// Forward declarations  
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_arg_with_substitution(CljObject *list, int index, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_add_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_sub_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_mul_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);
CljObject* eval_div_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);

CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env);

// Forward declarations for loop evaluation
CljObject* eval_body_with_env(CljObject *body, CljMap *env);
CljObject* eval_list_with_env(CljObject *list, CljMap *env);

/** @brief Compare symbol name directly (works for non-interned symbols) */
static inline int sym_is(CljObject *value, const char *name) {
    if (!value || value->type != CLJ_SYMBOL) return 0;
    CljSymbol *sym = as_symbol(value);
    return sym && strcmp(sym->name, name) == 0;
}

/** @brief Allocate array with stack optimization (size <= 16 on stack, else heap) */
static inline CljObject** alloc_obj_array(int size, CljObject **stack_buffer) {
    return size <= 16 ? stack_buffer : (CljObject**)malloc(sizeof(CljObject*) * size);
}

/** @brief Free array allocated with alloc_obj_array */
static inline void free_obj_array(CljObject **array, CljObject **stack_buffer) {
    if (array != stack_buffer) free(array);
}

/** @brief Get raw nth element from a list (0=head). Returns NULL if out of bounds */
static CljObject* list_get_element(CljObject *list, int index) {
    if (!list || list->type != CLJ_LIST || index < 0) return NULL;
    CljList *node = as_list(list);
    if (index == 0) return LIST_FIRST(node);
    int i = 0;
    while (i < index) {
        CljObject *rest = LIST_REST(node);
        if (!rest || !is_type(rest, CLJ_LIST)) return NULL;
        node = as_list(rest);
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
    return is_fixnum((CljValue)obj) || is_float16((CljValue)obj);
}

/** @brief Generic arithmetic function (variadic version) */
CljObject* eval_arithmetic_generic(CljObject *list, CljMap *env, ArithOp op, EvalState *st) {
    (void)st; // Suppress unused parameter warning
    int total_count = list_count(list);
    int argc = total_count - 1;  // Subtract 1 for the operator
    
    if (argc == 0) {
        // Handle zero arguments case
        switch (op) {
            case ARITH_ADD:
                return (CljObject*)make_fixnum(0);  // (+) → 0
            case ARITH_MUL:
                return (CljObject*)make_fixnum(1);  // (*) → 1
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
        if (is_type(args[i], CLJ_NIL)) {
            // Clean up already evaluated arguments
            for (int j = 0; j <= i; j++) {
                RELEASE(args[j]);
            }
            free(args);
            throw_exception_formatted("NumberFormatException", __FILE__, __LINE__, 0,
                "Cannot convert nil to Number");
            return NULL;
        }
        
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
            result = native_add_variadic(args, argc);
            break;
        case ARITH_SUB:
            result = native_sub_variadic(args, argc);
            break;
        case ARITH_MUL:
            result = native_mul_variadic(args, argc);
            break;
        case ARITH_DIV:
            result = native_div_variadic(args, argc);
            break;
    }
    
    // Clean up arguments
    for (int i = 0; i < argc; i++) {
        RELEASE(args[i]);
    }
    free(args);
    
    return result;
}

// Generic arithmetic function (with parameter substitution)
CljObject* eval_arithmetic_generic_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, ArithOp op, CljObject *closure_env) {
    CljObject *a = eval_arg_with_substitution(list, 1, params, values, param_count, closure_env);
    CljObject *b = eval_arg_with_substitution(list, 2, params, values, param_count, closure_env);
    
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
        if (is_float16((CljValue)b) && as_float16((CljValue)b) == 0.0) {
            throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                    "Division by zero: %f / %f", as_float16((CljValue)a), as_float16((CljValue)b));
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
    
    int result;
    switch (op) {
        case ARITH_ADD:
            result = as_fixnum((CljValue)a) + as_fixnum((CljValue)b);
            break;
        case ARITH_SUB:
            result = as_fixnum((CljValue)a) - as_fixnum((CljValue)b);
            break;
        case ARITH_MUL:
            result = as_fixnum((CljValue)a) * as_fixnum((CljValue)b);
            break;
        case ARITH_DIV:
            result = as_fixnum((CljValue)a) / as_fixnum((CljValue)b);
            break;
        default:
            return NULL;
    }
    
    return (CljObject*)make_fixnum(result);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
CljObject* eval_function_call(CljObject *fn, CljObject **args, int argc, CljMap *env) {
    (void)env;
    if (!is_type(fn, CLJ_FUNC) && !is_type(fn, CLJ_CLOSURE)) {
        throw_exception("TypeError", "Attempt to call non-function value", NULL, 0, 0);
        return NULL;
    }
    
    // Check if it's a native function (CljFunc) or Clojure function (CljFunction)
    if (is_native_fn(fn)) {
        // It's a native function (CljFunc)
        CljFunc *native_func = (CljFunc*)fn;
        return native_func->fn(args, argc);
    }
    
    // It's a Clojure function (CljFunction)
    CljFunction *func = (CljFunction*)fn;
    if (!func) {
        return make_error("Invalid function object", NULL, 0, 0);
    }
    
    // Arity check
    if (argc != func->param_count) {
        throw_exception("ArityError", "Arity mismatch in function call", NULL, 0, 0);
        return NULL;
    }
    
    // Clojure functions with parameters are now supported
    
    // Simplified parameter binding
    // Replace parameter symbols with their values in the body
    CljObject *result = eval_body_with_params(func->body, func->params, args, argc, func->closure_env);
    
    return result;
}


// Evaluate body with environment lookup (for loops)
CljObject* eval_body_with_env(CljObject *body, CljMap *env) {
    if (!body) return NULL;
    
    if (is_type(body, CLJ_SYMBOL)) {
        // Look up symbol in environment
        return env_get_stack((CljObject*)env, body);
    }
    
    // For lists, evaluate them with environment
    if (is_type(body, CLJ_LIST)) {
        return eval_list_with_env(body, env);
    }
    
    // Literal value
    return body ? (RETAIN(body), body) : NULL;
}

// Evaluate list with environment (for loops)
CljObject* eval_list_with_env(CljObject *list, CljMap *env) {
    if (!list || list->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list(list);
    
    CljObject *head = list_data->first;
    
    // First element is the operator
    CljObject *op = head;
    
    // For symbols, look up in environment
    if (is_type(op, CLJ_SYMBOL)) {
        CljObject *resolved = env_get_stack((CljObject*)env, op);
        if (resolved) {
            // If it's a function, call it
            if (is_type(resolved, CLJ_FUNC)) {
                // Count arguments
                int total_count = list_count(list);
                int argc = total_count - 1;
                if (argc < 0) argc = 0;
                
                // Evaluate arguments
                CljObject *args_stack[16];
                CljObject **args = alloc_obj_array(argc, args_stack);
                
                for (int i = 0; i < argc; i++) {
                    args[i] = eval_body_with_env(list_get_element(list, i + 1), env);
                    if (!args[i]) args[i] = NULL;
                }
                
                // Call the function
                CljObject *result = eval_function_call(resolved, args, argc, env);
                
                free_obj_array(args, args_stack);
                
                return result;
            }
            // Otherwise, return the resolved value
            return resolved ? (RETAIN(resolved), resolved) : NULL;
        }
    }
    
    // Fallback: return first element
    return head ? (RETAIN(head), head) : NULL;
}

// Simplified body evaluation with parameter binding
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    if (!body) return NULL;
    
    if (is_type(body, CLJ_SYMBOL)) {
        // Resolve symbol - check parameters first
        for (int i = 0; i < param_count; i++) {
            if (params[i] && body == params[i]) {
                return values[i] ? (RETAIN(values[i]), values[i]) : NULL;
            }
            // Also try name comparison for non-interned symbols
            if (params[i] && is_type(params[i], CLJ_SYMBOL) && is_type(body, CLJ_SYMBOL)) {
                CljSymbol *param_sym = as_symbol(params[i]);
                CljSymbol *body_sym = as_symbol(body);
                if (param_sym && body_sym && strcmp(param_sym->name, body_sym->name) == 0) {
                    return values[i] ? (RETAIN(values[i]), values[i]) : NULL;
                }
            }
        }
        // If not a parameter, try to resolve from closure_env (namespace map)
        if (closure_env && is_type(closure_env, CLJ_MAP)) {
            CljObject *resolved = map_get(closure_env, body);
            if (resolved) {
                return resolved ? (RETAIN(resolved), resolved) : NULL;
            }
        }
        // If not found in parameters or closure_env, try to resolve from global namespace
        // This is a fallback for global symbols
        // For now, just return the symbol itself to avoid complex resolution
        return body ? (RETAIN(body), body) : NULL;
    }
    
    // For lists, evaluate them with parameter substitution
    if (is_type(body, CLJ_LIST)) {
        return eval_list_with_param_substitution(body, params, values, param_count, closure_env);
    }
    
    // Literal value
    return body ? (RETAIN(body), body) : NULL;
}

// Evaluate list with parameter substitution
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    if (!list || list->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list(list);
    
    CljObject *head = list_data->first;
    
    // First element is the operator
    CljObject *op = head;
    
    // Simplified builtin operations
    if (sym_is(op, "if")) {
        // (if cond then else?)
        CljObject *cond_val = eval_arg_with_substitution(list, 1, params, values, param_count, closure_env);
        bool truthy = clj_is_truthy(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return NULL;
        return eval_body_with_params(branch, params, values, param_count, closure_env);
    }
    if (sym_is(op, "+")) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_ADD, closure_env);
    }
    
    if (sym_is(op, "-")) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_SUB, closure_env);
    }
    
    if (sym_is(op, "*")) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_MUL, closure_env);
    }
    
    if (sym_is(op, "/")) {
        return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_DIV, closure_env);
    }
    
    if (sym_is(op, "println")) {
        return eval_println(list, NULL);  // Simplified - no parameter substitution for now
    }
    
    // Handle maps as functions (for key lookup)
    if (is_type(op, CLJ_MAP)) {
        int total_count = list_count(list);
        int argc = total_count - 1;
        
        if (argc != 1) {
            throw_exception_formatted("ArityException", __FILE__, __LINE__, 0,
                "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
            return NULL;
        }
        
        CljObject *key = eval_arg_with_substitution(list, 1, params, values, param_count, closure_env);
        if (!key) return NULL;
        
        CljObject *result = map_get(op, key);
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
            CljSymbol *param_sym = as_symbol(params[i]);
            CljSymbol *op_sym = as_symbol(op);
            if (param_sym && op_sym && strcmp(param_sym->name, op_sym->name) == 0) {
                resolved_op = values[i];
                break;
            }
        }
    }
    
    // If not found in parameters, try closure_env
    if (!resolved_op && is_type(op, CLJ_SYMBOL) && is_type(closure_env, CLJ_MAP)) {
        resolved_op = map_get(closure_env, op);
    }
    
    // If op was resolved to a function, call it
    if (resolved_op && is_type(resolved_op, CLJ_FUNC)) {
        // Count arguments
        int total_count = list_count(list);
        int argc = total_count - 1;
        if (argc < 0) argc = 0;
        
        // Evaluate arguments with substitution
        CljObject *args_stack[16];
        CljObject **args = alloc_obj_array(argc, args_stack);
        
        for (int i = 0; i < argc; i++) {
            args[i] = eval_arg_with_substitution(list, i + 1, params, values, param_count, closure_env);
            if (!args[i]) args[i] = NULL;
        }
        
        // Call the function
        CljObject *result = eval_function_call(resolved_op, args, argc, NULL);
        
        free_obj_array(args, args_stack);
        
        return result;
    }
    
    // Fallback: return first element
    return head ? (RETAIN(head), head) : NULL;
}

// Simplified body evaluation (basic implementation)
/** @brief Evaluate function body expressions */
CljObject* eval_body(CljObject *body, CljMap *env, EvalState *st) {
    if (!body) return NULL;
    
    // Simplified implementation - would normally evaluate the AST
    if (is_type(body, CLJ_LIST)) {
        // Evaluate list
        return eval_list(body, env, st);
    }
    
    if (is_type(body, CLJ_SYMBOL)) {
        // Resolve symbol
        return env_get_stack((CljObject*)env, body);
    }
    
    // Literal value
    return body ? (RETAIN(body), body) : NULL;
}

// Simplified list evaluation
CljObject* eval_list(CljObject *list, CljMap *env, EvalState *st) {
    if (!list || list->type != CLJ_LIST) {
        return NULL;
    }

    CljList *list_data = as_list(list);
    if (!list_data) {
        return NULL;
    }
    
    CljObject *head = LIST_FIRST(list_data);
    
    // First element is the operator
    CljObject *op = head;
    
    // If first element is a list, evaluate it first (for nested calls like ((array-map)))
    if (is_type(op, CLJ_LIST)) {
        op = eval_list(op, env, st);
        if (!op) return NULL;
        // Now op is the result of evaluating the inner list - continue with it
    }
    
    // Handle maps as functions (for key lookup) - must be first
    if (is_type(op, CLJ_MAP)) {
        int total_count = list_count(list);
        int argc = total_count - 1;
        
        // Maps require exactly 1 argument (the key)
        if (argc != 1) {
            throw_exception_formatted("ArityException", __FILE__, __LINE__, 0,
                "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
            return NULL;
        }
        
        // Get the key argument
        CljObject *key = eval_arg_retained(list, 1, env);
        if (!key) {
            return NULL;
        }
        
        // Perform map lookup
        CljObject *result = map_get(op, key);
        RELEASE(key);
        
        return result ? RETAIN(result) : NULL;
    }
    
    // Special forms that need special handling
    if (sym_is(op, "if")) {
        // (if cond then else?)
        CljObject *cond_val = eval_arg_retained(list, 1, env);
        bool truthy = clj_is_truthy(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return NULL;
        return eval_body(branch, env, st);
    }
    
    if (sym_is(op, "and")) {
        // (and expr1 expr2 ...) - short circuit evaluation
        // Returns first falsy value or last value
        int argc = list_count(list);
        if (argc <= 1) return (CljObject*)make_special(SPECIAL_TRUE); // (and) => true
        
        CljObject *result = (CljObject*)make_special(SPECIAL_TRUE);
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
    
    if (sym_is(op, "or")) {
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
    
    if (sym_is(op, "def")) {
        return eval_def(list, env, st);
    }
    
    if (sym_is(op, "ns")) {
        return eval_ns(list, env, st);
    }
    
    if (sym_is(op, "fn")) {
        return AUTORELEASE(eval_fn(list, env));
    }
    
    if (sym_is(op, "quote")) {
        // (quote expr) - return expr without evaluating
        CljObject *quoted_expr = list_get_element(list, 1);
        if (!quoted_expr) return NULL;
        return RETAIN(quoted_expr), quoted_expr;
    }
    
    // Arithmetic operations
    if (sym_is(op, "+")) {
        return eval_arithmetic_generic(list, env, ARITH_ADD, st);
    }
    
    if (sym_is(op, "-")) {
        return eval_arithmetic_generic(list, env, ARITH_SUB, st);
    }
    
    if (sym_is(op, "*")) {
        return eval_arithmetic_generic(list, env, ARITH_MUL, st);
    }
    
    if (sym_is(op, "/")) {
        return eval_arithmetic_generic(list, env, ARITH_DIV, st);
    }
    
    if (sym_is(op, "str")) {
        // Call native_str with evaluated arguments
        int total_count = list_count(list);
        int argc = total_count - 1;
        if (argc < 0) argc = 0;
        
        CljObject *args_stack[16];
        CljObject **args = alloc_obj_array(argc, args_stack);
        for (int i = 0; i < argc; i++) {
            args[i] = eval_arg_retained(list, i + 1, env);
            if (!args[i]) args[i] = NULL;
        }
        
        CljObject *result = native_str(args, argc);
        free_obj_array(args, args_stack);
        return result;
    }
    
    if (sym_is(op, "count")) {
        return eval_count(list, env);
    }
    
    if (sym_is(op, "println")) {
        return eval_println(list, env);
    }
    
    if (sym_is(op, "first")) {
        return eval_first(list, env);
    }
    
    if (sym_is(op, "rest")) {
        return eval_rest(list, env);
    }
    
    if (sym_is(op, "cons")) {
        return eval_cons(list, env);
    }
    
    if (sym_is(op, "seq")) {
        return eval_seq(list, env);
    }
    
    if (sym_is(op, "next")) {
        return eval_rest(list, env); // next is alias for rest
    }
    
    if (sym_is(op, "for")) {
        CljObject *result = eval_for(list, env);
        return AUTORELEASE(result);
    }
    
    if (sym_is(op, "doseq")) {
        CljObject *result = eval_doseq(list, env);
        return AUTORELEASE(result);
    }
    
    if (sym_is(op, "dotimes")) {
        CljObject *result = eval_dotimes(list, env);
        return AUTORELEASE(result);
    }
    
    if (sym_is(op, "list")) {
        CljObject *result = eval_list_function(list, env);
        return AUTORELEASE(result);
    }
    
    // Fallback: try to resolve symbol and call as function
    if (is_type(op, CLJ_SYMBOL)) {
        // Handle keywords as functions (for map lookup)
        CljSymbol *sym = as_symbol(op);
        if (sym->name[0] == ':') {
            // Keyword as function - perform map lookup
            int total_count = list_count(list);
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
                    CljObject *result = map_get(arg, op);
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
            int total_count = list_count(list);
            int argc = total_count - 1;
            
            // Maps require exactly 1 argument (the key)
            if (argc != 1) {
                throw_exception_formatted("ArityException", __FILE__, __LINE__, 0,
                    "Wrong number of args (%d) passed to: clojure.lang.PersistentArrayMap", argc);
                return NULL;
            }
            
            // Get the key argument
            CljObject *key = eval_arg_retained(list, 1, env);
            if (!key) {
                return NULL;
            }
            
            // Perform map lookup
            CljObject *result = map_get(fn, key);
            RELEASE(key);
            
            return result ? RETAIN(result) : NULL;
        }
        
        // Check if it's a function
        if (is_type(fn, CLJ_FUNC)) {
            // Count arguments
            int total_count = list_count(list);
            int argc = total_count - 1; // -1 for the function symbol itself
            if (argc < 0) argc = 0;
            
            // Stack allocate arguments array
            CljObject *args_stack[16];
            CljObject **args = alloc_obj_array(argc, args_stack);
            
            // Evaluate arguments
            for (int i = 0; i < argc; i++) {
                args[i] = eval_arg_retained(list, i + 1, env);
                if (!args[i]) args[i] = NULL;
            }
            
            // Call the function
            CljObject *result = eval_function_call(fn, args, argc, env);
            
            // Cleanup heap-allocated args if any
            free_obj_array(args, args_stack);
            
            return result;
        }
        
        // Not a function, just return the resolved value
        return fn ? (RETAIN(fn), fn) : NULL;
    }
    
    // Error: first element is not a function
    if (is_fixnum((CljValue)op) || is_float16((CljValue)op) || is_type(op, CLJ_STRING) || is_special((CljValue)op)) {
        throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
                "Cannot call %s as a function", clj_type_name(op->type));
        return NULL;
    }
    
    // Fallback: return first element (for other types)
    return head ? (RETAIN(head), head) : NULL;
}

// Small wrapper functions for arithmetic operations
CljObject* eval_add(CljObject *list, CljMap *env) {
    return eval_arithmetic_generic(list, env, ARITH_ADD, NULL);
}

CljObject* eval_sub(CljObject *list, CljMap *env) {
    return eval_arithmetic_generic(list, env, ARITH_SUB, NULL);
}

CljObject* eval_mul(CljObject *list, CljMap *env) {
    return eval_arithmetic_generic(list, env, ARITH_MUL, NULL);
}

CljObject* eval_div(CljObject *list, CljMap *env) {
    return eval_arithmetic_generic(list, env, ARITH_DIV, NULL);
}

CljObject* eval_equal(CljObject *list, CljMap *env) {
    CljObject *a = eval_arg_retained(list, 1, env);
    CljObject *b = eval_arg_retained(list, 2, env);
    
    if (!a || !b) return NULL;
    
    bool equal = clj_equal(a, b);
    return equal ? (CljObject*)make_special(SPECIAL_TRUE) : (CljObject*)make_special(SPECIAL_FALSE);
}

CljObject* eval_add_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_ADD, closure_env);
}

CljObject* eval_sub_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_SUB, closure_env);
}

CljObject* eval_mul_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_MUL, closure_env);
}

CljObject* eval_div_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_DIV, closure_env);
}

CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    CljObject *arg = eval_arg_with_substitution(list, 1, params, values, param_count, closure_env);
    if (arg) {
        char *str = pr_str(arg);
        printf("println: %s\n", str);
        free(str);
    }
    return NULL;
}


CljObject* eval_println(CljObject *list, CljMap *env) {
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (arg) {
        char *str = pr_str(arg);
        printf("println: %s\n", str);
        free(str);
        RELEASE(arg); // Release after use
    }
    return NULL;
}

CljObject* eval_def(CljObject *list, CljMap *env, EvalState *st) {
    if (!is_list(list)) return NULL;
    
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
        value = eval_list(value_expr, env, st);
    } else {
        value = eval_expr_simple(value_expr, st);
    }
    if (!value) {
        return NULL;
    }
    
    // If the value is a function, set its name
    if (is_type(value, CLJ_FUNC)) {
        CljFunction *func = as_function(value);
        CljSymbol *sym = as_symbol(symbol);
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

CljObject* eval_ns(CljObject *list, CljMap *env, EvalState *st) {
    (void)env;  // Not used
    if (!list || !st) return NULL;
    
    // Get namespace name (first argument) - use list_get_element like eval_def
    CljObject *ns_name_obj = list_get_element(list, 1);
    if (!ns_name_obj || ns_name_obj->type != CLJ_SYMBOL) {
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
    
    return NULL;
}

CljObject* eval_fn(CljObject *list, CljMap *env) {
    if (!is_list(list)) return NULL;
    
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
        CljPersistentVector *vec = as_vector(params_list);
        param_count = vec ? vec->count : 0;
    } else {
        param_count = list_count(params_list);
    }
    
    CljObject *params_stack[16];
    CljObject **params = alloc_obj_array(param_count, params_stack);
    
    for (int i = 0; i < param_count; i++) {
        if (is_type(params_list, CLJ_VECTOR)) {
            CljPersistentVector *vec = as_vector(params_list);
            params[i] = vec->data[i];
        } else {
            params[i] = list_get_element(params_list, i);
        }
        if (!params[i] || params[i]->type != CLJ_SYMBOL) {
            // Invalid parameter
            free_obj_array(params, params_stack);
            return NULL;
        }
    }
    
    // Create function object
    CljObject *fn = make_function(params, param_count, body, (CljObject*)env, NULL);
    
    // Cleanup heap-allocated params if any
    free_obj_array(params, params_stack);
    
    return fn ? (RETAIN(fn), fn) : NULL;
}

CljObject* eval_symbol(CljObject *symbol, EvalState *st) {
    if (!symbol || symbol->type != CLJ_SYMBOL) {
        return NULL;
    }
    
    CljSymbol *sym = as_symbol(symbol);
    
    // Keywords evaluate to themselves
    if (sym && sym->name[0] == ':') {
        return RETAIN(symbol);
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
        return value;  // Gefunden
    }
    
    // Fehler: Symbol kann nicht aufgelöst werden
    const char *name = sym ? sym->name : "unknown";
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Unable to resolve symbol: %s in this context", name);
    return NULL;
}

CljObject* eval_str(CljObject *list, CljMap *env) {
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (!arg) return AUTORELEASE(make_string(""));
    
    char *str = pr_str(arg);
    CljObject *result = AUTORELEASE(make_string(str));
    free(str);
    return result;
}

CljObject* eval_prn(CljObject *list, CljMap *env) {
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (arg) {
        char *str = pr_str(arg);
        printf("%s\n", str);
        free(str);
    }
    return NULL;
}

CljObject* eval_count(CljObject *list, CljMap *env) {
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (!arg) return AUTORELEASE((CljObject*)make_fixnum(0));
    
    if (is_type(arg, CLJ_NIL)) {
        return AUTORELEASE((CljObject*)make_fixnum(0)); // nil has count 0
    }
    
    if (is_type(arg, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector(arg);
        return AUTORELEASE(vec ? (CljObject*)make_fixnum(vec->count) : (CljObject*)make_fixnum(0));
    }
    
    if (is_type(arg, CLJ_LIST)) {
        CljList *list_data = as_list(arg);
        int count = 0;
        CljObject *current = list_data->first;
        while (current) {
            count++;
            if (is_type(current, CLJ_LIST)) {
                CljList *current_list = as_list(current);
                current = current_list ? current_list->rest : NULL;
            } else {
                current = NULL;
            }
        }
        return AUTORELEASE((CljObject*)make_fixnum(count));
    }
    
    if (is_type(arg, CLJ_MAP)) {
        CljMap *map = as_map(arg);
        return AUTORELEASE(map ? (CljObject*)make_fixnum(map->count) : (CljObject*)make_fixnum(0));
    }
    
    if (is_type(arg, CLJ_STRING)) {
        // String data is stored directly after CljObject header
        char **str_ptr = (char**)((char*)arg + sizeof(CljObject));
        return AUTORELEASE((CljObject*)make_fixnum(strlen(*str_ptr)));
    }
    
    return AUTORELEASE((CljObject*)make_fixnum(1)); // Single value (int, bool, symbol, etc.)
}

CljObject* eval_first(CljObject *list, CljMap *env) {
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (!arg) return NULL;
    
    // Use new seq implementation for unified behavior
    CljObject *seq = seq_create(arg);
    if (!seq) return NULL;
    
    CljObject *result = seq_first(seq);
    seq_release(seq);
    
    return result ? result : NULL;
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
CljObject* eval_rest(CljObject *list, CljMap *env) {
    CljObject *result = NULL;  // Single result variable for clean return pattern
    CljObject *arg = eval_arg_retained(list, 1, env);  // Get the sequence argument
    
    // Handle NULL argument case - return empty list
    if (!arg) {
        result = (CljObject*)make_list(NULL, NULL);  // Create empty list for nil/empty case
    } else {
        // Use new seq implementation for unified behavior across all sequence types
        CljObject *seq = seq_create(arg);  // Create sequence iterator
        if (!seq) {
            // Seq creation failed - return empty list as fallback
            result = (CljObject*)make_list(NULL, NULL);
        } else {
            // Get the rest of the sequence (everything except first element)
            CljObject *rest_seq = seq_rest(seq);
            seq_release(seq);  // Clean up the original sequence iterator
            
            if (!rest_seq) {
                // No rest elements - return empty list
                result = (CljObject*)make_list(NULL, NULL);
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

CljObject* eval_cons(CljObject *list, CljMap *env) {
    CljObject *elem = eval_arg_retained(list, 1, env);
    CljObject *coll = eval_arg_retained(list, 2, env);
    
    if (!elem) elem = NULL;
    
    CljObject *result = NULL;
    
    // Fall 1: nil oder leer
    if (!coll || is_type(coll, CLJ_NIL)) {
        result = (CljObject*)make_list(elem, NULL);
        RELEASE(elem);
        return AUTORELEASE(result);
    }
    
    // Fall 2: Bereits CLJ_LIST oder CLJ_SEQ → direkt als rest verwenden
    if (is_type(coll, CLJ_LIST) || is_type(coll, CLJ_SEQ)) {
        result = (CljObject*)make_list(elem, coll);
        RELEASE(elem);   // Balance: make_list macht RETAIN
        RELEASE(coll);   // Balance: make_list macht RETAIN
        return AUTORELEASE(result);
    }
    
    // Fall 3: Vektor oder andere → zu Seq konvertieren
    CljObject *seq = seq_create(coll);  // Heap-Objekt 1
    RELEASE(coll);  // Balance aus eval_arg_retained
    
    if (!seq || is_type(seq, CLJ_NIL)) {
        // Leere Seq → nur Element
        result = (CljObject*)make_list(elem, NULL);
    } else {
        // Seq als rest → make_list macht RETAIN auf seq
        result = (CljObject*)make_list(elem, seq);  // Heap-Objekt 2
        RELEASE(seq);  // Balance: make_list macht RETAIN
    }
    
    RELEASE(elem);
    return AUTORELEASE(result);
}

CljObject* eval_seq(CljObject *list, CljMap *env) {
    CljObject *arg = eval_arg_retained(list, 1, env);
    if (!arg) return NULL;
    
    // If argument is already nil, return nil
    if (is_type(arg, CLJ_NIL)) {
        return NULL;
    }
    
    // Check if argument is seqable
    if (!is_seqable(arg)) {
        return NULL;
    }
    
    // For lists, return as-is (lists are already sequences)
    if (is_type(arg, CLJ_LIST)) {
        return arg ? (RETAIN(arg), arg) : NULL;
    }
    
    // For other seqable types, return SeqIterator directly
    CljObject *seq = seq_create(arg);
    if (!seq) return NULL;
    
    return (CljObject*)seq;
}

// ============================================================================
// FOR-LOOP IMPLEMENTATIONS
// ============================================================================

CljObject* eval_for(CljObject *list, CljMap *env) {
    // (for [binding coll] expr)
    // Returns a lazy sequence of results
    
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
    CljList *result = make_list(NULL, NULL);
    
    // Iterate over collection using seq
    CljObject *seq = seq_create(collection);
    if (seq) {
        while (!seq_empty(seq)) {
            CljObject *element = seq_first(seq);
            
            // Create new environment with binding using map_assoc
            CljMap *new_env = make_map(4); // Small capacity for loop environment
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
            CljObject *next = seq_next(seq);
            seq_release(seq);
            seq = next;
        }
        // Clean up final seq iterator (not returned as value)
        seq_release(seq);
    }
    
    RELEASE(collection);
    return AUTORELEASE((CljObject*)result);
}

CljObject* eval_doseq(CljObject *list, CljMap *env) {
    // (doseq [binding coll] expr)
    // Executes expr for side effects, returns nil
    
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
            CljObject *element = seq_first(seq);
            
            // Create new environment with binding using map_assoc
            CljMap *new_env = make_map(4); // Small capacity for loop environment
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
            
            
            CljObject *next = seq_next(seq);
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

CljObject* eval_list_function(CljObject *list, CljMap *env) {
    (void)env; // Suppress unused parameter warning
    // (list arg1 arg2 ...) - creates a list from the arguments
    if (!list || list->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list(list);
    
    // Create new list starting from the second element (skip 'list' symbol)
    CljObject *args_list = (CljObject*)LIST_REST(list_data);
    if (!args_list) return NULL;
    
    // Simply return the arguments as a list (they're already evaluated by eval_list)
    RETAIN(args_list);
    return args_list;
}

CljObject* eval_dotimes(CljObject *list, CljMap *env) {
    // (dotimes [var n] expr)
    // Executes expr n times with var bound to 0, 1, ..., n-1
    
    // Parse arguments directly without evaluation
    CljList *list_data = as_list(list);
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
        CljMap *new_env = make_map(4); // Small capacity for loop environment
        if (new_env) {
            // Copy existing environment bindings
            if (env) {
                // For now, just use the existing environment
                // TODO: Implement proper environment copying
            }
            // Add new binding
            map_assoc((CljObject*)new_env, var, (CljObject*)make_fixnum(i));
            
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

// Helper function for evaluating arguments with automatic retention
CljObject* eval_arg_retained(CljObject *list, int index, CljMap *env) {
    CljObject *result = eval_arg(list, index, env);
    if (result) RETAIN(result);
    return result;
}

// Thread-local recursion depth tracking for eval_arg
static _Thread_local int g_eval_arg_depth = 0;

// Helper function for evaluating arguments
CljObject* eval_arg(CljObject *list, int index, CljMap *env) {
    if (!list || list->type != CLJ_LIST) return NULL;
    
    // Handle NULL environment gracefully
    if (env == NULL) {
        // Return the element as-is if no environment is available
        CljObject *element = list_nth(list, index);
        return element ? RETAIN(element) : NULL;
    }
    
    // Use the existing list_nth function which is safer
    CljObject *element = list_nth(list, index);
    if (!element) return NULL;
    
    // For simple types (numbers, strings, booleans), return as-is
    if (is_fixnum((CljValue)element) || is_float16((CljValue)element) || 
        is_type(element, CLJ_STRING) || is_special((CljValue)element) || is_type(element, CLJ_NIL)) {
        return element; // Don't retain - caller will handle retention
    }
    
    // For symbols, resolve them from environment
    if (is_type(element, CLJ_SYMBOL)) {
        if (env) {
            CljObject *resolved = env_get_stack((CljObject*)env, element);
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
        CljObject *result = eval_list(element, env, st);
        g_eval_arg_depth--;
        
        return result;
    }
    
    // For vectors, maps, etc., return as-is
    return element; // Don't retain - caller will handle retention
}

// Evaluate argument with parameter substitution
CljObject* eval_arg_with_substitution(CljObject *list, int index, CljObject **params, CljObject **values, int param_count, CljObject *closure_env) {
    if (!list || list->type != CLJ_LIST) return NULL;
    
    // Use the existing list_nth function which is safer
    CljObject *element = list_nth(list, index);
    if (!element) return NULL;
    
    return eval_body_with_params(element, params, values, param_count, closure_env);
}

// is_symbol is already defined in namespace.c