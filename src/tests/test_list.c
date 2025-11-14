// List-spezifische Tests
#include "tests_common.h"
#include "../list.h"
#include "../types.h"
#include "../parser.h"
#include "../symbol.h"

// Tests for nth with lists - should distinguish between nil element and out-of-bounds


TEST(test_nth_list_nil_at_index_1) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // (nth '(1 nil 3) 1) should return nil (nil element exists at index 1)
    CljObject *nil_elem = NULL;
    TRY {
        nil_elem = eval_string("(nth '(1 nil 3) 1)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth should not throw exception for nil element");
    } END_TRY
    // nil is represented as NULL
    TEST_ASSERT_NULL_MESSAGE(nil_elem, 
        "nth should return NULL for nil element");
}

TEST(test_nth_list_empty_list_out_of_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // (nth '() 0) should throw exception (out-of-bounds)
    CljObject *empty_list = NULL;
    TRY {
        empty_list = eval_string("(nth '() 0)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
        // This is correct - empty list should throw exception
    } END_TRY
    TEST_ASSERT_NULL(empty_list);
}

TEST(test_nth_list_nil_at_index_0) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // (nth '(nil 2 3) 0) should return nil (nil element exists at index 0)
    CljObject *nil_first = NULL;
    TRY {
        nil_first = eval_string("(nth '(nil 2 3) 0)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth should not throw exception for nil element at index 0");
    } END_TRY
    TEST_ASSERT_NULL_MESSAGE(nil_first,
        "nth should return NULL for nil element at index 0");
}

TEST(test_nth_list_all_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // (nth '(nil nil nil) 1) should return nil (nil element exists at index 1)
    CljObject *all_nil = NULL;
    TRY {
        all_nil = eval_string("(nth '(nil nil nil) 1)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth should not throw exception for nil element");
    } END_TRY
    TEST_ASSERT_NULL_MESSAGE(all_nil,
        "nth should return NULL for nil element");
}

TEST(test_nth_list_out_of_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // Out-of-bounds should throw exception
    // (nth '(1 2 3) 5) should throw exception (out-of-bounds)
    CljObject *out_of_bounds = NULL;
    TRY {
        out_of_bounds = eval_string("(nth '(1 2 3) 5)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
        // This is correct - out-of-bounds should throw exception
    } END_TRY
    TEST_ASSERT_NULL(out_of_bounds);
}

TEST(test_nth_list_nil_with_default_problem) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // (nth '(1 nil 3) 1 :default) should return nil (nil element exists, default is NOT returned)
    // This is Clojure behavior: when element is nil, default is NOT returned
    CljObject *nil_with_default = NULL;
    TRY {
        nil_with_default = eval_string("(nth '(1 nil 3) 1 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth should not throw exception for nil element with default");
    } END_TRY
    // This should be NULL (nil element), not :default
    TEST_ASSERT_NULL_MESSAGE(nil_with_default,
        "nth should return NULL for nil element, not return default");

    // Out-of-bounds with default should throw exception (not return default)
    // (nth '(1 2 3) 5 :default) should throw exception (out-of-bounds)
    // Clojure behavior: out-of-bounds throws exception, even with default
    CljObject *out_with_default = NULL;
    TRY {
        out_with_default = eval_string("(nth '(1 2 3) 5 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
        // This is correct - out-of-bounds should throw exception, even with default
    } END_TRY
    TEST_ASSERT_NULL(out_with_default);
}

