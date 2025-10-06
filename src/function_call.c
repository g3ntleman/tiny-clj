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

#include "CljObject.h"
#include "vector.h"
#include "function_call.h"
#include "seq.h"
#include "namespace.h"
#include "memory_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


// Forward declarations
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count);
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_arg_with_substitution(CljObject *list, int index, CljObject **params, CljObject **values, int param_count);
CljObject* eval_add_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_sub_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_mul_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_div_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);

CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);

/** @brief Compare symbol name directly (works for non-interned symbols) */
static inline int sym_is(CljObject *value, const char *name) {
    CljSymbol *sym = as_symbol(value);
    return sym && strcmp(sym->name, name) == 0;
}

/** @brief Get raw nth element from a list (0=head). Returns NULL if out of bounds */
static CljObject* list_get_element(CljObject *list, int index) {
    if (!list || list->type != CLJ_LIST || index < 0) return NULL;
    CljList *node = as_list(list);
    if (!node) return NULL;
    if (index == 0) return node->head;
    int i = 0;
    while (node && i < index) {
        node = node->tail ? as_list(node->tail) : NULL;
        i++;
    }
    return (node && node->head) ? node->head : NULL;
}

// Arithmetic operation types
typedef enum {
    ARITH_ADD, ARITH_SUB, ARITH_MUL, ARITH_DIV
} ArithOp;

// Arithmetic operation functions
static int add_op(int a, int b) { return a + b; }
static int sub_op(int a, int b) { return a - b; }
static int mul_op(int a, int b) { return a * b; }
static int div_op(int a, int b) { return a / b; }

// Error messages
static const char* arith_errors[] = {
    "Invalid arguments for addition",
    "Invalid arguments for subtraction", 
    "Invalid arguments for multiplication",
    "Invalid arguments for division"
};

/** @brief Generic arithmetic function (normal version) */
CljObject* eval_arithmetic_generic(CljObject *list, CljObject *env, ArithOp op) {
    CljObject *a = eval_arg(list, 1, env);
    CljObject *b = eval_arg(list, 2, env);
    
    if (!a || !b || a->type != CLJ_INT || b->type != CLJ_INT) {
        throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                "Invalid arguments for %s: expected two integers, got %s and %s", 
                arith_errors[op], a ? clj_type_name(a->type) : "nil", b ? clj_type_name(b->type) : "nil");
        return clj_nil();
    }
    
    // Division by zero check
    if (op == ARITH_DIV && b->as.i == 0) {
        throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                "Division by zero: %d / %d", a->as.i, b->as.i);
        return clj_nil();
    }
    
    int (*op_func)(int, int) = (op == ARITH_ADD) ? add_op :
                              (op == ARITH_SUB) ? sub_op :
                              (op == ARITH_MUL) ? mul_op : div_op;
    
    int result = op_func(a->as.i, b->as.i);
    return make_int(result);
}

// Generic arithmetic function (with parameter substitution)
CljObject* eval_arithmetic_generic_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count, ArithOp op) {
    CljObject *a = eval_arg_with_substitution(list, 1, params, values, param_count);
    CljObject *b = eval_arg_with_substitution(list, 2, params, values, param_count);
    
    if (!a || !b || a->type != CLJ_INT || b->type != CLJ_INT) {
        throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                "Invalid arguments for %s: expected two integers, got %s and %s", 
                arith_errors[op], a ? clj_type_name(a->type) : "nil", b ? clj_type_name(b->type) : "nil");
        return clj_nil();
    }
    
    // Division by zero check
    if (op == ARITH_DIV && b->as.i == 0) {
        throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
                "Division by zero: %d / %d", a->as.i, b->as.i);
        return clj_nil();
    }
    
    int (*op_func)(int, int) = (op == ARITH_ADD) ? add_op :
                              (op == ARITH_SUB) ? sub_op :
                              (op == ARITH_MUL) ? mul_op : div_op;
    
    int result = op_func(a->as.i, b->as.i);
    return make_int(result);
}

