/*
 * Function-body optimization walk for Tiny-CLJ.
 *
 * This file implements a single recursive AST walk that applies multiple
 * optimizations in one traversal.
 */

#include "optimize.h"
#include "object.h"
#include "symbol.h"
#include "exception.h"
#include "list.h"
#include "memory.h"
#include "ast.h"
#include "vector.h"
#include "record.h"
#include "value.h"
#include <string.h>
#include <stdio.h>

typedef struct RecordBinding {
    const struct RecordBinding *parent;
    CljSymbol *symbol;
    ID target_expr;  // AST constructor call that yields a known record shape, or NULL to shadow.
} RecordBinding;

typedef struct RecordSlotFrame {
    const struct RecordSlotFrame *parent;
    const ID *slot_targets;  // per-slot constructor target (AST_CALL) or NULL
    unsigned int slot_count;
} RecordSlotFrame;

// Check if expr is the last element in list (single traversal)
static bool is_last_in_list(CljObject *expr, CljList *list) {
    if (!list) return false;
    while (list->rest) list = as_list(list->rest);
    return list->first == expr;
}

static bool symbol_cname_equals(CljSymbol *sym, const char *name) {
    if (!sym || !name || !sym->cname) return false;
    return strcmp(sym->cname, name) == 0;
}

// Resolve a descriptor from constructor symbols like ->Type or map->Type.
static CljRecordDescriptor *record_descriptor_from_ctor_symbol(CljSymbol *ctor_sym) {
    if (!ctor_sym || !ctor_sym->cname) return NULL;

    const char *cname = ctor_sym->cname;
    const char *type_name = NULL;
    if (strncmp(cname, "map->", 5) == 0 && cname[5] != '\0') {
        type_name = cname + 5;
    } else if (strncmp(cname, "->", 2) == 0 && cname[2] != '\0') {
        type_name = cname + 2;
    } else {
        return NULL;
    }

    CljSymbol *type_sym = ctor_sym->ns_name
        ? intern_symbol(ctor_sym->ns_name, type_name)
        : intern_symbol_global(type_name);
    if (!type_sym) return NULL;

    return record_descriptor_lookup((ID)type_sym);
}

static bool record_binding_lookup(const RecordBinding *env, CljSymbol *symbol, ID *out_target_expr) {
    if (!out_target_expr) return false;
    for (const RecordBinding *entry = env; entry; entry = entry->parent) {
        if (entry->symbol == symbol) {
            *out_target_expr = entry->target_expr;
            return true;
        }
    }
    return false;
}

static CljRecordDescriptor *record_descriptor_from_ctor_call(ID target_expr) {
    if (!target_expr || TAG(target_expr) != CLJ_AST_CALL) return NULL;
    CljASTCall *target_call = as_ast_call(target_expr);
    if (!target_call || !target_call->op || TAG(target_call->op) != CLJ_SYMBOL) {
        return NULL;
    }
    return record_descriptor_from_ctor_symbol(as_symbol(target_call->op));
}

static bool slot_frame_resolve_target(const RecordSlotFrame *slot_env, CljSlotRef *slot_ref, ID *out_target) {
    if (!slot_ref || !out_target) return false;

    const RecordSlotFrame *frame = slot_env;
    uint8_t depth = slot_ref->depth;
    while (frame && depth > 0) {
        frame = frame->parent;
        depth--;
    }
    if (!frame) return false;
    if ((unsigned int)slot_ref->slot >= frame->slot_count) return false;

    *out_target = frame->slot_targets ? frame->slot_targets[(unsigned int)slot_ref->slot] : NULL;
    return true;
}

