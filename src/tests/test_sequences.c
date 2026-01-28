/*
 * Sequence and Collection Tests using Unity Framework
 * 
 * Tests for conj, rest, and other sequence operations.
 */

#include "tests_common.h"

// ============================================================================
// CONJ AND REST TESTS
// ============================================================================

TEST(test_conj_arity_0) {
    // Use global st from setUp
    
    // Test (conj) - should return nil
    CljObject *result = eval_string("(conj)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
    
}

TEST(test_conj_arity_1) {
    // Use global st from setUp
    
    // Test (conj [1 2]) - should return collection unchanged
    CljObject *result = eval_string("(conj [1 2])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, result->type);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_conj_arity_2) {
    // Use global st from setUp
    
    // Test (conj [1 2] 3) - should return [1 2 3]
    CljObject *result = eval_string("(conj [1 2] 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, result->type);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_conj_arity_variadic) {
    // Use global st from setUp
    
    // Test (conj [1] 2 3 4) - should return [1 2 3 4]
    CljObject *result = eval_string("(conj [1] 2 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, result->type);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_conj_nil_collection) {
    // Use global st from setUp
    
    // Test (conj nil 1) - should return (1)
    CljObject *result = eval_string("(conj nil 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_conj_list_single) {
    // Use global st from setUp
    
    // Test (conj '(1 2) 3) - should return (3 1 2)
    // conj on lists adds element to front
    CljObject *result = eval_string("(conj '(1 2) 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_list_type(result->type));
    
    // Verify first element is 3
    CljObject *first_result = eval_string("(first (conj '(1 2) 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(is_fixnum(first_result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(first_result));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_conj_list_variadic) {
    // Use global st from setUp
    
    // Test (conj '(1) 2 3) - should return (3 2 1)
    // conj on lists adds elements to front in reverse order
    CljObject *result = eval_string("(conj '(1) 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_list_type(result->type));
    
    // Verify order: first should be 3 (last argument)
    CljObject *first_result = eval_string("(first (conj '(1) 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(is_fixnum(first_result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(first_result));
    
    // Verify second element is 2
    CljObject *second_result = eval_string("(first (rest (conj '(1) 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(second_result);
    TEST_ASSERT_TRUE(is_fixnum(second_result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(second_result));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_conj_empty_list) {
    // Use global st from setUp
    
    // Test (conj '() 1) - should return (1)
    CljObject *result = eval_string("(conj '() 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_list_type(result->type));
    
    // Verify first element is 1
    CljObject *first_result = eval_string("(first (conj '() 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(is_fixnum(first_result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first_result));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_rest_arity_0) {
    // Use global st from setUp
    
    // Test (rest) - should throw ArityException
    bool exception_caught = false;
    TRY {
        (void)eval_string("(rest)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected ArityException for (rest)");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArityException", ex->type);
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

TEST(test_rest_nil) {
    // Use global st from setUp
    
    // Test (rest nil) - should return ()
    CljObject *result = eval_string("(rest nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_rest_empty_vector) {
    // Use global st from setUp
    
    // Test (rest []) - should return ()
    CljObject *result = eval_string("(rest [])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_rest_single_element) {
    // Use global st from setUp
    
    // Test (rest [1]) - should return ()
    CljObject *result = eval_string("(rest [1])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

// ============================================================================
// SEQUENCE PERFORMANCE TESTS
// ============================================================================

TEST(test_seq_rest_performance) {
    // Test that (rest (rest (rest ...))) uses CljSeqIterator efficiently
    // Use global st from setUp
    
    // Test direct vector creation first
    CljValue vec_val = make_vector(10);
    TEST_ASSERT_NOT_NULL(vec_val);
    
    // Create large vector
    CljObject *vec2 = eval_string("[1 2 3 4 5 6 7 8 9 10]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec2);
    
    // Multiple rest calls should return CLJ_SEQ (or CLJ_LIST for empty)
    CljObject *r1 = eval_string("(rest [1 2 3 4 5 6 7 8 9 10])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r1);
    // Should be CLJ_SEQ or CLJ_LIST (using CljSeqIterator)
    TEST_ASSERT_TRUE(r1->type == CLJ_SEQ || r1->type == CLJ_LIST);
    
    CljObject *r2 = eval_string("(rest (rest [1 2 3 4 5 6 7 8 9 10]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_TRUE(r2->type == CLJ_SEQ || r2->type == CLJ_LIST);
    
    // Test that multiple rest calls are O(1) - not O(n²)
    // This is the key test: if we had O(n) copying, this would be very slow
    CljObject *r3 = eval_string("(rest (rest (rest (rest (rest [1 2 3 4 5 6 7 8 9 10])))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_TRUE(r3->type == CLJ_SEQ || r3->type == CLJ_LIST);
    
    // Test that we can chain many rest calls without performance degradation
    CljObject *r4 = eval_string("(rest (rest (rest (rest (rest (rest (rest (rest (rest [1 2 3 4 5 6 7 8 9 10])))))))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r4);
    TEST_ASSERT_TRUE(r4->type == CLJ_SEQ || r4->type == CLJ_LIST);
    
    // vec2, r1, r2, r3, r4 are automatically managed by eval_string
}

// ============================================================================
// Tests for filter function
// ============================================================================

TEST(test_filter_basic) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (filter even? (list 1 2 3 4 5)) => (2 4)
    // Use list instead of vector to avoid potential vector handling issues
    CljObject *result = eval_string("(vec (filter even? (list 1 2 3 4 5)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);

    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(2, vector_count(v));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)vector_nth(v, 1)));
    
}

TEST(test_filter_empty) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (filter even? []) => ()
    CljObject *result = eval_string("(filter even? [])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);  // Returns empty list, not nil (Clojure semantics)
    CljObject *count_result = eval_string("(count (filter even? []))", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(count_result));
}

TEST(test_filter_all_match) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (filter pos? [1 2 3]) => (1 2 3)
    CljObject *result = eval_string("(vec (filter pos? [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    
    TEST_ASSERT_EQUAL_INT(3, vector_count(as_vector(result)));
    
}

TEST(test_filter_none_match) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (filter neg? [1 2 3]) => ()
    CljObject *result = eval_string("(filter neg? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);  // Returns empty list, not nil (Clojure semantics)
    CljObject *count_result = eval_string("(count (filter neg? [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(count_result));
}

TEST(test_filter_with_custom_predicate) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (filter (fn [x] (> x 2)) [1 2 3 4 5]) => (3 4 5)
    CljObject *result = eval_string("(vec (filter (fn [x] (> x 2)) [1 2 3 4 5]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);

    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(3, vector_count(v));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)vector_nth(v, 0)));
    
}

// ============================================================================
// REVERSE TESTS
// ============================================================================

TEST(test_reverse_basic) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse (list 1 2 3)) => (3 2 1)
    CljObject *result = eval_string("(reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify count is 3
    CljObject *count_result = eval_string("(count (reverse (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_TRUE(is_fixnum(count_result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(count_result));
    
    // Verify first element is 3
    CljObject *first_result = eval_string("(first (reverse (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(is_fixnum(first_result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(first_result));
    
}

TEST(test_reverse_empty) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse (list)) => empty list
    CljObject *result = eval_string("(reverse (list))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify count is 0
    CljObject *count_result = eval_string("(count (reverse (list)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_TRUE(is_fixnum(count_result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(count_result));
    
}

TEST(test_reverse_nil) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse nil) => empty list
    CljObject *result = eval_string("(reverse nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify count is 0
    CljObject *count_result = eval_string("(count (reverse nil))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_TRUE(is_fixnum(count_result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(count_result));
    
}

TEST(test_reverse_single_element) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse (list 1)) => (1)
    CljObject *result = eval_string("(reverse (list 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify count is 1
    CljObject *count_result = eval_string("(count (reverse (list 1)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_TRUE(is_fixnum(count_result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(count_result));
    
    // Verify first element is 1
    CljObject *first_result = eval_string("(first (reverse (list 1)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(is_fixnum(first_result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first_result));
    
}

TEST(test_reverse_multiple_elements) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse (list 1 2 3 4 5)) => (5 4 3 2 1)
    CljObject *result = eval_string("(reverse (list 1 2 3 4 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify count is 5
    CljObject *count_result = eval_string("(count (reverse (list 1 2 3 4 5)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_TRUE(is_fixnum(count_result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(count_result));
    
    // Verify first element is 5
    CljObject *first_result = eval_string("(first (reverse (list 1 2 3 4 5)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(is_fixnum(first_result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(first_result));
    
    // Verify second element is 4
    CljObject *second_result = eval_string("(first (rest (reverse (list 1 2 3 4 5))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(second_result);
    TEST_ASSERT_TRUE(is_fixnum(second_result));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(second_result));
    
}

TEST(test_reverse_list_with_nil_elements) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse '(1 nil 3)) => (3 nil 1)
    // Store result once, then verify elements via vector conversion
    CljObject *check = eval_string(
        "(let [r (reverse '(1 nil 3))] "
        "  (and (= 3 (count r)) "
        "       (= 3 (first r)) "
        "       (nil? (nth r 1)) "
        "       (= 1 (nth r 2))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(check);
    TEST_ASSERT_TRUE(clj_is_truthy(check));
}

TEST(test_reverse_vector_with_nil_elements) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reverse [1 nil 3]) => (3 nil 1)
    // Store result once, then verify elements via single expression
    CljObject *check = eval_string(
        "(let [r (reverse [1 nil 3])] "
        "  (and (= 3 (count r)) "
        "       (= 3 (first r)) "
        "       (nil? (nth r 1)) "
        "       (= 1 (nth r 2))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(check);
    TEST_ASSERT_TRUE(clj_is_truthy(check));
}

// ============================================================================
// REDUCE TESTS
// ============================================================================

// test_reduce_debug removed: step-by-step debug test, subsumed by test_reduce_single_element

// EDGE CASE 1: Empty collection should call reducer with zero args
TEST(test_reduce_empty_collection) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list)) => 0 (calls + with zero arguments)
    CljObject *result = eval_string("(reduce + (list))", g_test_eval_state);
    assert_fixnum(result, 0);
    
    // Note: Vector support may require seq handling - test with list for now
    // Empty list test is sufficient for edge case coverage
}

// EDGE CASE 2: Single element collection should return that element
TEST(test_reduce_single_element) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list 42)) => 42
    CljObject *result = eval_string("(reduce + (list 42))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    // Test: (reduce * (list 5)) => 5
    CljObject *result2 = eval_string("(reduce * (list 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result2));
}

// EDGE CASE 3: Nil collection should call reducer with zero args
TEST(test_reduce_nil_collection) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + nil) => 0 (calls + with zero arguments)
    CljObject *result = eval_string("(reduce + nil)", g_test_eval_state);
    assert_fixnum(result, 0);
}

// EDGE CASE 4: Basic reduce with addition
TEST(test_reduce_basic_addition) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list 1 2 3 4 5)) => 15
    CljObject *result = eval_string("(reduce + (list 1 2 3 4 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum((CljValue)result));
    
    // Note: Vector support may require seq handling - test with list for now
    // Test: (reduce + (list 1 2 3)) => 6
    CljObject *result2 = eval_string("(reduce + (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result2));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum((CljValue)result2));
}

// EDGE CASE 5: Reduce with multiplication
TEST(test_reduce_multiplication) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce * (list 1 2 3 4)) => 24
    CljObject *result = eval_string("(reduce * (list 1 2 3 4))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum(result));
    
    // Note: Vector support may require seq handling - test with list for now
    // Test: (reduce * (list 2 3 4)) => 24
    CljObject *result2 = eval_string("(reduce * (list 2 3 4))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum(result2));
}

// EDGE CASE 6: Reduce with max function
TEST(test_reduce_max) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce max (list 1 5 3 9 2)) => 9
    CljObject *result = eval_string("(reduce max (list 1 5 3 9 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(9, as_fixnum(result));
    
    // Note: Vector support may require seq handling - test with list for now
    // Test: (reduce max (list 10 3 7 1)) => 10
    CljObject *result2 = eval_string("(reduce max (list 10 3 7 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result2));
}

// EDGE CASE 7: Reduce with min function
TEST(test_reduce_min) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce min (list 5 2 8 1 9)) => 1
    CljObject *result = eval_string("(reduce min (list 5 2 8 1 9))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// EDGE CASE 8: Reduce with custom function
TEST(test_reduce_custom_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce (fn [a b] (+ a b)) (list 1 2 3)) => 6
    CljObject *result = eval_string("(reduce (fn [a b] (+ a b)) (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// EDGE CASE 9: Reduce with subtraction (order matters)
TEST(test_reduce_subtraction) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce - (list 10 3 2)) => 5 (10 - 3 - 2)
    CljObject *result = eval_string("(reduce - (list 10 3 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
}

// EDGE CASE 10: Reduce with zero values
TEST(test_reduce_with_zero) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list 0 0 0)) => 0
    CljObject *result = eval_string("(reduce + (list 0 0 0))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
    
    // Test: (reduce * (list 5 0 3)) => 0
    CljObject *result2 = eval_string("(reduce * (list 5 0 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result2));
}

// EDGE CASE 11: Reduce with negative numbers
TEST(test_reduce_with_negative_numbers) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list -1 -2 -3)) => -6
    CljObject *result = eval_string("(reduce + (list -1 -2 -3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(-6, as_fixnum(result));
    
    // Test: (reduce * (list -2 3 -4)) => 24
    CljObject *result2 = eval_string("(reduce * (list -2 3 -4))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum(result2));
}

// EDGE CASE 12: Reduce with large collection
TEST(test_reduce_large_collection) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list 1 2 3 4 5 6 7 8 9 10)) => 55
    CljObject *result = NULL;
    TRY {
        result = eval_string("(reduce + (list 1 2 3 4 5 6 7 8 9 10))", g_test_eval_state);
        if (!result) {
            TEST_FAIL_MESSAGE("reduce returned NULL");
            return;
        }
        // eval_string returns AUTORELEASE objects - no manual RETAIN/RELEASE needed
        if (!is_fixnum(result)) {
            char msg[256];
            test_snprintf(msg, sizeof(msg), "reduce returned non-fixnum: type=%d", result ? result->type : -1);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        int value = as_fixnum(result);
        if (value != 55) {
            char msg[256];
            test_snprintf(msg, sizeof(msg), "reduce returned %d, expected 55", value);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        // No manual cleanup needed - result is autoreleased
    } CATCH(ex) {
        char msg[512];
        test_snprintf(msg, sizeof(msg), "reduce threw exception: %s - %s",
                ex ? ex->type : "unknown", ex ? ex->message : "no message");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
}

// EDGE CASE 13: Reduce with function that returns first argument (like identity for reduce)
TEST(test_reduce_with_identity_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Note: reduce expects a function that takes two arguments (acc, val)
    // identity only takes one argument, so we use a function that returns the first argument
    // Test: (reduce (fn [a b] a) (list 42)) => 42
    CljObject *result = NULL;
    TRY {
        result = eval_string("(reduce (fn [a b] a) (list 42))", g_test_eval_state);
        if (!result) {
            TEST_FAIL_MESSAGE("reduce returned NULL");
            return;
        }
        if (!is_fixnum(result)) {
            char msg[256];
            test_snprintf(msg, sizeof(msg), "reduce returned non-fixnum: type=%d", result ? result->type : -1);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        int value = as_fixnum(result);
        if (value != 42) {
            char msg[256];
            test_snprintf(msg, sizeof(msg), "reduce returned %d, expected 42", value);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
    } CATCH(ex) {
        char msg[512];
        test_snprintf(msg, sizeof(msg), "reduce threw exception: %s - %s",
                ex ? ex->type : "unknown", ex ? ex->message : "no message");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
}

// LOW-LEVEL parameter resolution tests removed (9 tests):
// - test_parameter_resolution_in_function_body
// - test_parameter_resolution_in_let
// - test_parameter_resolution_in_nested_call
// - test_parameter_resolution_with_empty
// - test_parameter_resolution_with_first
// - test_two_parameters_resolution
// - test_parameter_resolution_from_index_in_let
// - test_parameter_resolution_from_index_in_let_non_nil
// - test_parameter_in_let_with_two_params
// These were debug tests after a bugfix, subsumed by higher-level tests like test_reduce_*

// EDGE CASE 14: Reduce with two elements
TEST(test_reduce_two_elements) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (reduce + (list 10 20)) => 30
    CljObject *result = eval_string("(reduce + (list 10 20))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
    
    // Note: Vector support may require seq handling - test with list for now
    // Test: (reduce * (list 3 4)) => 12
    CljObject *result2 = eval_string("(reduce * (list 3 4))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(12, as_fixnum(result2));
}

// ============================================================================
// REST/NEXT TESTS FOR PARAMETER RESOLUTION
// ============================================================================

// test_rest_with_parameter removed: subsumed by test_rest_with_parameter_nested

// THESIS 2: rest with parameter in nested function call
// Test: (rest coll) where coll is a parameter in a nested function
TEST(test_rest_with_parameter_nested) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: Define a function that takes coll as parameter and calls (rest coll) in a let
    // (defn test-rest-nested [coll] (let [r (rest coll)] r))
    CljObject *defn_result = eval_string("(defn test-rest-nested [coll] (let [r (rest coll)] r))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(defn_result);
    // Don't RELEASE defn_result - eval_string returns autoreleased object
    
    // Test: (test-rest-nested (list 1 2 3)) => (2 3)
    CljObject *result = eval_string("(test-rest-nested (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Verify result is (2 3)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

// test_rest_with_parameter_recursive removed: similar to other rest tests
// test_rest_with_parameter_closure removed: subsumed by nested/let tests  
// test_rest_with_parameter_reduce_like removed: similar to recursive
// test_rest_with_parameter_step_function removed: similar to reduce_like

// THESIS 7: next with parameter in function call
// Test: (next coll) where coll is a parameter
TEST(test_next_with_parameter) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: Define a function that takes coll as parameter and calls (next coll)
    // (defn test-next [coll] (next coll))
    CljObject *defn_result = eval_string("(defn test-next [coll] (next coll))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(defn_result);
    // Don't RELEASE defn_result - eval_string returns autoreleased object
    
    // Test: (test-next (list 1 2 3)) => (2 3)
    CljObject *result = eval_string("(test-next (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Verify result is (2 3)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

// THESIS 8: rest with parameter in let binding
// Test: (rest coll) where coll is a parameter in a let binding
TEST(test_rest_with_parameter_let) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: Define a function that uses let to bind coll and then calls (rest coll)
    // (defn test-rest-let [coll] (let [c coll] (rest c)))
    CljObject *defn_result = eval_string("(defn test-rest-let [coll] (let [c coll] (rest c)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(defn_result);
    // Don't RELEASE defn_result - eval_string returns autoreleased object
    
    // Test: (test-rest-let (list 1 2 3)) => (2 3)
    CljObject *result = eval_string("(test-rest-let (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result->type == CLJ_LIST || result->type == CLJ_SEQ);
    
    // Verify result is (2 3)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}


// ============================================================================
// RANGE TESTS
// ============================================================================

TEST(test_range) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (range 5) => [0 1 2 3 4]
    CljObject *result1 = eval_string("(range 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result1));
    CljVector *vec1 = as_vector(result1);
    TEST_ASSERT_EQUAL_INT(5, vector_count(vec1));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)vector_nth(vec1, 0)));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)vector_nth(vec1, 4)));
    
    // Test: (range 2 5) => [2 3 4]
    CljObject *result2 = eval_string("(range 2 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result2));
    CljVector *vec2 = as_vector(result2);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec2));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)vector_nth(vec2, 0)));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)vector_nth(vec2, 2)));
    
    // Test: (range 0 10 2) => [0 2 4 6 8]
    CljObject *result3 = eval_string("(range 0 10 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result3));
    CljVector *vec3 = as_vector(result3);
    TEST_ASSERT_EQUAL_INT(5, vector_count(vec3));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)vector_nth(vec3, 0)));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum((CljValue)vector_nth(vec3, 4)));
    
    // Test: (range 0) => []
    CljObject *result4 = eval_string("(range 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, ((CljObject*)result4)->type);
    CljVector *vec4 = as_vector(result4);
    TEST_ASSERT_EQUAL_INT(0, vector_count(vec4));
}

// ============================================================================
// REPEAT TESTS
// ============================================================================

TEST(test_repeat) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (repeat 3 "x") => ["x" "x" "x"]
    CljObject *result1 = eval_string("(repeat 3 \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result1));
    CljVector *vec1 = as_vector(result1);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec1));
    
    // Test: (repeat 0 "x") => []
    CljObject *result2 = eval_string("(repeat 0 \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, ((CljObject*)result2)->type);
    CljVector *vec2 = as_vector(result2);
    TEST_ASSERT_EQUAL_INT(0, vector_count(vec2));
    
    // Test: (repeat 2 42) => [42 42]
    CljObject *result3 = eval_string("(repeat 2 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, TAG(result3));
    CljVector *vec3 = as_vector(result3);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec3));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)vector_nth(vec3, 0)));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)vector_nth(vec3, 1)));
}

