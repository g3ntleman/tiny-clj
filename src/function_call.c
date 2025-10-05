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
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
CljObject* eval_body_with_params(CljObject *body, CljObject **params, CljObject **values, int param_count);
CljObject* eval_list_with_param_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_arg_with_substitution(CljObject *list, int index, CljObject **params, CljObject **values, int param_count);
CljObject* eval_add_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_sub_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_mul_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_div_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);
CljObject* eval_println_with_substitution(CljObject *list, CljObject **params, CljObject **values, int param_count);

// Helper: compare symbol name directly (works for non-interned symbols)
static inline int sym_is(CljObject *value, const char *name) {
    CljSymbol *sym = as_symbol(value);
    return sym && strcmp(sym->name, name) == 0;
}

// Helper: get raw nth element from a list (0=head). Returns NULL if out of bounds
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

// Generic arithmetic function (normal version)
CljObject* eval_arithmetic_generic(CljObject *list, CljObject *env, ArithOp op) {
    CljObject *a = eval_arg(list, 1, env);
    CljObject *b = eval_arg(list, 2, env);
    
    if (!a || !b || a->type != CLJ_INT || b->type != CLJ_INT) {
        throw_exception("ArithmeticException", arith_errors[op], NULL, 0, 0);
        return clj_nil();
    }
    
    // Division by zero check
    if (op == ARITH_DIV && b->as.i == 0) {
        throw_exception("ArithmeticException", "Division by zero", NULL, 0, 0);
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
        throw_exception("ArithmeticException", arith_errors[op], NULL, 0, 0);
        return clj_nil();
    }
    
    // Division by zero check
    if (op == ARITH_DIV && b->as.i == 0) {
        throw_exception("ArithmeticException", "Division by zero", NULL, 0, 0);
        return clj_nil();
    }
    
    int (*op_func)(int, int) = (op == ARITH_ADD) ? add_op :
                              (op == ARITH_SUB) ? sub_op :
                              (op == ARITH_MUL) ? mul_op : div_op;
    
    int result = op_func(a->as.i, b->as.i);
    return make_int(result);
}

// Extended function call implementation with complete evaluation
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
                return values[i] ? (retain(values[i]), values[i]) : clj_nil();
            }
        }
        // Fallback: return symbol itself
        return body ? (retain(body), body) : clj_nil();
    }
    
    // Literal value
    return body ? (retain(body), body) : clj_nil();
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
        int truthy = 0;
        if (cond_val && cond_val != clj_nil()) {
            if (cond_val->type == CLJ_BOOL) truthy = cond_val->as.b != 0; else truthy = 1;
        }
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
    return head ? (retain(head), head) : clj_nil();
}

// Simplified body evaluation (placeholder for real eval implementation)
CljObject* eval_body(CljObject *body, CljObject *env) {
    if (!body) return clj_nil();
    
    // Simplified implementation - would normally evaluate the AST
    if (body->type == CLJ_LIST) {
        // Evaluate list
        return eval_list(body, env);
    }
    
    if (body->type == CLJ_SYMBOL) {
        // Resolve symbol
        return env_get_stack(env, body);
    }
    
    // Literal value
    return body ? (retain(body), body) : clj_nil();
}

// Simplified list evaluation
CljObject* eval_list(CljObject *list, CljObject *env) {
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
        int truthy = 0;
        if (cond_val && cond_val != clj_nil()) {
            if (cond_val->type == CLJ_BOOL) truthy = cond_val->as.b != 0; else truthy = 1;
        }
        CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
        if (!branch) return clj_nil();
        return eval_body(branch, env);
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
    
    // Fallback: return first element
    return head ? (retain(head), head) : clj_nil();
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

CljObject* eval_str(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return make_string("");
    
    char *str = pr_str(arg);
    CljObject *result = make_string(str);
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
    if (!arg) return make_int(0);
    
    if (arg->type == CLJ_NIL) {
        return make_int(0); // nil has count 0
    }
    
    if (arg->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(arg);
        return vec ? make_int(vec->count) : make_int(0);
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
        return make_int(count);
    }
    
    if (arg->type == CLJ_MAP) {
        CljMap *map = as_map(arg);
        return map ? make_int(map->count) : make_int(0);
    }
    
    if (arg->type == CLJ_STRING) {
        return make_int(strlen((char*)arg->as.data));
    }
    
    return make_int(1); // Single value (int, bool, symbol, etc.)
}

CljObject* eval_first(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return clj_nil();
    
    if (arg->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(arg);
        if (vec && vec->count > 0) {
            return vec->data[0] ? (retain(vec->data[0]), vec->data[0]) : clj_nil();
        }
    } else if (arg->type == CLJ_LIST) {
        CljList *list_data = as_list(arg);
        if (list_data && list_data->head) {
            return list_data->head ? (retain(list_data->head), list_data->head) : clj_nil();
        }
    }
    
    return clj_nil();
}

CljObject* eval_rest(CljObject *list, CljObject *env) {
    CljObject *arg = eval_arg(list, 1, env);
    if (!arg) return make_list(); // Empty list
    
    if (arg->type == CLJ_VECTOR) {
        CljPersistentVector *vec = as_vector(arg);
        if (vec && vec->count > 1) {
            CljObject *rest_vec = make_vector(vec->count - 1, 0);
            CljPersistentVector *rest_data = as_vector(rest_vec);
            if (rest_data) {
                for (int i = 1; i < vec->count; i++) {
                    rest_data->data[i-1] = vec->data[i] ? (retain(vec->data[i]), vec->data[i]) : NULL;
                }
                rest_data->count = vec->count - 1;
            }
            return rest_vec;
        }
    } else if (arg->type == CLJ_LIST) {
        CljList *list_data = as_list(arg);
        if (list_data && list_data->tail) {
            return list_data->tail ? (retain(list_data->tail), list_data->tail) : make_list();
        }
    }
    
    return make_list(); // Empty list
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
    
    return eval_body(elements[index], env);
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