// Resolve target expression to a constructor call through local bindings.
static ID resolve_record_target_expr(ID target_expr,
                                     const RecordBinding *binding_env,
                                     const RecordSlotFrame *slot_env) {
    ID current = target_expr;
    for (int depth = 0; depth < 32; depth++) {
        if (!current) return NULL;
        if (TAG(current) == CLJ_AST_CALL) {
            return record_descriptor_from_ctor_call(current) ? current : NULL;
        }
        if (TAG(current) == CLJ_SLOT_REF) {
            ID slot_target = NULL;
            if (!slot_frame_resolve_target(slot_env, (CljSlotRef *)current, &slot_target)) {
                return NULL;
            }
            if (!slot_target) return NULL;
            current = slot_target;
            continue;
        }
        if (TAG(current) != CLJ_SYMBOL) {
            return NULL;
        }

        ID bound_target = NULL;
        if (!record_binding_lookup(binding_env, as_symbol(current), &bound_target)) {
            return NULL;
        }
        if (!bound_target) {
            return NULL;
        }
        current = bound_target;
    }
    return NULL;
}

// Infer record field index for constant keyword lookups on constructor targets.
static int infer_record_field_index_from_target(ID target_expr, ID key,
                                                const RecordBinding *binding_env,
                                                const RecordSlotFrame *slot_env) {
    if (!target_expr || !key || !IS_KEYWORD(key)) {
        return -1;
    }

    ID resolved_target = resolve_record_target_expr(target_expr, binding_env, slot_env);
    if (!resolved_target) {
        return -1;
    }

    CljRecordDescriptor *desc = record_descriptor_from_ctor_call(resolved_target);
    if (!desc || !desc->field_keys) {
        return -1;
    }

    unsigned int field_count = vector_count(desc->field_keys);
    for (unsigned int i = 0; i < field_count; i++) {
        ID candidate = vector_nth(desc->field_keys, i);
        if (candidate == key || clj_equal(candidate, key)) {
            return (int)i;
        }
    }
    return -1;
}

// Rewrite (:k (->Type ...)) and (get (->Type ...) :k) into record-get-index.
// Returns true when rewrite was applied.
static bool rewrite_record_lookup_call(CljASTCall *call,
                                       const RecordBinding *binding_env,
                                       const RecordSlotFrame *slot_env) {
    if (!call || !call->op || !call->args) return false;

    unsigned int argc = vector_count(call->args);
    if (argc == 0) return false;

    ID target_expr = NULL;
    ID key = NULL;
    ID default_expr = NULL;

    if (IS_KEYWORD(call->op)) {
        if (argc != 1 && argc != 2) return false;
        key = call->op;
        target_expr = vector_nth(call->args, 0);
        default_expr = (argc == 2) ? vector_nth(call->args, 1) : NULL;
    } else if (TAG(call->op) == CLJ_SYMBOL) {
        CljSymbol *op_sym = as_symbol(call->op);
        if (!symbol_cname_equals(op_sym, "get")) return false;
        if (argc != 2 && argc != 3) return false;
        target_expr = vector_nth(call->args, 0);
        key = vector_nth(call->args, 1);
        default_expr = (argc == 3) ? vector_nth(call->args, 2) : NULL;
        if (!IS_KEYWORD(key)) return false;
    } else {
        return false;
    }

    int index = infer_record_field_index_from_target(target_expr, key, binding_env, slot_env);
    if (index < 0) return false;

    CljPersistentVector *new_args = make_vector(3, STRONG);
    if (!new_args) return false;
    vector_conj_inplace(&new_args, target_expr);
    vector_conj_inplace(&new_args, fixnum(index));
    vector_conj_inplace(&new_args, default_expr);

    CljSymbol *fast_op = intern_symbol_global("record-get-index");
    if (!fast_op) {
        RELEASE(new_args);
        return false;
    }

    ASSIGN(call->op, (ID)fast_op);
    ASSIGN(call->args, new_args);
    RELEASE(new_args);
    return true;
}

// Get last element and check tail position (for AST_CALL bodies)
static bool last_arg_tail_position(CljObject *expr, CljPersistentVector *args) {
    if (!args) return false;
    unsigned int count = vector_count(args);
    if (count == 0) return false;
    ID last = vector_nth(args, count - 1);
    if (last == expr) return true;
    return is_tail_position(expr, last);
}

// Get last element and check tail position (for let bodies)
static bool last_is_tail_position(CljObject *expr, CljList *list) {
    if (!list) return false;
    while (list->rest) list = as_list(list->rest);
    return is_tail_position(expr, list->first);
}

