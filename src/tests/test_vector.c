// Vector-spezifische Tests
#include "tests_common.h"

TEST(test_vector_builtin_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // (vector) => []
    CljObject *v0 = eval_string("(vector)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v0);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v0->type);
    CljObject *c0 = eval_string("(count (vector))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(c0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c0));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)c0));

    // (vector 1 2 3) => [1 2 3]
    CljObject *v3 = eval_string("(vector 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v3->type);
    CljObject *n0 = eval_string("(nth (vector 1 2 3) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n0));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)n0));
    CljObject *n2 = eval_string("(nth (vector 1 2 3) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)n2));

}

TEST(test_nth_with_default_and_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // In-bounds ohne Default
    CljObject *x = eval_string("(nth [10 20 30] 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)x));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)x));

    // Out-of-bounds mit Default
    CljObject *d = eval_string("(nth [10 20 30] 5 :na)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_TRUE(d && TAG(d) == CLJ_SYMBOL);

}

TEST(test_nth_with_lists) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Basic list access
    // (nth '(10 20 30) 0) => 10
    CljObject *n0 = eval_string("(nth '(10 20 30) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n0));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)n0));

    // (nth '(10 20 30) 1) => 20
    CljObject *n1 = eval_string("(nth '(10 20 30) 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n1));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)n1));

    // (nth '(10 20 30) 2) => 30
    CljObject *n2 = eval_string("(nth '(10 20 30) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n2));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)n2));

    // Out-of-bounds without default => nil
    CljObject *out = eval_string("(nth '(10 20 30) 5)", g_test_eval_state);
    TEST_ASSERT_NULL(out);

    // Out-of-bounds with default => default value
    CljObject *out_default = eval_string("(nth '(10 20 30) 5 :not-found)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out_default);
    TEST_ASSERT_TRUE(TAG(out_default) == CLJ_SYMBOL);

    // Empty list => nil
    CljObject *empty = eval_string("(nth '() 0)", g_test_eval_state);
    TEST_ASSERT_NULL(empty);

    // Empty list with default => default value
    CljObject *empty_default = eval_string("(nth '() 0 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(empty_default);
    TEST_ASSERT_TRUE(TAG(empty_default) == CLJ_SYMBOL);

    // List with nil elements
    // (nth '(1 nil 3) 1) => nil
    CljObject *nil_elem = eval_string("(nth '(1 nil 3) 1)", g_test_eval_state);
    if (nil_elem) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_elem), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(nil_elem);  // NULL is correct for nil
    }

    // (nth '(nil 2 nil) 0) => nil
    CljObject *nil_first = eval_string("(nth '(nil 2 nil) 0)", g_test_eval_state);
    if (nil_first) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_first), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(nil_first);  // NULL is correct for nil
    }

    // (nth '(nil 2 nil) 1) => 2
    CljObject *nil_second = eval_string("(nth '(nil 2 nil) 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(nil_second);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nil_second));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nil_second));

}

TEST(test_nth_with_sequences) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // nth with rest (creates a sequence)
    // (nth (rest '(10 20 30)) 0) => 20
    CljObject *rest_nth = eval_string("(nth (rest '(10 20 30)) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rest_nth);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)rest_nth));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)rest_nth));

    // (nth (rest '(10 20 30)) 1) => 30
    CljObject *rest_nth2 = eval_string("(nth (rest '(10 20 30)) 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(rest_nth2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)rest_nth2));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)rest_nth2));

    // nth with next (creates a sequence)
    // (nth (next '(10 20 30)) 0) => 20
    CljObject *next_nth = eval_string("(nth (next '(10 20 30)) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(next_nth);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)next_nth));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)next_nth));

    // nth with subvec (creates a vector, but tests sequence path)
    // (nth (subvec [1 2 3 4] 1 3) 0) => 2
    CljObject *subvec_nth = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(subvec_nth);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)subvec_nth));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)subvec_nth));

    // Out-of-bounds with sequence
    CljObject *seq_out = eval_string("(nth (rest '(10 20)) 5)", g_test_eval_state);
    TEST_ASSERT_NULL(seq_out);

    // Out-of-bounds with sequence and default
    CljObject *seq_out_default = eval_string("(nth (rest '(10 20)) 5 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(seq_out_default);
    TEST_ASSERT_TRUE(TAG(seq_out_default) == CLJ_SYMBOL);

}

TEST(test_nth_edge_cases) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Negative index => nil
    CljObject *neg = eval_string("(nth [10 20 30] -1)", g_test_eval_state);
    TEST_ASSERT_NULL(neg);

    // Negative index with default => default value
    CljObject *neg_default = eval_string("(nth [10 20 30] -1 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(neg_default);
    TEST_ASSERT_TRUE(TAG(neg_default) == CLJ_SYMBOL);

    // nil collection => nil
    CljObject *nil_coll = eval_string("(nth nil 0)", g_test_eval_state);
    TEST_ASSERT_NULL(nil_coll);

    // nil collection with default => default value
    CljObject *nil_coll_default = eval_string("(nth nil 0 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(nil_coll_default);
    TEST_ASSERT_TRUE(TAG(nil_coll_default) == CLJ_SYMBOL);

    // First element (index 0)
    CljObject *first = eval_string("(nth [10 20 30] 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)first));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)first));

    // Last element
    CljObject *last = eval_string("(nth [10 20 30] 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)last));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)last));

    // Just out of bounds
    CljObject *just_out = eval_string("(nth [10 20 30] 3)", g_test_eval_state);
    TEST_ASSERT_NULL(just_out);

    // Just out of bounds with default
    CljObject *just_out_default = eval_string("(nth [10 20 30] 3 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(just_out_default);
    TEST_ASSERT_TRUE(TAG(just_out_default) == CLJ_SYMBOL);

    // Large index
    CljObject *large = eval_string("(nth [10 20 30] 1000)", g_test_eval_state);
    TEST_ASSERT_NULL(large);

    // Large index with default
    CljObject *large_default = eval_string("(nth [10 20 30] 1000 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(large_default);
    TEST_ASSERT_TRUE(TAG(large_default) == CLJ_SYMBOL);

    // Single element vector
    CljObject *single = eval_string("(nth [42] 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(single);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)single));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)single));

    // Single element list
    CljObject *single_list = eval_string("(nth '(42) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(single_list);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)single_list));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)single_list));

}

TEST(test_nth_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Vector with nil elements
    // (nth [1 nil 3] 1) => nil
    CljObject *vec_nil = eval_string("(nth [1 nil 3] 1)", g_test_eval_state);
    // nil is represented as NULL - check both NULL and TAG for robustness
    if (vec_nil) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(vec_nil), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(vec_nil);  // NULL is correct for nil
    }

    // (nth [nil 2 nil] 0) => nil
    CljObject *vec_nil_first = eval_string("(nth [nil 2 nil] 0)", g_test_eval_state);
    if (vec_nil_first) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(vec_nil_first), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(vec_nil_first);  // NULL is correct for nil
    }

    // (nth [nil 2 nil] 1) => 2
    CljObject *vec_nil_second = eval_string("(nth [nil 2 nil] 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec_nil_second);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)vec_nil_second));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)vec_nil_second));

    // (nth [nil 2 nil] 2) => nil
    CljObject *vec_nil_third = eval_string("(nth [nil 2 nil] 2)", g_test_eval_state);
    if (vec_nil_third) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(vec_nil_third), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(vec_nil_third);  // NULL is correct for nil
    }

    // List with nil elements (already tested in test_nth_with_lists, but adding more)
    // (nth '(nil nil nil) 1) => nil
    CljObject *list_all_nil = eval_string("(nth '(nil nil nil) 1)", g_test_eval_state);
    if (list_all_nil) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(list_all_nil), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(list_all_nil);  // NULL is correct for nil
    }

    // Distinguish between nil element and out-of-bounds
    // (nth [1 nil 3] 1) => nil (nil element)
    CljObject *nil_at_index = eval_string("(nth [1 nil 3] 1)", g_test_eval_state);
    if (nil_at_index) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_at_index), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(nil_at_index);  // NULL is correct for nil
    }

    // (nth [1 nil 3] 3) => nil (out-of-bounds)
    CljObject *out_of_bounds = eval_string("(nth [1 nil 3] 3)", g_test_eval_state);
    TEST_ASSERT_NULL(out_of_bounds);  // Out-of-bounds should return NULL

    // Using default to distinguish: nil element vs out-of-bounds
    // When element is nil, default is NOT returned (element exists, it's just nil)
    // This is Clojure behavior: (nth [1 nil 3] 1 :default) => nil (not :default)
    CljObject *nil_with_default = eval_string("(nth [1 nil 3] 1 :default)", g_test_eval_state);
    if (nil_with_default) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_with_default), "nth should return nil (NULL or CLJ_NIL) for nil element, not default");
    } else {
        TEST_ASSERT_NULL(nil_with_default);  // nil element, not default
    }

    // Out-of-bounds with default => default
    CljObject *out_with_default = eval_string("(nth [1 nil 3] 3 :default)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(out_with_default);
    TEST_ASSERT_TRUE(TAG(out_with_default) == CLJ_SYMBOL);

}

TEST(test_peek_and_pop_vector) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // peek
    CljObject *p1 = eval_string("(peek [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)p1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)p1));
    CljObject *p0 = eval_string("(peek [])", g_test_eval_state);
    TEST_ASSERT_NULL(p0);

    // pop
    CljObject *pop1 = eval_string("(pop [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(pop1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, pop1->type);
    CljObject *cnt = eval_string("(count (pop [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)cnt));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)cnt));

}

TEST(test_subvec_bounds_and_slices) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *s1 = eval_string("(subvec [1 2 3 4] 1 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, s1->type);
    CljObject *s1n0 = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s1n0));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s1n0));

    // start-only
    CljObject *s2c = eval_string("(count (subvec [1 2 3 4] 2))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s2c));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s2c));

}

TEST(test_subvec_edge_cases) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Normal cases
    // (subvec [1 2 3 4] 0 4) → [1 2 3 4] (complete vector)
    CljObject *full = eval_string("(subvec [1 2 3 4] 0 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(full);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, full->type);
    CljObject *full_count = eval_string("(count (subvec [1 2 3 4] 0 4))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)full_count));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)full_count));

    // (subvec [1 2 3 4] 0 1) → [1] (first element)
    CljObject *first = eval_string("(subvec [1 2 3 4] 0 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first);
    CljObject *first_val = eval_string("(nth (subvec [1 2 3 4] 0 1) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)first_val));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)first_val));

    // (subvec [1 2 3 4] 3 4) → [4] (last element)
    CljObject *last = eval_string("(subvec [1 2 3 4] 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(last);
    CljObject *last_val = eval_string("(nth (subvec [1 2 3 4] 3 4) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)last_val));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)last_val));

    // Edge cases
    // (subvec [] 0 0) → [] (empty vector)
    CljObject *empty = eval_string("(subvec [] 0 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, empty->type);
    CljObject *empty_count = eval_string("(count (subvec [] 0 0))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_count));

    // (subvec [1 2 3] 1 1) → [] (start == end, empty result)
    CljObject *empty_result = eval_string("(subvec [1 2 3] 1 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(empty_result);
    CljObject *empty_result_count = eval_string("(count (subvec [1 2 3] 1 1))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_result_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_result_count));

    // (subvec [1] 0 1) → [1] (single-element vector)
    CljObject *single = eval_string("(subvec [1] 0 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(single);
    CljObject *single_val = eval_string("(nth (subvec [1] 0 1) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)single_val));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)single_val));

    // (subvec [1 2 3] 0) → [1 2 3] (from start, end missing)
    CljObject *from_start = eval_string("(subvec [1 2 3] 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(from_start);
    CljObject *from_start_count = eval_string("(count (subvec [1 2 3] 0))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)from_start_count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)from_start_count));

    // (subvec [1 2 3] 0 3) → [1 2 3] (complete vector explicitly)
    CljObject *complete = eval_string("(subvec [1 2 3] 0 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(complete);
    CljObject *complete_count = eval_string("(count (subvec [1 2 3] 0 3))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)complete_count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)complete_count));

    // Nested/Integration
    // (nth (subvec [1 2 3 4] 1 3) 0) → 2 (subvec with nth)
    CljObject *nested_nth = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nested_nth));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nested_nth));

    // (count (subvec [1 2 3 4] 2)) → 2 (subvec with count)
    CljObject *nested_count = eval_string("(count (subvec [1 2 3 4] 2))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nested_count));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nested_count));

    // (subvec (subvec [1 2 3 4 5] 1 4) 0 2) → [2 3] (nested subvec)
    CljObject *nested_subvec = eval_string("(subvec (subvec [1 2 3 4 5] 1 4) 0 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(nested_subvec);
    CljObject *nested_subvec_val = eval_string("(nth (subvec (subvec [1 2 3 4 5] 1 4) 0 2) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nested_subvec_val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nested_subvec_val));

}

TEST(test_subvec_error_cases) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Index out of bounds: start < 0
    CljObject *result1 = NULL;
    TRY {
        result1 = eval_string("(subvec [1 2 3] -1 2)", g_test_eval_state);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result1);  // Should fail

    // Index out of bounds: end > count
    CljObject *result2 = NULL;
    TRY {
        result2 = eval_string("(subvec [1 2 3] 0 4)", g_test_eval_state);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result2);  // Should fail

    // Index out of bounds: start > end
    CljObject *result3 = NULL;
    TRY {
        result3 = eval_string("(subvec [1 2 3] 2 1)", g_test_eval_state);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result3);  // Should fail

    // Index out of bounds: start > count
    CljObject *result4 = NULL;
    TRY {
        result4 = eval_string("(subvec [1 2 3] 4 5)", g_test_eval_state);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result4);  // Should fail

    // Type error: nil as vector
    CljObject *result5 = NULL;
    TRY {
        result5 = eval_string("(subvec nil 0 1)", g_test_eval_state);
    } CATCH(ex) {
        // Expected: IllegalArgumentException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result5);  // Should fail

    // Type error: wrong type
    CljObject *result6 = NULL;
    TRY {
        result6 = eval_string("(subvec \"not-vector\" 0 1)", g_test_eval_state);
    } CATCH(ex) {
        // Expected: IllegalArgumentException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result6);  // Should fail

    // Arity error: only 1 argument
    TRY {
        CljObject *result7 = eval_string("(subvec [1 2 3])", g_test_eval_state);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("Expected ArityException to be thrown");
    } CATCH(ex) {
        // Expected: ArityException
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_ARITY, ex->type);
    } END_TRY

    // Arity error: 4 arguments
    TRY {
        CljObject *result8 = eval_string("(subvec [1 2 3] 0 1 2)", g_test_eval_state);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("Expected ArityException to be thrown");
    } CATCH(ex) {
        // Expected: ArityException
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_ARITY, ex->type);
    } END_TRY

}

TEST(test_vec_from_list_and_vector_id) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: vec converts list to vector
    // (vec '(1 2 3)) => [1 2 3]
    CljObject *v = eval_string("(vec '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v->type);
    CljObject *c = eval_string("(count (vec '(1 2 3)))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c));
    
    // Test: vec on vector is No-Op (returns same vector)
    // (vec [1 2 3]) => [1 2 3] (same object)
    CljObject *v1 = eval_string("(vec [1 2 3])", g_test_eval_state);
    CljObject *v2 = eval_string("(vec [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v1->type);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v2->type);
    // Note: They might be different objects (new evaluation), but same content
    CljObject *c1 = eval_string("(count (vec [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c1));
    
    // Test: vec on empty list => empty vector
    // (vec '()) => []
    CljObject *empty = eval_string("(vec '())", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, empty->type);
    CljObject *empty_count = eval_string("(count (vec '()))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_count));

}

TEST(test_vec_with_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: vec converts list with nil element to vector
    // (vec '(1 nil 3)) => [1 nil 3]
    CljObject *v = eval_string("(vec '(1 nil 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v->type);
    
    // Check count
    CljObject *c = eval_string("(count (vec '(1 nil 3)))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c));
    
    // Check first element (should be 1)
    CljObject *first = eval_string("(nth (vec '(1 nil 3)) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)first));
    
    // Check second element (should be nil)
    CljObject *second = eval_string("(nth (vec '(1 nil 3)) 1)", g_test_eval_state);
    // nil is represented as NULL - check both NULL and TAG for robustness
    if (second) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(second), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(second);  // NULL is correct for nil
    }
    
    // Check third element (should be 3)
    CljObject *third = eval_string("(nth (vec '(1 nil 3)) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)third));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)third));
    
    // Test: vec on vector with nil element (using list syntax since vector literal may not parse nil correctly)
    // (vec '(nil 2 nil)) => [nil 2 nil]
    CljObject *v2 = eval_string("(vec '(nil 2 nil))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v2->type);
    CljObject *c2 = eval_string("(count (vec '(nil 2 nil)))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c2));
    
    // Check first element (should be nil)
    CljObject *first2 = eval_string("(nth (vec '(nil 2 nil)) 0)", g_test_eval_state);
    // nil is represented as NULL - check both NULL and TAG for robustness
    if (first2) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(first2), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(first2);  // NULL is correct for nil
    }
    
    // Check second element (should be 2)
    CljObject *second2 = eval_string("(nth (vec '(nil 2 nil)) 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(second2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)second2));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)second2));
    
    // Check third element (should be nil)
    CljObject *third2 = eval_string("(nth (vec '(nil 2 nil)) 2)", g_test_eval_state);
    // nil is represented as NULL - check both NULL and TAG for robustness
    if (third2) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(third2), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NULL(third2);  // NULL is correct for nil
    }

}

// ============================================================================
// Tests for vector_conj() COW implementation
// ============================================================================

// Test that vector_conj uses in-place mutation when RC=1
TEST(test_vector_conj_cow_rc_one_inplace) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector* vec = make_vector(4, false);
        // base.rc is part of CljObject, access via cast
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // First conj should be in-place (RC=1, capacity allows)
        CljValue new_vec1 = (CljValue)vector_conj((CljPersistentVector*)vec, (ID)fixnum(10));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, new_vec1); // Same pointer!
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        TEST_ASSERT_EQUAL_INT(1, vector_count(vec));
        
        // Second conj should also be in-place
        CljValue new_vec2 = (CljValue)vector_conj((CljPersistentVector*)vec, (ID)fixnum(20));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, new_vec2); // Same pointer!
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // Verify entries
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(vec, 1)));
    });
}

// Test that vector_conj uses Copy-on-Write when RC>1
TEST(test_vector_conj_cow_rc_greater_one) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector* vec = make_vector(4, false);
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // Add some entries
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(10));
        
        // RETAIN to increase RC
        RETAIN((CljValue)vec);
        TEST_ASSERT_EQUAL(2, ((CljObject*)vec)->rc);
        
        // Now COW should trigger
        CljValue new_vec = (CljValue)vector_conj((CljPersistentVector*)vec, (ID)fixnum(20));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, new_vec); // NEW pointer!
        TEST_ASSERT_EQUAL(2, ((CljObject*)vec)->rc); // Original RC unchanged
        
        // Verify original vector unchanged
        TEST_ASSERT_EQUAL_INT(1, vector_count(vec));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        
        // Verify new vector has both entries
        CljPersistentVector *new_vec_data = as_vector(new_vec);
        TEST_ASSERT_EQUAL_INT(2, vector_count(new_vec_data));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec_data, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec_data, 1)));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
    });
}

// Test that vector_conj handles capacity growth with COW
TEST(test_vector_conj_cow_capacity_growth) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector *vec = (CljPersistentVector*)make_vector(2, false);
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // Fill capacity
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(10));
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(20));
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)vec);
        
        // Add more - should trigger COW with growth
        CljValue new_vec = (CljValue)vector_conj((CljPersistentVector*)vec, (ID)fixnum(30));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, new_vec); // NEW pointer!
        
        CljPersistentVector *new_vec_data = as_vector(new_vec);
        // Capacity is implementation detail, only check count
        TEST_ASSERT_EQUAL_INT(3, vector_count(new_vec_data));
        
        // Verify all entries
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec_data, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec_data, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec_data, 2)));
        
        // Original unchanged
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
    });
}

// Test that original vector remains unchanged after COW
TEST(test_vector_conj_cow_original_unchanged) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector* vec = make_vector(4, false);
        
        // Add entries
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(10));
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(20));
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)vec);
        CljValue new_vec = (CljValue)vector_conj((CljPersistentVector*)vec, (ID)fixnum(30));
        
        // Original should be unchanged
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(vec, 1)));
        
        // New vector should have all entries
        CljPersistentVector *new_vec_data = as_vector(new_vec);
        TEST_ASSERT_EQUAL_INT(3, vector_count(new_vec_data));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec_data, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec_data, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec_data, 2)));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
    });
}

// Test memory leak detection for vector_conj COW
TEST(test_vector_conj_cow_memory_leak) {
    WITH_MEMORY_PROFILING({
        CljPersistentVector* vec = make_vector(4, false);
        
        // Add entries
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(10));
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(20));
        vector_conj((CljPersistentVector*)vec, (ID)fixnum(30));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)vec);
        CljValue new_vec = (CljValue)vector_conj((CljPersistentVector*)vec, (ID)fixnum(40));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
        
        // Memory should be clean (no leaks)
    });
}