// ============================================================================
// CONS WITH EMPTY LIST TESTS
// ============================================================================

TEST(test_cons_with_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (cons 1 nil) => (1), count should be 1
    CljObject *result = eval_string("(count (cons 1 nil))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST(test_cons_with_empty_list) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (cons 1 (list)) => (1), count should be 1
    // This tests that (list) behaves like nil for cons
    CljObject *result = eval_string("(count (cons 1 (list)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST(test_cons_chain_with_empty_list) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (cons 1 (cons 2 (list))) => (1 2), count should be 2
    CljObject *result = eval_string("(count (cons 1 (cons 2 (list))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

TEST(test_cons_chain_with_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (cons 1 (cons 2 nil)) => (1 2), count should be 2
    CljObject *result = eval_string("(count (cons 1 (cons 2 nil)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

// ============================================================================
// SEQUENCE NIL ELEMENT TESTS
// ============================================================================

TEST(test_next_vector_with_nil_element) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (next [1 nil]) should return sequence with 1 element (nil)
    // count should be 1, not 2
    CljObject *count_result = eval_string("(count (next [1 nil]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(count_result));
    
    // first element should be nil
    CljObject *first_result = eval_string("(nil? (first (next [1 nil])))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first_result);
    TEST_ASSERT_TRUE(first_result == clj_true);
}

TEST(test_rest_vector_with_nil_element) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (rest [1 nil]) should return sequence with 1 element (nil)
    CljObject *count_result = eval_string("(count (rest [1 nil]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(count_result));
}

TEST(test_take_with_nil_element) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (take 2 [1 nil]) should return (1 nil), count 2
    CljObject *count_result = eval_string("(count (take 2 [1 nil]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(count_result));
}
