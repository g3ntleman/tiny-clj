/*
 * Tests for pretreated (compiled) AST execution.
 *
 * These tests only validate correctness and safe fallback behavior.
 * Performance is measured separately via scripts/profile_performance.sh.
 */

#include "tests_common.h"
#include "../eval.h"

TEST(test_compiled_ast_basic_specials_and_calls) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    eval_set_use_compiled_ast(1);

    ID r_if = eval_string("(if true 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r_if);
    TEST_ASSERT_TRUE(is_fixnum(r_if));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(r_if));

    ID r_do = eval_string("(do 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r_do);
    TEST_ASSERT_TRUE(is_fixnum(r_do));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(r_do));

    ID r_let = eval_string("(let [x 1] (+ x 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r_let);
    TEST_ASSERT_TRUE(is_fixnum(r_let));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(r_let));

    ID r_fn = eval_string("((fn [n] (+ n 1)) 41)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r_fn);
    TEST_ASSERT_TRUE(is_fixnum(r_fn));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(r_fn));

    eval_set_use_compiled_ast(0);
}