// Check if an expression is in tail position within a body
bool is_tail_position(CljObject *expr, CljObject *body) {
    if (!expr || !body) return false;

    if (TAG(body) == CLJ_AST_CALL) {
        CljASTCall *call = (CljASTCall*)body;
        CljSymbol *head = (call && call->op && TAG(call->op) == CLJ_SYMBOL) ? as_symbol(call->op) : NULL;
        CljPersistentVector *args = call ? call->args : NULL;
        unsigned int argc = args ? vector_count(args) : 0;

        if (!head) {
            return expr == body;
        }

        if (head == SYM_IF) {
            if (argc >= 2) {
                ID then_expr = vector_nth(args, 1);
                if (then_expr == expr || is_tail_position(expr, then_expr)) return true;
            }
            if (argc >= 3) {
                ID else_expr = vector_nth(args, 2);
                if (else_expr == expr || is_tail_position(expr, else_expr)) return true;
            }
            return false;
        } else if (head == SYM_WHEN) {
            return last_arg_tail_position(expr, args);
        } else if (head == SYM_DO) {
            return last_arg_tail_position(expr, args);
        } else if (head == SYM_LET) {
            return last_arg_tail_position(expr, args);
        } else if (head == SYM_COND) {
            for (unsigned int i = 1; i < argc; i += 2) {
                ID branch = vector_nth(args, i);
                if (branch == expr || is_tail_position(expr, branch)) return true;
            }
            return false;
        }

        return expr == body;
    }

    if (!is_list_type(TAG(body))) return false;

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
            if (last_is_tail_position(expr, body_exprs)) return true;
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
    if (!call_expr || !func_name) return false;
    CljObject *called_name = NULL;
    if (is_list_type(TAG(call_expr))) {
        CljList *call_list = as_list(call_expr);
        called_name = call_list ? call_list->first : NULL;
    } else if (TAG(call_expr) == CLJ_AST_CALL) {
        CljASTCall *call = (CljASTCall*)call_expr;
        called_name = call ? call->op : NULL;
    } else {
        return false;
    }
    if (!called_name || TAG(called_name) != CLJ_SYMBOL) return false;
    CljSymbol *called_sym = as_symbol(called_name);
    CljSymbol *func_sym = as_symbol(func_name);
    return called_sym == func_sym;
}

// Validate that all recur calls are in tail position
void validate_recur_positions(CljObject *body, CljObject *parent_body) {
    if (!body || !is_list_type(TAG(body))) return;
    CljList *body_list = as_list(body);
    if (!body_list) return;

    CljSymbol *head = (CljSymbol*)body_list->first;
    if (head == SYM_RECUR && !is_tail_position(body, parent_body)) {
        throw_exception(EXCEPTION_RUNTIME, "recur must be in tail position", __FILE__, __LINE__, 0);
        return;
    }

    // Recursively check all elements
    CljObject *rest_obj = body_list->rest;
    while (rest_obj && is_list_type(TAG(rest_obj))) {
        CljList *rest = as_list(rest_obj);
        if (!rest) break;
        validate_recur_positions(rest->first, body);
        rest_obj = rest->rest;
    }
}

// Build (recur ...) from an existing call tail. Returns a retained list.
static CljObject* make_recur_list(CljList *call_list, CljSymbol *func_sym) {
    (void)func_sym;  // Unused parameter (kept for API consistency)

    CljObject *rest_obj = call_list->rest;
    CljList *new_list = (CljList*)make_list((CljObject*)SYM_RECUR, NULL);
    if (!new_list) return NULL;
    if (rest_obj && is_list_type(TAG(rest_obj))) {
        RETAIN(rest_obj);
        new_list->rest = rest_obj;
    }
    return (CljObject*)new_list;
}

// Forward declaration
static CljObject* optimize_function_body_walk_with_bindings_owned(CljObject *body, CljObject *func_name,
                                                            CljObject **params, int param_count,
                                                            CljObject *parent_body,
                                                            const RecordBinding *binding_env,
                                                            const RecordSlotFrame *slot_env);