// Extended function call implementation with complete evaluation
/** @brief Main function call evaluator */
CljObject* eval_function_call(CljObject *fn, CljObject **args, int argc, CljObject *env) {
    (void)env;
    if (!fn || fn->type != CLJ_FUNC) {
        throw_exception("TypeError", "Attempt to call non-function value", NULL, 0, 0);
        return clj_nil();
    }
    
    // Get function data
    CljFunction *func = as_function(fn);
    if (!func) {
        return make_error("Invalid function object", NULL, 0, 0);
    }
    
    // Arity check
    if (argc != func->param_count) {
        throw_exception("ArityError", "Arity mismatch in function call", NULL, 0, 0);
        return clj_nil();
    }
    
    // Simplified parameter binding
    // Replace parameter symbols with their values in the body
    CljObject *result = eval_body_with_params(func->body, func->params, args, argc);
    return result;
}


// Simplified body evaluation with parameter binding
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count) {
    if (!body) return clj_nil();
    
    // Simplified implementation - would normally evaluate the AST
    if (body->type == CLJ_LIST) {
        // Evaluate list - create temporary list with replaced symbols
        return eval_list_with_param_substitution(body, params, values, param_count);
    }
    
    if (body->type == CLJ_SYMBOL) {
        // Resolve symbol - check parameters
        for (int i = 0; i < param_count; i++) {
            if (params[i] && body == params[i]) {
                return values[i] ? (RETAIN(values[i]), values[i]) : clj_nil();
            }
        }
        // Fallback: return symbol itself
        return body ? (RETAIN(body), body) : clj_nil();
    }
    
    // Literal value
    return body ? (RETAIN(body), body) : clj_nil();
}

// Evaluate list with parameter substitution
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count) {
    if (!list || list->type != CLJ_LIST) return clj_nil();
    
    CljList *list_data = as_list(list);
    if (!list_data) return clj_nil();
    
    CljObject *head = list_data->head;
    if (!head) return clj_nil();
    
    // First element is the operator
    CljObject *op = head;
    
    // Simplified builtin operations
    if (sym_is(op, "if")) {
        // (if cond then else?)
        CljObject *cond_val = eval_arg_with_substitution(list, 1, params, values, param_count);
        bool truthy = clj_is_truthy(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return clj_nil();
        return eval_body_with_params(branch, params, values, param_count);
    }
    if (is_symbol(op, "+")) {
        return eval_add_with_substitution(list, params, values, param_count);
    }
    
    if (is_symbol(op, "-")) {
        return eval_sub_with_substitution(list, params, values, param_count);
    }
    
    if (is_symbol(op, "*")) {
        return eval_mul_with_substitution(list, params, values, param_count);
    }
    
    if (is_symbol(op, "/")) {
        return eval_div_with_substitution(list, params, values, param_count);
    }
    
    if (is_symbol(op, "println")) {
        return eval_println_with_substitution(list, params, values, param_count);
    }
    
    // Fallback: return first element
    return head ? (RETAIN(head), head) : clj_nil();
}

// Simplified body evaluation (placeholder for real eval implementation)
/** @brief Evaluate function body expressions */
CljObject* eval_body(CljObject *body, CljObject *env, EvalState *st) {
    if (!body) return clj_nil();
    
    // Simplified implementation - would normally evaluate the AST
    if (body->type == CLJ_LIST) {
        // Evaluate list
        return eval_list(body, env, st);
    }
    
    if (body->type == CLJ_SYMBOL) {
        // Resolve symbol
        return env_get_stack(env, body);
    }
    
    // Literal value
    return body ? (RETAIN(body), body) : clj_nil();
}

// Simplified list evaluation
CljObject* eval_list(CljObject *list, CljObject *env, EvalState *st) {
    if (!list || list->type != CLJ_LIST) return clj_nil();
    
    CljList *list_data = as_list(list);
    if (!list_data) return clj_nil();
    
    CljObject *head = list_data->head;
    if (!head) return clj_nil();
    
    // First element is the operator
    CljObject *op = head;
    
    // Simplified builtin operations
    if (sym_is(op, "if")) {
        // (if cond then else?)
        CljObject *cond_val = eval_arg(list, 1, env);
        bool truthy = clj_is_truthy(cond_val);
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return clj_nil();
        return eval_body(branch, env, st);
    }
    if (sym_is(op, "+")) {
        return eval_add(list, env);
    }
    
    if (sym_is(op, "-")) {
        return eval_sub(list, env);
    }
    
    if (sym_is(op, "*")) {
        return eval_mul(list, env);
    }
    
    if (sym_is(op, "/")) {
        return eval_div(list, env);
    }
    
    if (sym_is(op, "println")) {
        return eval_println(list, env);
    }
    
    if (sym_is(op, "def")) {
        return eval_def(list, env, st);
    }
    
    if (sym_is(op, "fn")) {
        return eval_fn(list, env);
    }
    
    if (sym_is(op, "str")) {
        return eval_str(list, env);
    }
    
    if (sym_is(op, "prn")) {
        return eval_prn(list, env);
    }
    
    if (sym_is(op, "count")) {
        return eval_count(list, env);
    }
    
    if (sym_is(op, "first")) {
        return eval_first(list, env);
    }
    
    if (sym_is(op, "rest")) {
        return eval_rest(list, env);
    }
    
    if (sym_is(op, "seq")) {
        return eval_seq(list, env);
    }
    
    if (sym_is(op, "next")) {
        return eval_rest(list, env); // next is alias for rest
    }
    
    if (sym_is(op, "for")) {
        CljObject *result = eval_for(list, env);
        return result ? AUTORELEASE(result) : NULL;
    }
    
    if (sym_is(op, "doseq")) {
        CljObject *result = eval_doseq(list, env);
        return result ? AUTORELEASE(result) : NULL;
    }
    
    if (sym_is(op, "dotimes")) {
        CljObject *result = eval_dotimes(list, env);
        return result ? AUTORELEASE(result) : NULL;
    }
    
    if (sym_is(op, "list")) {
        CljObject *result = eval_list_function(list, env);
        return result ? AUTORELEASE(result) : NULL;
    }
    
    // Fallback: return first element
    return head ? (RETAIN(head), head) : clj_nil();
}

// Small wrapper functions for arithmetic operations
CljObject* eval_add(CljObject *list, CljObject *env) {
    return eval_arithmetic_generic(list, env, ARITH_ADD);
}

CljObject* eval_sub(CljObject *list, CljObject *env) {
    return eval_arithmetic_generic(list, env, ARITH_SUB);
}

CljObject* eval_mul(CljObject *list, CljObject *env) {
    return eval_arithmetic_generic(list, env, ARITH_MUL);
}

CljObject* eval_div(CljObject *list, CljObject *env) {
    return eval_arithmetic_generic(list, env, ARITH_DIV);
}

CljObject* eval_add_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_ADD);
}

