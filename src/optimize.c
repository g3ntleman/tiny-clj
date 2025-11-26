/*
 * Tail Call Optimization (TCO) for Tiny-CLJ
 *
 * Compact, DRY implementation for detecting and transforming recursive tail calls
 * into explicit `recur` calls, following the Clojure approach.
 */

#include "optimize.h"
#include "common.h"
#include "object.h"
#include "symbol.h"
#include "exception.h"
#include "list.h"
#include "memory.h"
#include <string.h>
#include <stdio.h>

// Helper: Check if two symbols are equal
static bool symbols_equal(CljSymbol *sym1, CljSymbol *sym2) {
    if (!sym1 || !sym2) return false;
    if (sym1 == sym2) return true;
    CLJ_ASSERT(sym1->cname && sym2->cname);
    return strcmp(sym1->cname, sym2->cname) == 0;
}

// Helper: Find last element in list
static CljList* list_last(CljList *list) {
    while (list && list->rest) {
        list = as_list(list->rest);
    }
    return list;
}

// Helper: Check if expr is last in list
static bool is_last_in_list(CljObject *expr, CljList *list) {
    CljList *last = list_last(list);
    return last && last->first == expr;
}

// Check if an expression is in tail position within a body
bool is_tail_position(CljObject *expr, CljObject *body) {
    if (!expr || !body || TAG(body) != CLJ_LIST) return false;

    CljList *body_list = as_list(body);

    // Check if expr is last element
    if (is_last_in_list(expr, body_list)) return true;

    // Check special forms
    CljObject *head_obj = body_list->first;
    CljSymbol *head = (CljSymbol*)head_obj;
    CljList *rest = as_list(body_list->rest);
    if (!rest) return false;

    if (head == SYM_IF) {
        // (if cond then else) - both branches are tail
        CljList *then_list = as_list(rest->rest);
        if (then_list) {
            if (then_list->first == expr) return true;
            if (is_tail_position(expr, then_list->first)) return true;
            CljList *else_list = as_list(then_list->rest);
            if (else_list && (else_list->first == expr || is_tail_position(expr, else_list->first))) return true;
        }
    } else if (head == SYM_WHEN) {
        // (when cond body...) - last body is tail
        CljList *body_exprs = as_list(rest->rest);
        if (body_exprs && is_last_in_list(expr, body_exprs)) return true;
    } else if (head == SYM_DO) {
        // (do expr...) - last expr is tail
        if (is_last_in_list(expr, rest)) return true;
    } else if (head == SYM_LET) {
        // (let [bindings] body...) - last body is tail
        CljList *body_exprs = as_list(rest->rest);
        if (body_exprs) {
            if (is_last_in_list(expr, body_exprs)) return true;
            CljList *last = list_last(body_exprs);
            if (last && is_tail_position(expr, last->first)) return true;
        }
    } else if (head == SYM_COND) {
        // (cond test expr ...) - each expr is tail
        bool is_test = true;
        while (rest) {
            if (!is_test && (rest->first == expr || is_tail_position(expr, rest->first))) return true;
            rest = as_list(rest->rest);
            is_test = !is_test;
        }
    }

    return false;
}

// Check if a function call is recursive
bool is_recursive_call(CljObject *call_expr, CljObject *func_name) {
    if (!call_expr || !func_name || TAG(call_expr) != CLJ_LIST) return false;
    CljList *call_list = as_list(call_expr);
    CljObject *called_name = call_list->first;
    if (!called_name || TAG(called_name) != CLJ_SYMBOL) return false;
    CljSymbol *called_sym = as_symbol(called_name);
    CljSymbol *func_sym = as_symbol(func_name);
    return symbols_equal(called_sym, func_sym);
}

// Validate that all recur calls are in tail position
void validate_recur_positions(CljObject *body, CljObject *parent_body) {
    if (!body || TAG(body) != CLJ_LIST) return;
    CljList *body_list = as_list(body);
    if (!body_list) return;

    CljSymbol *head = (CljSymbol*)body_list->first;
    if (head == SYM_RECUR && !is_tail_position(body, parent_body)) {
        throw_exception(EXCEPTION_RUNTIME, "recur must be in tail position", __FILE__, __LINE__, 0);
        return;
    }

    // Recursively check all elements
    CljObject *rest_obj = body_list->rest;
    while (rest_obj && TAG(rest_obj) == CLJ_LIST) {
        CljList *rest = as_list(rest_obj);
        if (!rest) break;
        validate_recur_positions(rest->first, body);
        rest_obj = rest->rest;
    }
}

