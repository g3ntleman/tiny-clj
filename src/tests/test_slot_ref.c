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
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_VECTOR, TAG(params_vec), "Params must be vector");

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