CljObject* eval_sub_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_SUB);
}

CljObject* eval_mul_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_MUL);
}

CljObject* eval_div_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count) {
    return eval_arithmetic_generic_with_substitution(list, params, values, param_count, ARITH_DIV);
}

CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count) {
    CljObject *arg = eval_arg_with_substitution(list, 1, params, values, param_count);
    if (arg) {
        char *str = pr_str(arg);
        printf("println: %s\n", str);
        free(str);
    }
    return clj_nil();
}


CljObject* eval_println(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (arg) {
        char *str = pr_str(arg);
        printf("println: %s\n", str);
        free(str);
    }
    return clj_nil();
}

CljObject* eval_def(CljObject *list, CljObject *env, EvalState *st) {
    if (!is_list(list)) return clj_nil();
    
    // Get the symbol name (second argument) - don't evaluate it, just get the symbol
    CljObject *symbol = list_get_element(list, 1);
    if (!symbol || symbol->type != CLJ_SYMBOL) {
        return clj_nil();
    }
    
    // Get the value (third argument) - evaluate this
    CljObject *value_expr = list_get_element(list, 2);
    if (!value_expr) {
        return clj_nil();
    }
    
    // Evaluate the value expression
    CljObject *value = NULL;
    if (value_expr->type == CLJ_LIST) {
        value = eval_list(value_expr, env, st);
    } else {
        value = eval_expr_simple(value_expr, st);
    }
    if (!value) {
        return clj_nil();
    }
    
    // Store the symbol-value binding in the environment
    if (st) {
        // Store in namespace
        ns_define(st, symbol, value);
    }
    
    return AUTORELEASE(value);
}

CljObject* eval_fn(CljObject *list, CljObject *env) {
    if (!is_list(list)) return clj_nil();
    
    // Get the parameter list (second argument)
    CljObject *params = eval_arg(list, 1, env);
    if (!params || params->type != CLJ_LIST) {
        return clj_nil();
    }
    
    // Get the body (third argument)
    CljObject *body = eval_arg(list, 2, env);
    if (!body) {
        return clj_nil();
    }
    
    // TODO: Create a function object with parameters and body
    // For now, just return the body
    return body;
}

