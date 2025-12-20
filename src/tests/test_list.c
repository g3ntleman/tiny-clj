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
    // Clojure behavior: when element exists (even if nil), default is NOT returned
    CljObject *nil_with_default = NULL;
    TRY {
        nil_with_default = eval_string("(nth '(1 nil 3) 1 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth should not throw exception for nil element with default");
    } END_TRY
    // This should be NULL (nil element), not :default
    TEST_ASSERT_NULL_MESSAGE(nil_with_default,
        "nth should return NULL for nil element, not return default");

    // Out-of-bounds with default should return default (not throw exception)
    // (nth '(1 2 3) 5 :default) should return :default
    // Clojure behavior: out-of-bounds returns not-found value when provided
    CljObject *out_with_default = NULL;
    TRY {
        out_with_default = eval_string("(nth '(1 2 3) 5 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(out_with_default, "nth should return :default for out-of-bounds");
    TEST_ASSERT_TRUE_MESSAGE(TAG(out_with_default) == CLJ_SYMBOL && IS_KEYWORD(out_with_default),
        "nth should return :default keyword for out-of-bounds");
}

// Tests for LIST_FOR_EACH macro

TEST(test_list_for_each_basic) {
    // Create list (1 2 3)
    CljList *l3 = make_list(fixnum(3), NULL);
    CljList *l2 = make_list(fixnum(2), l3);
    CljList *list = make_list(fixnum(1), l2);
    AUTORELEASE((CljObject*)list);
    
    int sum = 0;
    int count = 0;
    LIST_FOR_EACH(list, elem) {
        sum += as_fixnum(elem);
        count++;
    }
    
    TEST_ASSERT_EQUAL_INT(6, sum);
    TEST_ASSERT_EQUAL_INT(3, count);
}

TEST(test_list_for_each_empty) {
    CljList *list = NULL;
    
    int count = 0;
    LIST_FOR_EACH(list, elem) {
        (void)elem;
        count++;
    }
    
    TEST_ASSERT_EQUAL_INT(0, count);
}

TEST(test_list_for_each_single) {
    CljList *list = make_list(fixnum(42), NULL);
    AUTORELEASE((CljObject*)list);
    
    int sum = 0;
    int count = 0;
    LIST_FOR_EACH(list, elem) {
        sum += as_fixnum(elem);
        count++;
    }
    
    TEST_ASSERT_EQUAL_INT(42, sum);
    TEST_ASSERT_EQUAL_INT(1, count);
}

TEST(test_list_for_each_rest) {
    // Create list (op 1 2 3) and iterate over REST (1 2 3)
    CljList *l3 = make_list(fixnum(3), NULL);
    CljList *l2 = make_list(fixnum(2), l3);
    CljList *l1 = make_list(fixnum(1), l2);
    CljList *list = make_list(SYM_PLUS, l1);
    AUTORELEASE((CljObject*)list);
    
    int sum = 0;
    int count = 0;
    LIST_FOR_EACH(LIST_REST(list), elem) {
        sum += as_fixnum(elem);
        count++;
    }
    
    TEST_ASSERT_EQUAL_INT(6, sum);
    TEST_ASSERT_EQUAL_INT(3, count);
}

TEST(test_list_for_each_find) {
    // Test finding an element (like MAP_FOR_EACH usage)
    CljList *l3 = make_list(fixnum(3), NULL);
    CljList *l2 = make_list(fixnum(2), l3);
    CljList *list = make_list(fixnum(1), l2);
    AUTORELEASE((CljObject*)list);
    
    int found = -1;
    LIST_FOR_EACH(list, elem) {
        if (as_fixnum(elem) == 2) {
            found = as_fixnum(elem);
            break;  // Skip remaining comparisons (like MAP_FOR_EACH)
        }
    }
    
    TEST_ASSERT_EQUAL_INT(2, found);
}

TEST(test_list_for_each_with_nil_in_middle) {
    // Create list (1 nil 3) - nil in the middle
    // NULL means nil in tiny-clj, so we use NULL as the element
    CljList *l3 = make_list(fixnum(3), NULL);
    CljList *l2 = make_list(NULL, l3);  // nil element in the middle
    CljList *list = make_list(fixnum(1), l2);
    AUTORELEASE((CljObject*)list);
    
    int count = 0;
    int nil_count = 0;
    int sum = 0;
    LIST_FOR_EACH(list, elem) {
        count++;
        if (elem == NULL) {
            nil_count++;
        } else {
            sum += as_fixnum(elem);
        }
    }
    
    // Should iterate over all 3 elements: 1, nil, 3
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_INT(1, nil_count);  // One nil element
    TEST_ASSERT_EQUAL_INT(4, sum);  // 1 + 3 = 4
}