// Helper: Transform call to (recur ...)
static CljObject* transform_to_recur(CljList *call_list, CljSymbol *func_sym) {
    (void)func_sym;  // Unused parameter (kept for API consistency)

    CljObject *rest_obj = call_list->rest;
    CljList *new_list = (CljList*)make_list((CljObject*)SYM_RECUR, NULL);
    if (!new_list) return NULL;
    if (rest_obj && TAG(rest_obj) == CLJ_LIST) {
        RETAIN(rest_obj);
        new_list->rest = rest_obj;
    }
    RETAIN((CljObject*)new_list);
    return (CljObject*)new_list;
}

// Forward declaration
CljObject* transform_recursive_tail_calls(CljObject *body, CljObject *func_name,
                                         CljObject **params, int param_count,
                                         CljObject *parent_body);

// Helper: Transform list of expressions
static CljList* transform_list(CljList *list, CljObject *func_name,
                                CljObject **params, int param_count,
                                CljObject *parent_body) {
    if (!list) return NULL;

    CljList *new_rest = NULL, *new_current = NULL;
    CljList *current = list;

    while (current) {
        CljObject *expr = current->first;
        if (!expr) {
            current = as_list(current->rest);
            continue;
        }

        CljObject *transformed = transform_recursive_tail_calls(expr, func_name, params, param_count, parent_body);
        if (!transformed) {
            RELEASE((CljObject*)new_rest);
            return NULL;
        }

        CljList *new_node = (CljList*)make_list(transformed, NULL);
        if (!new_node) {
            RELEASE(transformed);
            RELEASE((CljObject*)new_rest);
            return NULL;
        }

        if (!new_rest) {
            new_rest = new_current = new_node;
        } else {
            new_current->rest = (CljObject*)new_node;
            new_current = new_node;
        }
        current = as_list(current->rest);
    }

    return new_rest;
}

// Helper: Build list with 1-3 elements
static CljList* build_list(CljObject *first, CljObject *second, CljObject *third) {
    CljList *list = (CljList*)make_list(first, NULL);
    if (!list) return NULL;

    if (second) {
        CljList *second_node = (CljList*)make_list(second, NULL);
        if (!second_node) {
            RELEASE((CljObject*)list);
            return NULL;
        }
        list->rest = (CljObject*)second_node;

        if (third) {
            CljList *third_node = (CljList*)make_list(third, NULL);
            if (!third_node) {
                RELEASE((CljObject*)list);
                return NULL;
            }
            second_node->rest = (CljObject*)third_node;
        }
    }

    return list;
}