CljObject* eval_symbol(CljObject *symbol, EvalState *st) {
    if (!symbol || symbol->type != CLJ_SYMBOL) {
        return clj_nil();
    }
    
    
    // Lookup im aktuellen Namespace
    CljObject *value = ns_resolve(st, symbol);
    if (value) {
        return value;  // Gefunden
    }
    
    // Fehler: Undefinierte Variable
    CljSymbol *sym = as_symbol(symbol);
    const char *name = sym ? sym->name : "unknown";
    throw_exception_formatted(NULL, __FILE__, __LINE__, 0, "Undefined variable: %s", name);
    return NULL;
}

CljObject* eval_str(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return AUTORELEASE(make_string(""));
    
    char *str = pr_str(arg);
    CljObject *result = AUTORELEASE(make_string(str));
    free(str);
    return result;
}

CljObject* eval_prn(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (arg) {
        char *str = pr_str(arg);
        printf("%s\n", str);
        free(str);
    }
    return clj_nil();
}

CljObject* eval_count(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return AUTORELEASE(make_int(0));
    
    if (arg->type == CLJ_NIL) {
        return AUTORELEASE(make_int(0)); // nil has count 0
    }
    
    if (arg->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(arg);
        return vec ? AUTORELEASE(make_int(vec->count)) : AUTORELEASE(make_int(0));
    }
    
    if (arg->type == CLJ_LIST) {
        CljList *list_data = as_list(arg);
        int count = 0;
        CljObject *current = list_data ? list_data->head : NULL;
        while (current) {
            count++;
            CljList *current_list = as_list(current);
            current = current_list ? current_list->tail : NULL;
        }
        return AUTORELEASE(make_int(count));
    }
    
    if (arg->type == CLJ_MAP) {
        CljMap *map = as_map(arg);
        return map ? AUTORELEASE(make_int(map->count)) : AUTORELEASE(make_int(0));
    }
    
    if (arg->type == CLJ_STRING) {
        return AUTORELEASE(make_int(strlen((char*)arg->as.data)));
    }
    
    return AUTORELEASE(make_int(1)); // Single value (int, bool, symbol, etc.)
}

