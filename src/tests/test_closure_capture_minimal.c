/*
 * Minimal closure capture tests (lexicals + dynamic vars).
 *
 * These tests are intentionally small and focus on: a closure created in an
 * inner scope still being able to resolve lexical locals later, including when
 * invoked after the outer scope has returned.
 */

#include "tests_common.h"

static void require_capture_minimal(void) {
    // Ensure the fixture namespace is loaded.
    // Path mapping: test.closures.capture-minimal -> libs/test/closures/capture_minimal.clj
    (void)eval_string("(require 'test.closures.capture-minimal)", g_test_eval_state);
}

TEST(test_closure_capture_fn_param_after_return) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_capture_minimal();

    // (let [f (make-adder 10)] (f 5)) => 15
    CljObject *out = eval_string(
        "(let [f (test.closures.capture-minimal/make-adder 10)] (f 5))",
        g_test_eval_state
    );
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(is_fixnum(out));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(out));
}

TEST(test_closure_capture_let_local_after_return) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_capture_minimal();

    // (let [f (make-adder-let 7)] (f 8)) => 15
    CljObject *out = eval_string(
        "(let [f (test.closures.capture-minimal/make-adder-let 7)] (f 8))",
        g_test_eval_state
    );
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(is_fixnum(out));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(out));
}

TEST(test_closure_capture_nested_closures) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_capture_minimal();

    // (((make-nested 1) 2) 3) => 6
    CljObject *out = eval_string(
        "(((test.closures.capture-minimal/make-nested 1) 2) 3)",
        g_test_eval_state
    );
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(is_fixnum(out));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(out));
}

TEST(test_closure_capture_loop_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_capture_minimal();

    // ((make-loop-capturer 9)) => 9
    CljObject *out = eval_string(
        "((test.closures.capture-minimal/make-loop-capturer 9))",
        g_test_eval_state
    );
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(is_fixnum(out));
    TEST_ASSERT_EQUAL_INT(9, as_fixnum(out));
}

TEST(test_closure_dynamic_var_not_captured) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_capture_minimal();

    // Use the namespace where *dyn* is interned so binding uses the same symbol key
    // as the closure body (*dyn* is unqualified in the fixture fn).
    evalstate_set_ns(g_test_eval_state, "test.closures.capture-minimal");

    // Dynamic vars are resolved at call-time:
    // (let [f (make-dyn-reader)] (binding [*dyn* 42] (f))) => 42
    CljObject *out = eval_string(
        "(let [f (make-dyn-reader)]"
        "  (binding [*dyn* 42]"
        "    (f)))",
        g_test_eval_state
    );
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(is_fixnum(out));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(out));
}

