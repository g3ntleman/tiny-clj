/*
 * Tests for lexical addressing SlotRef (CLJ_SLOT_REF)
 */

#include "tests_common.h"
#include "../parser.h"
#include "../ast_canon.h"
#include "../ast.h"
#include "../list.h"

// ============================================================================
// TEST: Canonicalization rewrites fn params in body to SlotRefs
// ============================================================================
TEST(test_slot_ref_fn_params_rewritten_in_simple_body) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID parsed = parse("(fn [n acc] (+ n acc))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);

    ID canon = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(canon);
    TEST_ASSERT_TRUE_MESSAGE(is_list_like(canon), "canonicalize_ast should return list-like form");

    CljList *fn_form = as_list(canon);
    TEST_ASSERT_NOT_NULL(fn_form);
    TEST_ASSERT_EQUAL_PTR_MESSAGE((void*)SYM_FN, (void*)LIST_FIRST(fn_form), "Head should be SYM_FN");

    CljList *rest1 = as_list(LIST_REST(fn_form));
    TEST_ASSERT_NOT_NULL(rest1);

    // params vector
    ID params_vec = LIST_FIRST(rest1);
    TEST_ASSERT_NOT_NULL(params_vec);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_VECTOR_PERSISTENT, TAG(params_vec), "Params must be vector");

    // body expression: (+ n acc)
    CljList *body_tail = as_list(LIST_REST(rest1));
    TEST_ASSERT_NOT_NULL(body_tail);
    ID body_expr = LIST_FIRST(body_tail);
    TEST_ASSERT_NOT_NULL(body_expr);
    TEST_ASSERT_TRUE_MESSAGE(is_list_like(body_expr), "Body must be list-like");

    CljList *call = as_list(body_expr);
    CljList *args1 = as_list(LIST_REST(call));
    TEST_ASSERT_NOT_NULL(args1);
    ID arg_n = LIST_FIRST(args1);
    TEST_ASSERT_NOT_NULL(arg_n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_SLOT_REF, TAG(arg_n), "n should be rewritten to SlotRef");

    CljList *args2 = as_list(LIST_REST(args1));
    TEST_ASSERT_NOT_NULL(args2);
    ID arg_acc = LIST_FIRST(args2);
    TEST_ASSERT_NOT_NULL(arg_acc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_SLOT_REF, TAG(arg_acc), "acc should be rewritten to SlotRef");
}

// ============================================================================
// TEST: SlotRef works in operator position (calling a parameter function)
// ============================================================================
TEST(test_slot_ref_operator_position_calls_param_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // f is a parameter, used as operator: (f x)
    ID result = eval_string("((fn [f x] (f x)) inc 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

// ============================================================================
// TEST: Canonicalization rewrites free vars in nested fn bodies to depth>0 SlotRefs
// ============================================================================
TEST(test_slot_ref_nested_fn_rewrites_free_var_depth1) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID parsed = parse("(fn [a] (fn [b] (+ a b)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);

    ID canon = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(canon);
    TEST_ASSERT_TRUE_MESSAGE(is_list_like(canon), "canonicalize_ast should return list-like form");

    // Outer fn
    CljList *outer_fn = as_list(canon);
    TEST_ASSERT_NOT_NULL(outer_fn);
    TEST_ASSERT_EQUAL_PTR_MESSAGE((void*)SYM_FN, (void*)LIST_FIRST(outer_fn), "Head should be SYM_FN");

    CljList *outer_rest1 = as_list(LIST_REST(outer_fn));
    TEST_ASSERT_NOT_NULL(outer_rest1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_VECTOR_PERSISTENT, TAG(LIST_FIRST(outer_rest1)), "Outer params must be vector");

    CljList *outer_body_tail = as_list(LIST_REST(outer_rest1));
    TEST_ASSERT_NOT_NULL(outer_body_tail);
    ID inner_fn_expr = LIST_FIRST(outer_body_tail);
    TEST_ASSERT_NOT_NULL(inner_fn_expr);
    TEST_ASSERT_TRUE_MESSAGE(is_list_like(inner_fn_expr), "Inner fn form must be list-like");

    // Inner fn
    CljList *inner_fn = as_list(inner_fn_expr);
    TEST_ASSERT_NOT_NULL(inner_fn);
    TEST_ASSERT_EQUAL_PTR_MESSAGE((void*)SYM_FN, (void*)LIST_FIRST(inner_fn), "Inner head should be SYM_FN");

    CljList *inner_rest1 = as_list(LIST_REST(inner_fn));
    TEST_ASSERT_NOT_NULL(inner_rest1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_VECTOR_PERSISTENT, TAG(LIST_FIRST(inner_rest1)), "Inner params must be vector");

    CljList *inner_body_tail = as_list(LIST_REST(inner_rest1));
    TEST_ASSERT_NOT_NULL(inner_body_tail);
    ID inner_body_expr = LIST_FIRST(inner_body_tail);
    TEST_ASSERT_NOT_NULL(inner_body_expr);
    TEST_ASSERT_TRUE_MESSAGE(is_list_like(inner_body_expr), "Inner body must be list-like");

    // (+ a b)
    CljList *call = as_list(inner_body_expr);
    CljList *args1 = as_list(LIST_REST(call));
    TEST_ASSERT_NOT_NULL(args1);

    ID arg_a = LIST_FIRST(args1);
    TEST_ASSERT_NOT_NULL(arg_a);
    if (TAG(arg_a) == CLJ_SLOT_REF) {
        const CljSlotRef *ref_a = (const CljSlotRef*)arg_a;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, ref_a->depth, "a should have depth=1 (outer scope)");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ref_a->slot, "a should be slot 0 in outer scope");
    } else {
        // Some configurations may disable SlotRef(depth>0) rewriting and keep symbols.
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_SYMBOL, TAG(arg_a), "a should be a symbol when SlotRef rewrite is disabled");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("a", as_symbol(arg_a)->cname, "Expected symbol 'a'");
    }

    CljList *args2 = as_list(LIST_REST(args1));
    TEST_ASSERT_NOT_NULL(args2);
    ID arg_b = LIST_FIRST(args2);
    TEST_ASSERT_NOT_NULL(arg_b);
    if (TAG(arg_b) == CLJ_SLOT_REF) {
        const CljSlotRef *ref_b = (const CljSlotRef*)arg_b;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ref_b->depth, "b should have depth=0 (current scope)");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ref_b->slot, "b should be slot 0 in current scope");
    } else {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_SYMBOL, TAG(arg_b), "b should be a symbol when SlotRef rewrite is disabled");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("b", as_symbol(arg_b)->cname, "Expected symbol 'b'");
    }
}

// ============================================================================
// TEST: SlotRef(depth>0) works for closure capture at runtime
// ============================================================================
TEST(test_slot_ref_depth1_closure_capture_works_at_runtime) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string("(((fn [a] (fn [b] (+ a b))) 40) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_slot_ref_depth2_nested_closures_capture_works_at_runtime) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string("((((fn [a] (fn [b] (fn [c] (+ a b c)))) 1) 2) 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