// Optimize one AST_CALL argument in-place via the same optimizer walk.
static bool optimize_ast_call_arg_inplace(CljASTCall *call, unsigned int index,
                                          CljObject *func_name,
                                          CljObject **params, int param_count,
                                          CljObject *parent_body,
                                          const RecordBinding *binding_env,
                                          const RecordSlotFrame *slot_env) {
    if (!call || !call->args) return true;
    unsigned int argc = vector_count(call->args);
    if (index >= argc) return true;

    ID arg = vector_nth(call->args, index);
    if (!arg) return true;

    CljObject *transformed = optimize_function_body_walk_with_bindings_owned(arg, func_name, params, param_count,
                                                                       parent_body, binding_env, slot_env);
    if (!transformed) return false;
    if (transformed != arg) {
        vector_assoc_inplace(&call->args, index, transformed);
    }
    RELEASE(transformed);
    return true;
}

// Helper: Transform list of expressions
// Mutates the list in-place
// Returns the original list (now mutated) if transformations occurred
static CljList* optimize_list_walk_inplace(CljList *list, CljObject *func_name,
                                           CljObject **params, int param_count,
                                           CljObject *parent_body,
                                           const RecordBinding *binding_env,
                                           const RecordSlotFrame *slot_env) {
    if (!list) return NULL;

    for (CljList *current = list; current; current = as_list(current->rest)) {
        CljObject *expr = current->first;
        if (!expr) continue;

        CljObject *transformed = optimize_function_body_walk_with_bindings_owned(expr, func_name, params, param_count,
                                                                           parent_body, binding_env, slot_env);
        if (!transformed) return NULL;

        if (transformed != expr) {
            RELEASE(expr);
            current->first = transformed ? RETAIN(transformed) : NULL;
        }
        RELEASE(transformed);
    }

    return list;
}

static bool expr_contains_recur(ID expr) {
    if (!expr) return false;

    if (TAG(expr) == CLJ_AST_CALL) {
        CljASTCall *call = as_ast_call(expr);
        if (!call) return false;
        if (call->op == (ID)SYM_RECUR) return true;
        if (expr_contains_recur(call->op)) return true;
        if (call->args) {
            unsigned int argc = vector_count(call->args);
            for (unsigned int i = 0; i < argc; i++) {
                if (expr_contains_recur(vector_nth(call->args, i))) return true;
            }
        }
        return false;
    }

    if (TAG(expr) == CLJ_VECTOR_PERSISTENT || TAG(expr) == CLJ_VECTOR_TRANSIENT) {
        CljPersistentVector *vec = as_vector(expr);
        if (!vec) return false;
        unsigned int count = vector_count(vec);
        for (unsigned int i = 0; i < count; i++) {
            if (expr_contains_recur(vector_nth(vec, i))) return true;
        }
        return false;
    }

    if (!is_list_type(TAG(expr))) return false;
    CljList *list = as_list(expr);
    while (list) {
        if (expr_contains_recur(list->first)) return true;
        if (!list->rest || !is_list_type(TAG(list->rest))) break;
        list = as_list(list->rest);
    }
    return false;
}

// Build a list with 1-3 elements. Returns an owned list.
static CljList* make_list3(CljObject *first, CljObject *second, CljObject *third) {
    CljList *list = (CljList*)make_list(first, NULL);
    if (!list) return NULL;

    if (second) {
        CljList *second_node = (CljList*)make_list(second, NULL);
        if (!second_node) {
            RELEASE(list);
            return NULL;
        }
        ASSIGN(list->rest, second_node);
        RELEASE(second_node);

        if (third) {
            CljList *third_node = (CljList*)make_list(third, NULL);
            if (!third_node) {
                RELEASE(list);
                return NULL;
            }
            ASSIGN(second_node->rest, third_node);
            RELEASE(third_node);
        }
    }

    return list;
}

// Build an AST_CALL (recur ...) node while preserving existing call arguments.
// Returns an owned AST_CALL.
static CljObject* make_recur_ast_call(CljASTCall *call) {
    if (!call) return NULL;
    // Preserve AST_CALL representation so recur is evaluated as a special form.
    CljASTCall *recur_call = make_ast_call((CljObject*)SYM_RECUR, call->args);
    if (!recur_call) return NULL;
    return (CljObject*)recur_call;
}