// Transform recursive tail calls to recur
CljObject* transform_recursive_tail_calls(CljObject *body, CljObject *func_name,
                                         CljObject **params, int param_count,
                                         CljObject *parent_body) {
    if (!body) return NULL;
    if (TAG(body) != CLJ_LIST) return RETAIN(body), body;

    CljList *body_list = as_list(body);

    CljObject *head_obj = body_list->first;
    CljSymbol *head = (CljSymbol*)head_obj;
    CljObject *context = parent_body ? parent_body : body;

    // Transform recursive tail call
    bool is_recursive = is_recursive_call(body, func_name);
    bool is_tail = is_tail_position(body, context);
    if (is_recursive && is_tail) {
        CljSymbol *func_sym = func_name && TAG(func_name) == CLJ_SYMBOL ? as_symbol(func_name) : NULL;
        return transform_to_recur(body_list, func_sym);
    }

    // Transform special forms
    CljList *rest = as_list(body_list->rest);
    if (!rest) return RETAIN(body), body;

    if (head == SYM_IF) {
        // Transform (if cond then else)
        CljObject *cond = rest->first;
        CljObject *t_cond = cond ? transform_recursive_tail_calls(cond, func_name, params, param_count, body) : NULL;
        if (cond && !t_cond) return NULL;

        CljList *then_list = as_list(rest->rest);
        CljObject *t_then = NULL, *t_else = NULL;

        if (then_list && then_list->first) {
            t_then = transform_recursive_tail_calls(then_list->first, func_name, params, param_count, body);
            if (!t_then) {
                if (t_cond && t_cond != cond) RELEASE(t_cond);
                return NULL;
            }

            CljList *else_list = as_list(then_list->rest);
            if (else_list && else_list->first) {
                t_else = transform_recursive_tail_calls(else_list->first, func_name, params, param_count, body);
                if (!t_else) {
                    if (t_cond && t_cond != cond) RELEASE(t_cond);
                    RELEASE(t_then);
                    return NULL;
                }
            }
        }

        CljObject *cond_to_use = t_cond ? t_cond : cond;
        if (t_cond != cond && cond_to_use) {
            RETAIN(cond_to_use);
        }

        CljList *new_if = build_list((CljObject*)SYM_IF, cond_to_use, t_then);
        if (!new_if) {
            if (t_cond && t_cond != cond) RELEASE(t_cond);
            if (cond_to_use != cond && cond_to_use) RELEASE(cond_to_use);
            RELEASE(t_then);
            RELEASE(t_else);
            return NULL;
        }

        if (t_else) {
            CljList *rest_list = as_list(new_if->rest);
            if (rest_list && rest_list->rest) {
                CljList *then_node = as_list(rest_list->rest);
                if (then_node) {
                    CljList *else_node = (CljList*)make_list(t_else, NULL);
                    if (else_node) {
                        then_node->rest = (CljObject*)else_node;
                    } else {
                        RELEASE((CljObject*)new_if);
                        if (t_cond && t_cond != cond) RELEASE(t_cond);
                        RELEASE(t_then);
                        RELEASE(t_else);
                        return NULL;
                    }
                }
            }
        }

        RETAIN((CljObject*)new_if);
        return (CljObject*)new_if;
    }

    if (head == SYM_LET) {
        // Transform (let [bindings] body...)
        CljObject *bindings = rest->first;
        CljObject *t_bindings = bindings ? transform_recursive_tail_calls(bindings, func_name, params, param_count, body) : NULL;
        if (bindings && !t_bindings) return NULL;

        CljList *body_exprs = as_list(rest->rest);
        if (!body_exprs) return RETAIN(body), body;

        // Transform body expressions - preserve structure
        // If body_exprs has only one element, transform it directly
        // Otherwise, transform the list
        CljObject *t_body_obj = NULL;
        if (body_exprs->first && !body_exprs->rest) {
            // Single expression - transform directly
            t_body_obj = transform_recursive_tail_calls(body_exprs->first, func_name, params, param_count, body);
            if (!t_body_obj) {
                if (t_bindings && t_bindings != bindings) RELEASE(t_bindings);
                return NULL;
            }
        } else {
            // Multiple expressions - transform list
            CljList *t_body = transform_list(body_exprs, func_name, params, param_count, body);
            if (!t_body) {
                if (t_bindings && t_bindings != bindings) RELEASE(t_bindings);
                return NULL;
            }
            t_body_obj = (CljObject*)t_body;
        }

        CljList *new_let = build_list((CljObject*)SYM_LET, t_bindings ? t_bindings : bindings, t_body_obj);
        if (!new_let) {
            if (t_bindings && t_bindings != bindings) RELEASE(t_bindings);
            RELEASE(t_body_obj);
            return NULL;
        }

        RETAIN((CljObject*)new_let);
        return (CljObject*)new_let;
    }

    if (head == SYM_COND) {
        // Transform (cond test expr ...)
        CljList *t_rest = transform_list(rest, func_name, params, param_count, body);
        if (!t_rest) return NULL;

        CljList *new_cond = (CljList*)make_list((CljObject*)SYM_COND, NULL);
        if (!new_cond) {
            RELEASE((CljObject*)t_rest);
            return NULL;
        }

        CljList *rest_list = as_list(new_cond->rest);
        if (rest_list) {
            rest_list->rest = (CljObject*)t_rest;
        } else {
            CljList *new_rest_list = (CljList*)make_list((CljObject*)t_rest, NULL);
            if (!new_rest_list) {
                RELEASE((CljObject*)new_cond);
                RELEASE((CljObject*)t_rest);
                return NULL;
            }
            new_cond->rest = (CljObject*)new_rest_list;
        }

        RETAIN((CljObject*)new_cond);
        return (CljObject*)new_cond;
    }

    // Transform nested expressions
    // CRITICAL: Only transform recursive calls that are in tail position
    // For function calls like (+ (fib ...) (fib ...)), neither argument is in tail position
    // because the result of + is used, not the arguments directly
    // Only transform if the entire expression is in tail position
    if (is_tail_position(body, context)) {
        // The entire expression is in tail position - check if it's a recursive call
        if (is_recursive_call(body, func_name)) {
            CljSymbol *func_sym = func_name && TAG(func_name) == CLJ_SYMBOL ? as_symbol(func_name) : NULL;
            return transform_to_recur(body_list, func_sym);
        }
        // Not a recursive call, but in tail position - don't transform nested calls
        // because they are not in tail position (e.g., arguments of + are not in tail position)
        return RETAIN(body), body;
    } else {
        // Not in tail position - don't transform recursive calls
        // For non-special forms (like +, -, etc.), don't transform nested calls
        // because they are not in tail position
        return RETAIN(body), body;
    }
}