CljObject* eval_first(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return clj_nil();
    
    // Use new seq implementation for unified behavior
    CljObject *seq = seq_create(arg);
    if (!seq) return clj_nil();
    
    CljObject *result = seq_first(seq);
    seq_release(seq);
    
    return result ? result : clj_nil();
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
CljObject* eval_rest(CljObject *list, CljObject *env) {
    CljObject *result = NULL;  // Single result variable for clean return pattern
    CljObject *arg = eval_arg(list, 1, env);  // Get the sequence argument
    
    // Handle NULL argument case - return empty list
    if (!arg) {
        result = make_list();  // Create empty list for nil/empty case
    } else {
        // Use new seq implementation for unified behavior across all sequence types
        CljObject *seq = seq_create(arg);  // Create sequence iterator
        if (!seq) {
            // Seq creation failed - return empty list as fallback
            result = make_list();
        } else {
            // Get the rest of the sequence (everything except first element)
            CljObject *rest_seq = seq_rest(seq);
            seq_release(seq);  // Clean up the original sequence iterator
            
            if (!rest_seq) {
                // No rest elements - return empty list
                result = make_list();
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

CljObject* eval_seq(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return clj_nil();
    
    // If argument is already nil, return nil
    if (arg->type == CLJ_NIL) {
        return clj_nil();
    }
    
    // Check if argument is seqable
    if (!is_seqable(arg)) {
        return clj_nil();
    }
    
    // For lists, return as-is (lists are already sequences)
    if (arg->type == CLJ_LIST) {
        return arg ? (RETAIN(arg), arg) : clj_nil();
    }
    
    // For other seqable types, return SeqIterator directly
    CljObject *seq = seq_create(arg);
    if (!seq) return clj_nil();
    
    return (CljObject*)seq;
}

// ============================================================================
// FOR-LOOP IMPLEMENTATIONS
// ============================================================================

CljObject* eval_for(CljObject *list, CljObject *env) {
    // (for [binding coll] expr)
    // Returns a lazy sequence of results
    
    CljObject *binding_list = eval_arg(list, 1, env);
    CljObject *body = eval_arg(list, 2, env);
    
    if (!binding_list || binding_list->type != CLJ_LIST) {
        return clj_nil();
    }
    
    // Parse binding: [var coll]
    CljList *binding_data = as_list(binding_list);
    if (!binding_data || !binding_data->head || !binding_data->tail) {
        return clj_nil();
    }
    
    CljObject *var = binding_data->head;
    CljObject *coll = binding_data->tail;
    
    // Get collection to iterate over
    CljList *coll_data = as_list(coll);
    if (!coll_data || !coll_data->head) {
        return clj_nil();
    }
    
    CljObject *collection = coll_data->head; // Simple: just use the expression directly
    if (!collection) {
        return clj_nil();
    }
    
    // Create result list
    CljObject *result = make_list();
    
    // Iterate over collection using seq
    CljObject *seq = seq_create(collection);
    if (seq) {
        while (!seq_empty(seq)) {
            CljObject *element = seq_first(seq);
            
            // Create new environment with binding
            CljObject *new_env = make_list();
            CljList *new_env_data = as_list(new_env);
            if (new_env_data) {
                // Add binding to environment
                new_env_data->head = var;
                CljObject *tail_list = make_list();
                new_env_data->tail = tail_list;
                CljList *tail_data = as_list(tail_list);
                if (tail_data) {
                    tail_data->head = element;
                    tail_data->tail = env; // Chain with existing environment
                }
                // Clean up environment objects (not returned as values)
                RELEASE(new_env);
                RELEASE(tail_list);
            }
            
            // Evaluate body with new binding
            CljObject *body_result = body; // Simple: just return the expression
            
            // Add result to result list (simple implementation)
            if (body_result) {
                // For now, just return the first result
                // In a full implementation, this would be a lazy sequence
                RELEASE(result);
                RELEASE(collection);
                seq_release(seq);
                return AUTORELEASE(body_result);
            }
            
            CljObject *next = seq_next(seq);
            seq_release(seq);
            seq = next;
        }
        // Clean up final seq iterator (not returned as value)
        seq_release(seq);
    }
    
    RELEASE(collection);
    return AUTORELEASE(result);
}

CljObject* eval_doseq(CljObject *list, CljObject *env) {
    // (doseq [binding coll] expr)
    // Executes expr for side effects, returns nil
    
    CljObject *binding_list = list_get_element(list, 1);
    CljObject *body = list_get_element(list, 2);
    
    if (!binding_list || binding_list->type != CLJ_LIST || !body) {
        return clj_nil();
    }
    
    // Parse binding: [var coll]
    CljList *binding_data = as_list(binding_list);
    if (!binding_data || !binding_data->head || !binding_data->tail) {
        return clj_nil();
    }
    
    CljObject *var = binding_data->head;
    CljObject *coll = binding_data->tail;
    
    // Get collection to iterate over
    CljList *coll_data = as_list(coll);
    if (!coll_data || !coll_data->head) {
        return clj_nil();
    }
    
    CljObject *collection = coll_data->head; // Simple: just use the expression directly
    if (!collection) {
        return clj_nil();
    }
    
    // Iterate over collection using seq
    CljObject *seq = seq_create(collection);
    if (seq) {
        while (!seq_empty(seq)) {
            CljObject *element = seq_first(seq);
            
            // Create new environment with binding
            CljObject *new_env = make_list();
            CljList *new_env_data = as_list(new_env);
            if (new_env_data) {
                // Add binding to environment
                new_env_data->head = var;
                new_env_data->tail = make_list();
                CljList *tail_data = as_list(new_env_data->tail);
                if (tail_data) {
                    tail_data->head = element;
                    tail_data->tail = env; // Chain with existing environment
                }
            }
            
            // Evaluate body for side effects
            RELEASE(body); // Discard result
            
            // Clean up environment objects
            RELEASE(new_env_data->tail);  // Freigabe der tail-Liste
            RELEASE(new_env);             // Freigabe der new_env-Liste
            
            CljObject *next = seq_next(seq);
            seq_release(seq);
            seq = next;
        }
        // Clean up final seq iterator
        seq_release(seq);
    }
    
    // Clean up allocated objects (only collection is owned)
    RELEASE(collection);
    return AUTORELEASE(clj_nil()); // doseq always returns nil
}

CljObject* eval_list_function(CljObject *list, CljObject *env) {
    (void)env; // Suppress unused parameter warning
    // (list arg1 arg2 ...) - creates a list from the arguments
    if (!list || list->type != CLJ_LIST) return clj_nil();
    
    CljList *list_data = as_list(list);
    if (!list_data) return clj_nil();
    
    // Create new list starting from the second element (skip 'list' symbol)
    CljObject *args_list = list_data->tail;
    if (!args_list) return clj_nil();
    
    // Simply return the arguments as a list (they're already evaluated by eval_list)
    RETAIN(args_list);
    return args_list;
}

CljObject* eval_dotimes(CljObject *list, CljObject *env) {
    // (dotimes [var n] expr)
    // Executes expr n times with var bound to 0, 1, ..., n-1
    
    CljObject *binding_list = eval_arg(list, 1, env);
    CljObject *body = eval_arg(list, 2, env);
    
    if (!binding_list || binding_list->type != CLJ_LIST) {
        return clj_nil();
    }
    
    // Parse binding: [var n]
    CljList *binding_data = as_list(binding_list);
    if (!binding_data || !binding_data->head || !binding_data->tail) {
        return clj_nil();
    }
    
    CljObject *var = binding_data->head;
    CljObject *n_expr = binding_data->tail;
    
    // Get number of iterations
    CljList *n_data = as_list(n_expr);
    if (!n_data || !n_data->head) {
        return clj_nil();
    }
    
    CljObject *n_obj = n_data->head; // Simple: just use the expression directly
    if (!n_obj || n_obj->type != CLJ_INT) {
        if (n_obj) RELEASE(n_obj);
        return clj_nil();
    }
    
    int n = n_obj->as.i;
    RELEASE(n_obj);
    
    // Execute body n times
    for (int i = 0; i < n; i++) {
        // Create new environment with binding
        CljObject *new_env = make_list();
        CljList *new_env_data = as_list(new_env);
        if (new_env_data) {
            // Add binding to environment
            new_env_data->head = var;
            CljObject *tail_list = make_list();
            new_env_data->tail = tail_list;
            CljList *tail_data = as_list(tail_list);
            if (tail_data) {
                CljObject *int_obj = make_int(i);
                tail_data->head = int_obj;
                tail_data->tail = env; // Chain with existing environment
                // Clean up objects (not returned as values)
                RELEASE(int_obj);
            }
            // Clean up environment objects (not returned as values)
            RELEASE(new_env);
            RELEASE(tail_list);
        }
        
        // Evaluate body
        CljObject *body_result = body; // Simple: just return the expression
        if (body_result) {
            RELEASE(body_result); // Discard result
        }
    }
    
    return AUTORELEASE(clj_nil()); // dotimes always returns nil
}

// Helper function for evaluating arguments
CljObject* eval_arg(CljObject *list, int index, CljObject *env) {
    if (!list || list->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list(list);
    if (!list_data) return NULL;
    
    // Collect all elements in an array
    CljObject *elements[1000]; // Max 1000 elements
    int count = 0;
    
    // Add head
    if (list_data->head) {
        elements[count++] = list_data->head;
    }
    
    // Add tail elements
    CljObject *current = list_data->tail;
    while (current && count < 1000) {
        CljList *current_list = as_list(current);
        if (current_list && current_list->head) {
            elements[count++] = current_list->head;
        }
        current = current_list ? current_list->tail : NULL;
    }
    
    // Check index and evaluate element
    if (index < 0 || index >= count) return NULL;
    
    // Simple evaluation - just return the element directly for now
    return elements[index] ? (RETAIN(elements[index]), elements[index]) : clj_nil();
}

// Evaluate argument with parameter substitution
CljObject* eval_arg_with_substitution(CljObject *list, int index, CljObject **params, CljObject **values, int param_count) {
    if (!list || list->type != CLJ_LIST) return NULL;
    
    CljList *list_data = as_list(list);
    if (!list_data) return NULL;
    
    // Collect all elements in an array
    CljObject *elements[1000]; // Max 1000 elements
    int count = 0;
    
    // Add head
    if (list_data->head) {
        elements[count++] = list_data->head;
    }
    
    // Add tail elements
    CljObject *current = list_data->tail;
    while (current && count < 1000) {
        CljList *current_list = as_list(current);
        if (current_list && current_list->head) {
            elements[count++] = current_list->head;
        }
        current = current_list ? current_list->tail : NULL;
    }
    
    // Check index and evaluate element
    if (index < 0 || index >= count) return NULL;
    
    return eval_body_with_params(elements[index], params, values, param_count);
}

// is_symbol is already defined in namespace.c