// Optimize let-/loop-/binding-style forms with a bindings vector as first argument.
static bool optimize_binding_form_ast_call(CljASTCall *call,
                                           CljObject *func_name,
                                           CljObject **params, int param_count,
                                           CljObject *parent_body,
                                           const RecordBinding *binding_env,
                                           const RecordSlotFrame *slot_env,
                                           bool propagate_to_body) {
    if (!call || !call->args) return true;
    unsigned int argc = vector_count(call->args);
    if (argc == 0) return true;

    ID bindings_obj = vector_nth(call->args, 0);
    if (!bindings_obj ||
        (TAG(bindings_obj) != CLJ_VECTOR_PERSISTENT && TAG(bindings_obj) != CLJ_VECTOR_TRANSIENT)) {
        // Fallback for unexpected shapes: optimize all args with current environment.
        for (unsigned int i = 0; i < argc; i++) {
            if (!optimize_ast_call_arg_inplace(call, i, func_name, params, param_count, parent_body,
                                               binding_env, slot_env)) {
                return false;
            }
        }
        return true;
    }

    CljPersistentVector *bindings = as_vector(bindings_obj);
    unsigned int binding_count = bindings ? vector_count(bindings) : 0;
    unsigned int pair_count = binding_count / 2;

    RecordBinding *entries = NULL;
    ID *slot_targets = NULL;
    unsigned int entries_used = 0;
    if (pair_count > 0) {
        entries = (RecordBinding *)CLJ_MALLOC(sizeof(RecordBinding) * pair_count);
        slot_targets = (ID *)CLJ_MALLOC(sizeof(ID) * pair_count);
        if (!entries || !slot_targets) {
            CLJ_FREE(entries);
            CLJ_FREE(slot_targets);
            return false;
        }
        for (unsigned int i = 0; i < pair_count; i++) {
            slot_targets[i] = NULL;
        }
    }

    const RecordBinding *current_env = binding_env;
    for (unsigned int i = 0; i + 1 < binding_count; i += 2) {
        unsigned int slot_index = i / 2;
        ID init_expr = vector_nth(bindings, i + 1);
        CljObject *transformed = init_expr
            ? optimize_function_body_walk_with_bindings_owned(init_expr, func_name, params, param_count,
                                                        parent_body, current_env, slot_env)
            : NULL;
        if (init_expr && !transformed) {
            CLJ_FREE(entries);
            CLJ_FREE(slot_targets);
            return false;
        }
        if (init_expr && transformed != init_expr) {
            vector_assoc_inplace(&bindings, i + 1, transformed);
        }
        if (init_expr) {
            RELEASE(transformed);
        }

        ID sym_id = vector_nth(bindings, i);
        ID init_after_opt = vector_nth(bindings, i + 1);
        ID resolved_target = resolve_record_target_expr(init_after_opt, current_env, slot_env);
        if (slot_targets && slot_index < pair_count) {
            slot_targets[slot_index] = resolved_target;
        }

        if (!sym_id || TAG(sym_id) != CLJ_SYMBOL || entries_used >= pair_count) {
            continue;
        }

        entries[entries_used].parent = current_env;
        entries[entries_used].symbol = as_symbol(sym_id);
        entries[entries_used].target_expr = resolved_target;
        current_env = &entries[entries_used];
        entries_used++;
    }

    if ((ID)bindings != bindings_obj) {
        vector_assoc_inplace(&call->args, 0, (ID)bindings);
    }

    const RecordBinding *body_env = propagate_to_body ? current_env : binding_env;
    RecordSlotFrame body_frame = {
        .parent = slot_env,
        .slot_targets = slot_targets,
        .slot_count = pair_count
    };
    const RecordSlotFrame *body_slot_env = propagate_to_body ? &body_frame : slot_env;
    for (unsigned int i = 1; i < argc; i++) {
        if (!optimize_ast_call_arg_inplace(call, i, func_name, params, param_count, parent_body,
                                           body_env, body_slot_env)) {
            CLJ_FREE(entries);
            CLJ_FREE(slot_targets);
            return false;
        }
    }

    CLJ_FREE(entries);
    CLJ_FREE(slot_targets);
    return true;
}

// Optimize function body via one recursive walk with multiple optimizations.
static CljObject* optimize_function_body_walk_with_bindings_owned(CljObject *body, CljObject *func_name,
                                                            CljObject **params, int param_count,
                                                            CljObject *parent_body,
                                                            const RecordBinding *binding_env,
                                                            const RecordSlotFrame *slot_env) {
    if (!body) return NULL;
    if (TAG(body) == CLJ_AST_CALL) {
        CljASTCall *call = (CljASTCall*)body;
        CljObject *context = parent_body ? parent_body : body;
        bool is_recursive = is_recursive_call(body, func_name);
        bool is_tail = is_tail_position(body, context);
        if (is_recursive && is_tail) {
            CljObject *recur_list = make_recur_ast_call(call);
            if (!recur_list) return NULL;
            return recur_list;
        }

        CljSymbol *head = (call && call->op && TAG(call->op) == CLJ_SYMBOL) ? as_symbol(call->op) : NULL;
        if (!head || !call->args) {
            RETAIN(body);
            return body;
        }

        (void)rewrite_record_lookup_call(call, binding_env, slot_env);

        unsigned int argc = vector_count(call->args);
        if (head == SYM_LET || head == SYM_BINDING || head == SYM_LOOP) {
            // `loop` bindings can be reassigned by recur. Only propagate bindings into the
            // body when no recur is present (safe let-like subset).
            bool propagate_to_body = true;
            if (head == SYM_LOOP) {
                for (unsigned int i = 1; i < argc; i++) {
                    if (expr_contains_recur(vector_nth(call->args, i))) {
                        propagate_to_body = false;
                        break;
                    }
                }
            }
            if (!optimize_binding_form_ast_call(call, func_name, params, param_count, body,
                                                binding_env, slot_env, propagate_to_body)) {
                return NULL;
            }
            RETAIN(body);
            return body;
        }

        if (head == SYM_IF) {
            for (unsigned int i = 0; i < argc && i < 3; i++) {
                if (!optimize_ast_call_arg_inplace(call, i, func_name, params, param_count,
                                                   body, binding_env, slot_env)) {
                    return NULL;
                }
            }
            RETAIN(body);
            return body;
        }

        if (head == SYM_WHEN || head == SYM_DO || head == SYM_COND) {
            for (unsigned int i = 0; i < argc; i++) {
                if (!optimize_ast_call_arg_inplace(call, i, func_name, params, param_count,
                                                   body, binding_env, slot_env)) {
                    return NULL;
                }
            }
            RETAIN(body);
            return body;
        }

        RETAIN(body);
        return body;
    }
    if (!is_list_type(TAG(body))) return RETAIN(body), body;

    CljList *body_list = as_list(body);

    CljObject *head_obj = body_list->first;
    CljSymbol *head = (CljSymbol*)head_obj;
    CljObject *context = parent_body ? parent_body : body;

    // Tail-call optimization: rewrite direct self-tail calls to `recur`.
    bool is_recursive = is_recursive_call(body, func_name);
    bool is_tail = is_tail_position(body, context);
    if (is_recursive && is_tail) {
        CljSymbol *func_sym = func_name && TAG(func_name) == CLJ_SYMBOL ? as_symbol(func_name) : NULL;
        return make_recur_list(body_list, func_sym);
    }

    // Recurse through special forms (same optimization walk).
    CljList *rest = as_list(body_list->rest);
    if (!rest) return RETAIN(body), body;

    if (head == SYM_IF) {
        // Transform (if cond then else)
        CljObject *cond = rest->first;
        CljObject *t_cond = cond ? optimize_function_body_walk_with_bindings_owned(cond, func_name, params, param_count,
                                                                              body, binding_env, slot_env)
                                 : NULL;
        if (cond && !t_cond) return NULL;

        CljList *then_list = as_list(rest->rest);
        CljObject *t_then = NULL, *t_else = NULL;

        if (then_list && then_list->first) {
            t_then = optimize_function_body_walk_with_bindings_owned(then_list->first, func_name, params, param_count,
                                                               body, binding_env, slot_env);
            if (!t_then) {
                RELEASE(t_cond);
                return NULL;
            }

            CljList *else_list = as_list(then_list->rest);
            if (else_list && else_list->first) {
                t_else = optimize_function_body_walk_with_bindings_owned(else_list->first, func_name, params, param_count,
                                                                   body, binding_env, slot_env);
                if (!t_else) {
                    RELEASE(t_cond);
                    RELEASE(t_then);
                    return NULL;
                }
            }
        }

        CljObject *cond_to_use = t_cond ? t_cond : cond;

        CljList *new_if = make_list3((CljObject*)SYM_IF, cond_to_use, t_then);
        if (!new_if) {
            RELEASE(t_cond);
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
                        RELEASE(new_if);
                        RELEASE(t_cond);
                        RELEASE(t_then);
                        RELEASE(t_else);
                        return NULL;
                    }
                }
            }
        }

        RELEASE(t_cond);
        RELEASE(t_then);
        RELEASE(t_else);
        return (CljObject*)new_if;
    }

    if (head == SYM_LET) {
        // Transform (let [bindings] body...)
        CljObject *bindings = rest->first;
        CljObject *t_bindings = bindings
            ? optimize_function_body_walk_with_bindings_owned(bindings, func_name, params, param_count,
                                                        body, binding_env, slot_env)
            : NULL;
        if (bindings && !t_bindings) return NULL;

        CljList *body_exprs = as_list(rest->rest);
        if (!body_exprs) return RETAIN(body), body;

        // Transform body expressions - preserve structure
        // If body_exprs has only one element, transform it directly
        // Otherwise, transform the list
        CljObject *t_body_obj = NULL;
        bool t_body_obj_owned = false;
        if (body_exprs->first && !body_exprs->rest) {
            // Single expression - transform directly
            t_body_obj = optimize_function_body_walk_with_bindings_owned(body_exprs->first, func_name, params, param_count,
                                                                   body, binding_env, slot_env);
            if (!t_body_obj) {
                RELEASE(t_bindings);
                return NULL;
            }
            t_body_obj_owned = true;
        } else {
            // Multiple expressions - transform list
            CljList *t_body = optimize_list_walk_inplace(body_exprs, func_name, params, param_count,
                                                         body, binding_env, slot_env);
            if (!t_body) {
                RELEASE(t_bindings);
                return NULL;
            }
            t_body_obj = (CljObject*)t_body;
        }

        CljList *new_let = make_list3((CljObject*)SYM_LET, t_bindings ? t_bindings : bindings, t_body_obj);
        if (!new_let) {
            RELEASE(t_bindings);
            if (t_body_obj_owned) RELEASE(t_body_obj);
            return NULL;
        }

        RELEASE(t_bindings);
        if (t_body_obj_owned) RELEASE(t_body_obj);
        return (CljObject*)new_let;
    }

    if (head == SYM_COND) {
        // Transform (cond test expr ...)
        CljList *t_rest = optimize_list_walk_inplace(rest, func_name, params, param_count,
                                                     body, binding_env, slot_env);
        if (!t_rest) return NULL;

        // If nothing changed, return the original cond form
        if (t_rest == rest) {
            return RETAIN(body), body;
        }

        // Create new cond form: (cond test expr ...)
        CljList *new_cond = (CljList*)make_list((CljObject*)SYM_COND, (CljList*)t_rest);
        if (!new_cond) {
            return NULL;
        }

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
            return make_recur_list(body_list, func_sym);
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

CljObject* optimize_function_body_walk(CljObject *body, CljObject *func_name,
                                       CljObject **params, int param_count,
                                       CljObject *parent_body) {
    return optimize_function_body_walk_with_bindings_owned(body, func_name, params, param_count,
                                                     parent_body, NULL, NULL);
}
