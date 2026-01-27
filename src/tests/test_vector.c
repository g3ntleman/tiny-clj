// Vector-spezifische Tests
#include "tests_common.h"
#include "vector.h"
#include "types.h"

TEST_SHARED(test_vector_builtin_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // (vector) => []
    CljObject *v0 = eval_string("(vector)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v0);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v0->type);
    CljObject *c0 = eval_string("(count (vector))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(c0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c0));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)c0));

    // (vector 1 2 3) => [1 2 3]
    CljObject *v3 = eval_string("(vector 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v3->type);
    CljObject *n0 = eval_string("(nth (vector 1 2 3) 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n0));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)n0));
    CljObject *n2 = eval_string("(nth (vector 1 2 3) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)n2));

}

TEST_SHARED(test_nth_with_default_and_bounds) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // In-bounds ohne Default
    CljObject *x = eval_string("(nth [10 20 30] 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)x));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)x));

        // Out-of-bounds mit Default => default value (no exception)
        CljObject *d = NULL;
        TRY {
            d = eval_string("(nth [10 20 30] 5 :na)", g_test_eval_state);
        } CATCH(ex) {
            TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds");
        } END_TRY
        TEST_ASSERT_NOT_NULL_MESSAGE(d, "nth should return :na for out-of-bounds");
        TEST_ASSERT_TRUE_MESSAGE(TAG(d) == CLJ_SYMBOL && IS_KEYWORD(d),
            "nth should return :na keyword for out-of-bounds");

}

TEST_SHARED(test_nth_with_lists) {
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

    // Out-of-bounds without default => exception
    CljObject *out = NULL;
    TRY {
        out = eval_string("(nth '(10 20 30) 5)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(out);

    // Out-of-bounds with default => exception (not default value)
        CljObject *out_default = NULL;
        TRY {
            out_default = eval_string("(nth '(10 20 30) 5 :not-found)", g_test_eval_state);
        } CATCH(ex) {
            TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds");
        } END_TRY
        TEST_ASSERT_NOT_NULL_MESSAGE(out_default, "nth should return :not-found for out-of-bounds");
        TEST_ASSERT_TRUE_MESSAGE(TAG(out_default) == CLJ_SYMBOL && IS_KEYWORD(out_default),
            "nth should return :not-found keyword for out-of-bounds");

    // Empty list => exception
    CljObject *empty = NULL;
    TRY {
        empty = eval_string("(nth '() 0)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(empty);

    // Empty list with default => default value (no exception)
    CljObject *empty_default = NULL;
    TRY {
        empty_default = eval_string("(nth '() 0 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for empty list");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(empty_default, "nth should return :default for empty list");
    TEST_ASSERT_TRUE_MESSAGE(TAG(empty_default) == CLJ_SYMBOL && IS_KEYWORD(empty_default),
        "nth should return :default keyword for empty list");

    // List with nil elements
    // (nth '(1 nil 3) 1) => nil
    CljObject *nil_elem = eval_string("(nth '(1 nil 3) 1)", g_test_eval_state);
    if (nil_elem) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_elem), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(nil_elem);  // NULL is correct for nil
    }

    // (nth '(nil 2 nil) 0) => nil
    CljObject *nil_first = eval_string("(nth '(nil 2 nil) 0)", g_test_eval_state);
    if (nil_first) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_first), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(nil_first);  // NULL is correct for nil
    }

    // (nth '(nil 2 nil) 1) => 2
    CljObject *nil_second = eval_string("(nth '(nil 2 nil) 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(nil_second);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nil_second));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nil_second));

}

TEST_SHARED(test_nth_with_sequences) {
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

    // Out-of-bounds with sequence => exception
    CljObject *seq_out = NULL;
    TRY {
        seq_out = eval_string("(nth (rest '(10 20)) 5)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(seq_out);

    // Out-of-bounds with sequence and default => default value (no exception)
    CljObject *seq_out_default = NULL;
    TRY {
        seq_out_default = eval_string("(nth (rest '(10 20)) 5 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds sequence");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(seq_out_default, "nth should return :default for out-of-bounds sequence");
    TEST_ASSERT_TRUE_MESSAGE(TAG(seq_out_default) == CLJ_SYMBOL && IS_KEYWORD(seq_out_default),
        "nth should return :default keyword for out-of-bounds sequence");

}

TEST_SHARED(test_nth_edge_cases) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Negative index => exception
    CljObject *neg = NULL;
    TRY {
        neg = eval_string("(nth [10 20 30] -1)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(neg);

    // Negative index with default => exception (not default value)
        CljObject *neg_default = NULL;
        TRY {
            neg_default = eval_string("(nth [10 20 30] -1 :default)", g_test_eval_state);
        } CATCH(ex) {
            TEST_FAIL_MESSAGE("nth with default should not throw exception for negative index");
        } END_TRY
        TEST_ASSERT_NOT_NULL_MESSAGE(neg_default, "nth should return :default for negative index");
        TEST_ASSERT_TRUE_MESSAGE(TAG(neg_default) == CLJ_SYMBOL && IS_KEYWORD(neg_default),
            "nth should return :default keyword for negative index");

    // nil collection => exception
    CljObject *nil_coll = NULL;
    TRY {
        nil_coll = eval_string("(nth nil 0)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(nil_coll);

    // nil collection with default => default value (no exception)
    CljObject *nil_coll_default = NULL;
    TRY {
        nil_coll_default = eval_string("(nth nil 0 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for nil collection");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(nil_coll_default, "nth should return :default for nil collection");
    TEST_ASSERT_TRUE_MESSAGE(TAG(nil_coll_default) == CLJ_SYMBOL && IS_KEYWORD(nil_coll_default),
        "nth should return :default keyword for nil collection");

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

    // Just out of bounds => exception
    CljObject *just_out = NULL;
    TRY {
        just_out = eval_string("(nth [10 20 30] 3)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(just_out);

    // Just out of bounds with default => default value (no exception)
    CljObject *just_out_default = NULL;
    TRY {
        just_out_default = eval_string("(nth [10 20 30] 3 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(just_out_default, "nth should return :default for out-of-bounds");
    TEST_ASSERT_TRUE_MESSAGE(TAG(just_out_default) == CLJ_SYMBOL && IS_KEYWORD(just_out_default),
        "nth should return :default keyword for out-of-bounds");

    // Large index => exception
    CljObject *large = NULL;
    TRY {
        large = eval_string("(nth [10 20 30] 1000)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(large);

    // Large index with default => default value (no exception)
    CljObject *large_default = NULL;
    TRY {
        large_default = eval_string("(nth [10 20 30] 1000 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(large_default, "nth should return :default for large out-of-bounds");
    TEST_ASSERT_TRUE_MESSAGE(TAG(large_default) == CLJ_SYMBOL && IS_KEYWORD(large_default),
        "nth should return :default keyword for large out-of-bounds");

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

TEST_SHARED(test_nth_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Vector with nil elements
    // (nth [1 nil 3] 1) => nil
    CljObject *vec_nil = eval_string("(nth [1 nil 3] 1)", g_test_eval_state);
    // nil is represented as NULL - check both NULL and TAG for robustness
    if (vec_nil) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(vec_nil), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(vec_nil);  // NULL is correct for nil
    }

    // (nth [nil 2 nil] 0) => nil
    CljObject *vec_nil_first = eval_string("(nth [nil 2 nil] 0)", g_test_eval_state);
    if (vec_nil_first) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(vec_nil_first), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(vec_nil_first);  // NULL is correct for nil
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
        TEST_ASSERT_NIL(vec_nil_third);  // NULL is correct for nil
    }

    // List with nil elements (already tested in test_nth_with_lists, but adding more)
    // (nth '(nil nil nil) 1) => nil
    CljObject *list_all_nil = eval_string("(nth '(nil nil nil) 1)", g_test_eval_state);
    if (list_all_nil) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(list_all_nil), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(list_all_nil);  // NULL is correct for nil
    }

    // Distinguish between nil element and out-of-bounds
    // (nth [1 nil 3] 1) => nil (nil element)
    CljObject *nil_at_index = eval_string("(nth [1 nil 3] 1)", g_test_eval_state);
    if (nil_at_index) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_at_index), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(nil_at_index);  // NULL is correct for nil
    }

    // (nth [1 nil 3] 3) => exception (out-of-bounds)
    CljObject *out_of_bounds = NULL;
    TRY {
        out_of_bounds = eval_string("(nth [1 nil 3] 3)", g_test_eval_state);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(out_of_bounds);  // Out-of-bounds should throw exception

    // Using default to distinguish: nil element vs out-of-bounds
    // When element is nil, default is NOT returned (element exists, it's just nil)
    // This is Clojure behavior: (nth [1 nil 3] 1 :default) => nil (not :default)
    CljObject *nil_with_default = eval_string("(nth [1 nil 3] 1 :default)", g_test_eval_state);
    if (nil_with_default) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(nil_with_default), "nth should return nil (NULL or CLJ_NIL) for nil element, not default");
    } else {
        TEST_ASSERT_NIL(nil_with_default);  // nil element, not default
    }

    // Out-of-bounds with default => default value (no exception)
    CljObject *out_with_default = NULL;
    TRY {
        out_with_default = eval_string("(nth [1 nil 3] 3 :default)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("nth with default should not throw exception for out-of-bounds");
    } END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(out_with_default, "nth should return :default for out-of-bounds");
    TEST_ASSERT_TRUE_MESSAGE(TAG(out_with_default) == CLJ_SYMBOL && IS_KEYWORD(out_with_default),
        "nth should return :default keyword for out-of-bounds");

}

TEST_SHARED(test_peek_and_pop_vector) {
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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, pop1->type);
    CljObject *cnt = eval_string("(count (pop [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)cnt));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)cnt));

}

TEST_SHARED(test_subvec_bounds_and_slices) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *s1 = eval_string("(subvec [1 2 3 4] 1 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, s1->type);
    CljObject *s1n0 = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s1n0));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s1n0));

    // start-only
    CljObject *s2c = eval_string("(count (subvec [1 2 3 4] 2))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s2c));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s2c));

}

TEST_SHARED(test_subvec_edge_cases) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Normal cases
    // (subvec [1 2 3 4] 0 4) → [1 2 3 4] (complete vector)
    CljObject *full = eval_string("(subvec [1 2 3 4] 0 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(full);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, full->type);
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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, empty->type);
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

TEST_SHARED(test_subvec_error_cases) {
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
        (void)eval_string("(subvec [1 2 3])", g_test_eval_state);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("Expected ArityException to be thrown");
    } CATCH(ex) {
        // Expected: ArityException
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_ARITY, ex->type);
    } END_TRY

    // Arity error: 4 arguments
    TRY {
        (void)eval_string("(subvec [1 2 3] 0 1 2)", g_test_eval_state);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("Expected ArityException to be thrown");
    } CATCH(ex) {
        // Expected: ArityException
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_ARITY, ex->type);
    } END_TRY

}

TEST_SHARED(test_vec_from_list_and_vector_id) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: vec converts list to vector
    // (vec '(1 2 3)) => [1 2 3]
    CljObject *v = eval_string("(vec '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v->type);
    CljObject *c = eval_string("(count (vec '(1 2 3)))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c));
    
    // Test: vec on vector is No-Op (returns same vector)
    // (vec [1 2 3]) => [1 2 3] (same object)
    CljObject *v1 = eval_string("(vec [1 2 3])", g_test_eval_state);
    CljObject *v2 = eval_string("(vec [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v1->type);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v2->type);
    // Note: They might be different objects (new evaluation), but same content
    CljObject *c1 = eval_string("(count (vec [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c1));
    
    // Test: vec on empty list => empty vector
    // (vec '()) => []
    CljObject *empty = eval_string("(vec '())", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, empty->type);
    CljObject *empty_count = eval_string("(count (vec '()))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_count));

}

TEST_SHARED(test_vec_with_nil_elements) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: vec converts list with nil element to vector
    // (vec '(1 nil 3)) => [1 nil 3]
    CljObject *v = eval_string("(vec '(1 nil 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v->type);
    
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
        TEST_ASSERT_NIL(second);  // NULL is correct for nil
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
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, v2->type);
    CljObject *c2 = eval_string("(count (vec '(nil 2 nil)))", g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c2));
    
    // Check first element (should be nil)
    CljObject *first2 = eval_string("(nth (vec '(nil 2 nil)) 0)", g_test_eval_state);
    // nil is represented as NULL - check both NULL and TAG for robustness
    if (first2) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NIL, TAG(first2), "nth should return nil (NULL or CLJ_NIL) for nil element");
    } else {
        TEST_ASSERT_NIL(first2);  // NULL is correct for nil
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
        TEST_ASSERT_NIL(third2);  // NULL is correct for nil
    }

}

// ============================================================================
// ============================================================================
// WEAK VECTOR TESTS
// ============================================================================

TEST_SHARED(test_weak_vector_does_not_retain_elements) {
    TEST_IGNORE_MESSAGE("CLJ_VECTOR_TRANSIENT_WEAK is private");
    // Test that adding elements to CLJ_VECTOR_TRANSIENT_WEAK does NOT increase their RC
    WITH_AUTORELEASE_POOL({
        // Create a weak vector (like autorelease pool)
        CljPersistentVector *weak_vec = make_vector(8, CLJ_VECTOR_TRANSIENT_WEAK);
        TEST_ASSERT_NOT_NULL(weak_vec);
        
        // Create an object with RC=1
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add to weak vector - should NOT increase RC
        // vector_assoc handles count increment and capacity growth automatically
        unsigned int count = vector_count(weak_vec);
        CljPersistentVector *new_vec = vector_assoc(weak_vec, count, map);
        TEST_ASSERT_NOT_NULL(new_vec);
        
        // RC should still be 1 (not retained)
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Cleanup: vector_assoc may return a new vector if capacity grew
        // If it's the same vector, only release once
        if (new_vec != weak_vec) {
            // New vector was created (capacity grew), old one was already released in vector_assoc
            RELEASE(new_vec);
        } else {
            // Same vector, release once
            RELEASE(weak_vec);
        }
        RELEASE(map);
    });
}

TEST_SHARED(test_weak_vector_does_not_release_elements) {
    TEST_IGNORE_MESSAGE("CLJ_VECTOR_TRANSIENT_WEAK is private");
    // Test that removing elements from CLJ_VECTOR_TRANSIENT_WEAK does NOT decrease their RC
    WITH_AUTORELEASE_POOL({
        // Create a weak vector
        CljPersistentVector *weak_vec = make_vector(8, CLJ_VECTOR_TRANSIENT_WEAK);
        TEST_ASSERT_NOT_NULL(weak_vec);
        
        // Create an object with RC=1
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add to weak vector
        unsigned int count = vector_count(weak_vec);
        // Grow capacity if needed using make_vector_copy
        // Since we can't access capacity directly, we'll grow when count >= initial capacity (8)
        if (count >= 8) {
            int newcap = 16;  // Double the initial capacity
            CljPersistentVector *new_vec = make_vector_copy(weak_vec, newcap);
            if (new_vec) {
                RELEASE(weak_vec);
                weak_vec = new_vec;
            }
        }
        vector_increment_count(weak_vec);
        CljPersistentVector *new_vec = vector_assoc(weak_vec, count, map);
        TEST_ASSERT_NOT_NULL(new_vec);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // Still 1
        
        // Remove from weak vector using vector_pop - should NOT decrease RC
        CljPersistentVector *popped = vector_pop(new_vec);
        TEST_ASSERT_NOT_NULL(popped);
        
        // RC should still be 1 (not released)
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Cleanup
        RELEASE(weak_vec);
        if (new_vec != weak_vec) {
            RELEASE(new_vec);
        }
        if (popped != new_vec) {
            RELEASE(popped);
        }
        RELEASE(map);
    });
}

TEST_SHARED(test_weak_vector_nth_does_not_retain) {
    TEST_IGNORE_MESSAGE("CLJ_VECTOR_TRANSIENT_WEAK is private");
    // Test that vector_nth does NOT retain elements for CLJ_VECTOR_TRANSIENT_WEAK
    WITH_AUTORELEASE_POOL({
        // Create a weak vector
        CljPersistentVector *weak_vec = make_vector(8, CLJ_VECTOR_TRANSIENT_WEAK);
        TEST_ASSERT_NOT_NULL(weak_vec);
        
        // Create an object with RC=1
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add to weak vector
        // Note: vector_assoc for CLJ_VECTOR_TRANSIENT_WEAK allows index == count (append)
        unsigned int count = vector_count(weak_vec);
        // Grow capacity if needed using make_vector_copy
        // Since we can't access capacity directly, we'll grow when count >= initial capacity (8)
        if (count >= 8) {
            int newcap = 16;  // Double the initial capacity
            CljPersistentVector *new_vec = make_vector_copy(weak_vec, newcap);
            if (new_vec) {
                RELEASE(weak_vec);
                weak_vec = new_vec;
            }
        }
        vector_increment_count(weak_vec);
        CljPersistentVector *new_vec = vector_assoc(weak_vec, count, map);
        TEST_ASSERT_NOT_NULL(new_vec);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // Still 1
        TEST_ASSERT_EQUAL(1, vector_count(new_vec));  // Count should be 1
        
        // Get element using vector_nth - for weak vectors, does NOT retain
        ID result = vector_nth(new_vec, 0);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(map, result);
        
        // RC should still be 1 (not retained for weak vectors)
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Cleanup
        RELEASE(weak_vec);
        if (new_vec != weak_vec) {
            RELEASE(new_vec);
        }
        RELEASE(map);
    });
}

TEST_SHARED(test_weak_vector_multiple_elements_rc_unchanged) {
    TEST_IGNORE_MESSAGE("CLJ_VECTOR_TRANSIENT_WEAK is private");
    // Test that multiple elements in CLJ_VECTOR_TRANSIENT_WEAK maintain their RC
    WITH_AUTORELEASE_POOL({
        // Create a weak vector
        CljPersistentVector *weak_vec = make_vector(8, CLJ_VECTOR_TRANSIENT_WEAK);
        TEST_ASSERT_NOT_NULL(weak_vec);
        
        // Create multiple objects
        CljMap *map1 = (CljMap*)make_map(4);
        CljMap *map2 = (CljMap*)make_map(4);
        CljMap *map3 = (CljMap*)make_map(4);
        
        TEST_ASSERT_EQUAL(1, map1->base.rc);
        TEST_ASSERT_EQUAL(1, map2->base.rc);
        TEST_ASSERT_EQUAL(1, map3->base.rc);
        
        // Add all to weak vector
        // Note: vector_assoc for CLJ_VECTOR_TRANSIENT_WEAK with rc=1 mutates in-place
        for (int i = 0; i < 3; i++) {
            CljMap *map = (i == 0) ? map1 : (i == 1) ? map2 : map3;
            unsigned int count = vector_count(weak_vec);
            // Grow capacity if needed using make_vector_copy
        // Since we can't access capacity directly, we'll grow when count >= initial capacity (8)
        if (count >= 8) {
            int newcap = 16;  // Double the initial capacity
            CljPersistentVector *new_vec = make_vector_copy(weak_vec, newcap);
            if (new_vec) {
                RELEASE(weak_vec);
                weak_vec = new_vec;
            }
        }
            vector_increment_count(weak_vec);
            CljPersistentVector *new_vec = vector_assoc(weak_vec, count, map);
            TEST_ASSERT_NOT_NULL(new_vec);
            // For CLJ_VECTOR_TRANSIENT_WEAK with rc=1, vector_assoc mutates in-place
            if (new_vec != weak_vec) {
                RELEASE(weak_vec);
                weak_vec = new_vec;
            }
            // Verify count after each addition
            TEST_ASSERT_EQUAL(i + 1, vector_count(weak_vec));
        }
        
        // All RCs should still be 1
        TEST_ASSERT_EQUAL(1, map1->base.rc);
        TEST_ASSERT_EQUAL(1, map2->base.rc);
        TEST_ASSERT_EQUAL(1, map3->base.rc);
        
        // Verify final count
        TEST_ASSERT_EQUAL(3, vector_count(weak_vec));
        
        // Cleanup
        RELEASE(weak_vec);
        RELEASE(map1);
        RELEASE(map2);
        RELEASE(map3);
    });
}

TEST_SHARED(test_weak_vector_clear_does_not_release_elements) {
    TEST_IGNORE_MESSAGE("CLJ_VECTOR_TRANSIENT_WEAK is private");
    // Test that vector_clear does NOT release elements for CLJ_VECTOR_TRANSIENT_WEAK
    WITH_AUTORELEASE_POOL({
        // Create a weak vector
        CljPersistentVector *weak_vec = make_vector(8, CLJ_VECTOR_TRANSIENT_WEAK);
        TEST_ASSERT_NOT_NULL(weak_vec);
        
        // Create an object with RC=1
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add to weak vector
        unsigned int count = vector_count(weak_vec);
        // Grow capacity if needed using make_vector_copy
        // Since we can't access capacity directly, we'll grow when count >= initial capacity (8)
        if (count >= 8) {
            int newcap = 16;  // Double the initial capacity
            CljPersistentVector *new_vec = make_vector_copy(weak_vec, newcap);
            if (new_vec) {
                RELEASE(weak_vec);
                weak_vec = new_vec;
            }
        }
        vector_increment_count(weak_vec);
        CljPersistentVector *new_vec = vector_assoc(weak_vec, count, map);
        TEST_ASSERT_NOT_NULL(new_vec);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // Still 1
        
        // Clear weak vector - should NOT release elements
        vector_clear(new_vec);
        
        // RC should still be 1 (not released)
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Count should be 0
        TEST_ASSERT_EQUAL(0, vector_count(new_vec));
        
        // Cleanup
        RELEASE(weak_vec);
        if (new_vec != weak_vec) {
            RELEASE(new_vec);
        }
        RELEASE(map);
    });
}

// Test that clj_conj correctly updates count for transient vectors (event loop scenario)
TEST_SHARED(test_clj_conj_updates_count_for_event_loop) {
    WITH_AUTORELEASE_POOL({
        // Simulate event_loop_enqueue scenario:
        // 1. Create persistent vector with capacity
        CljPersistentVector *task_vec = make_vector(8, CLJ_VECTOR_PERSISTENT);
        TEST_ASSERT_NOT_NULL(task_vec);
        TEST_ASSERT_EQUAL_INT(0, vector_count(task_vec));
        
        // 2. Convert to transient (like task_queue_get does)
        CljPersistentVector *tvec = vector_transient(task_vec);
        RELEASE(task_vec);
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_TRANSIENT, ((CljObject*)tvec)->type);
        TEST_ASSERT_EQUAL_INT(0, vector_count(tvec));
        
        // 3. Use clj_conj to add an item (like event_loop_enqueue does)
        CljMap *test_map = make_map(2);
        TEST_ASSERT_NOT_NULL(test_map);
        
        CljPersistentVector *result = clj_conj(tvec, test_map);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(tvec, result);  // Should be same pointer (in-place)
        
        // 4. Check that count was updated correctly
        unsigned int count_after = vector_count(tvec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_after, 
            "clj_conj should increment count from 0 to 1");
        
        // 5. Add another item
        CljMap *test_map2 = make_map(2);
        TEST_ASSERT_NOT_NULL(test_map2);
        
        CljPersistentVector *result2 = clj_conj(tvec, test_map2);
        TEST_ASSERT_NOT_NULL(result2);
        TEST_ASSERT_EQUAL_PTR(tvec, result2);
        
        // 6. Check that count was updated again
        unsigned int count_after2 = vector_count(tvec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, count_after2,
            "clj_conj should increment count from 1 to 2");
        
        // 7. Verify elements are accessible
        ID elem0 = vector_nth(tvec, 0);
        ID elem1 = vector_nth(tvec, 1);
        TEST_ASSERT_EQUAL_PTR(test_map, elem0);
        TEST_ASSERT_EQUAL_PTR(test_map2, elem1);
        
        // Cleanup: elem0/elem1 lifetimes are tied to vector, do not RELEASE
        RELEASE(tvec);
        RELEASE(test_map);
        RELEASE(test_map2);
    });
}

// Test that clj_conj works with empty transient vector (capacity 0 scenario)
TEST_SHARED(test_clj_conj_with_empty_transient_vector) {
    WITH_AUTORELEASE_POOL({
        // Create empty persistent vector (capacity 0)
        CljPersistentVector *task_vec = make_vector(0, CLJ_VECTOR_PERSISTENT);
        TEST_ASSERT_NOT_NULL(task_vec);
        TEST_ASSERT_EQUAL_INT(0, vector_count(task_vec));
        
        // Convert to transient
        CljPersistentVector *tvec = vector_transient(task_vec);
        RELEASE(task_vec);
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_TRANSIENT, ((CljObject*)tvec)->type);
        TEST_ASSERT_EQUAL_INT(0, vector_count(tvec));
        
        // clj_conj should grow capacity and add item
        CljMap *test_map = make_map(2);
        TEST_ASSERT_NOT_NULL(test_map);
        
        ID result = clj_conj(tvec, test_map);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(tvec, result);
        
        // Count should be 1
        unsigned int count_after = vector_count(tvec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_after,
            "clj_conj should increment count even when starting with capacity 0");
        
        // Verify element is accessible
        ID elem0 = vector_nth(tvec, 0);
        TEST_ASSERT_EQUAL_PTR(test_map, elem0);
        
        // Cleanup: elem0 lifetime is tied to vector, do not RELEASE
        RELEASE(tvec);
        RELEASE(test_map);
    });
}

// New tests for transient vector mutable functions (backing_store design)
TEST_SHARED(test_transient_vector_conj_keeps_pointer_and_updates_backing_store) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector *vec = make_vector(1, CLJ_VECTOR_PERSISTENT);
        TEST_ASSERT_NOT_NULL(vec);

        CljPersistentVector *tvec = vector_transient(vec);
        RELEASE(vec);
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_TRANSIENT, ((CljObject*)tvec)->type);

        CljPersistentVector *result = vector_conj(tvec, fixnum(10));
        TEST_ASSERT_EQUAL_PTR(tvec, result);

        CljPersistentVector *backing = vector_persistent((CljTransientVector*)tvec);
        TEST_ASSERT_NOT_NULL(backing);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, ((CljObject*)backing)->type);
        TEST_ASSERT_EQUAL_INT(1, retain_count(backing));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(backing, 0)));

        CljPersistentVector *result2 = vector_conj(tvec, fixnum(20));
        TEST_ASSERT_EQUAL_PTR(tvec, result2);
        backing = vector_persistent((CljTransientVector*)tvec);
        TEST_ASSERT_NOT_NULL(backing);
        TEST_ASSERT_EQUAL_INT(2, vector_count(backing));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(vector_nth(backing, 1)));

        RELEASE(tvec);
    });
}

TEST_SHARED(test_transient_vector_assoc_keeps_pointer_and_updates_backing_store) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector *vec = make_vector(2, CLJ_VECTOR_PERSISTENT);
        vec = vector_conj(vec, fixnum(1));
        vec = vector_conj(vec, fixnum(2));
        TEST_ASSERT_NOT_NULL(vec);

        CljPersistentVector *tvec = vector_transient(vec);
        RELEASE(vec);
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_TRANSIENT, ((CljObject*)tvec)->type);

        CljPersistentVector *result = vector_assoc(tvec, 1, fixnum(99));
        TEST_ASSERT_EQUAL_PTR(tvec, result);

        CljPersistentVector *backing = vector_persistent((CljTransientVector*)tvec);
        TEST_ASSERT_NOT_NULL(backing);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR_PERSISTENT, ((CljObject*)backing)->type);
        TEST_ASSERT_EQUAL_INT(1, retain_count(backing));
        TEST_ASSERT_EQUAL_INT(99, as_fixnum(vector_nth(backing, 1)));

        RELEASE(tvec);
    });
}

TEST_SHARED(test_transient_vector_capacity_growth_keeps_pointer) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector *vec = make_vector(1, CLJ_VECTOR_PERSISTENT);
        TEST_ASSERT_NOT_NULL(vec);

        CljPersistentVector *tvec = vector_transient(vec);
        RELEASE(vec);
        TEST_ASSERT_NOT_NULL(tvec);

        for (int i = 0; i < 8; ++i) {
            CljPersistentVector *result = vector_conj(tvec, fixnum(i));
            TEST_ASSERT_EQUAL_PTR(tvec, result);
        }

        CljPersistentVector *backing = vector_persistent((CljTransientVector*)tvec);
        TEST_ASSERT_NOT_NULL(backing);
        TEST_ASSERT_EQUAL_INT(8, vector_count(backing));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(backing, 0)));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(vector_nth(backing, 7)));

        RELEASE(tvec);
    });
}

TEST_SHARED(test_equal_persistent_and_transient_vector) {
    // Test that (= persistent-vector transient-vector) returns false
    // This matches Clojure/JVM behavior where transient and persistent vectors
    // are considered different types, even if they have the same elements
    WITH_AUTORELEASE_POOL({
        // Test: (= persistent transient) should be false (different types)
        CljObject *equal_result = eval_string("(= (vector 1 2 3) (transient (vector 1 2 3)))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(equal_result);
        TEST_ASSERT_TRUE(is_special(equal_result));
        TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(equal_result));
        
        // Test: (= persistent persistent) should be true (same type, same elements)
        CljObject *equal_persistent = eval_string("(= (vector 1 2 3) (vector 1 2 3))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(equal_persistent);
        TEST_ASSERT_TRUE(is_special(equal_persistent));
        TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(equal_persistent));
        
        // Test: (= transient transient) should be true (same type, same elements)
        // Note: We need to create two separate transient vectors
        CljObject *equal_transient = eval_string("(= (transient (vector 1 2 3)) (transient (vector 1 2 3)))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(equal_transient);
        TEST_ASSERT_TRUE(is_special(equal_transient));
        TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(equal_transient));
        
        // Test: (= persistent (persistent! transient)) should be true
        // After converting transient back to persistent, they should be equal
        CljObject *equal_after_persistent = eval_string("(= (vector 1 2 3) (persistent! (transient (vector 1 2 3))))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(equal_after_persistent);
        TEST_ASSERT_TRUE(is_special(equal_after_persistent));
        TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(equal_after_persistent));
    });
}

// Clojure-compatibility test: (transient) on transient collection returns the same object
TEST_SHARED(test_transient_on_transient_returns_same_object) {
    WITH_AUTORELEASE_POOL({
        // Test 1: (transient) on transient vector returns the same object
        CljObject *tvec1 = eval_string("(transient (vector 1 2 3))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(tvec1);
        TEST_ASSERT_TRUE(TAG(tvec1) == CLJ_VECTOR_TRANSIENT);
        
        // Call transient again on the transient vector
        CljObject *tvec2 = eval_string("(transient (transient (vector 1 2 3)))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(tvec2);
        TEST_ASSERT_TRUE(TAG(tvec2) == CLJ_VECTOR_TRANSIENT);
        
        // They should be equal (same elements)
        CljObject *equal_result = eval_string("(= (transient (vector 1 2 3)) (transient (transient (vector 1 2 3))))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(equal_result);
        TEST_ASSERT_TRUE(is_special(equal_result));
        TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(equal_result));
        
        // Test 2: Direct test using native_transient function
        // Create a transient vector
        CljPersistentVector *vec = make_vector(3, CLJ_VECTOR_PERSISTENT);
        vec = vector_conj(vec, fixnum(1));
        vec = vector_conj(vec, fixnum(2));
        vec = vector_conj(vec, fixnum(3));
        CljPersistentVector *tvec = vector_transient(vec);
        RELEASE(vec);
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_TRUE(TAG(tvec) == CLJ_VECTOR_TRANSIENT);
        
        // Call transient on the transient vector - should return same object
        ID args[] = {tvec};
        CljObject *result = (CljObject*)native_transient(args, 1);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(tvec, result);  // Should be the same pointer
        TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_TRANSIENT);
        
        RELEASE(tvec);
        
        // Test 3: (transient) on transient map returns the same object
        CljMap *map = make_map(4);
        CljObject *key1 = (CljObject*)intern_symbol(NULL, ":a");
        CljObject *key2 = (CljObject*)intern_symbol(NULL, ":b");
        map = map_assoc(map, key1, fixnum(1));
        map = map_assoc(map, key2, fixnum(2));
        CljMap *tmap = map_transient(map);
        RELEASE(map);
        TEST_ASSERT_NOT_NULL(tmap);
        TEST_ASSERT_TRUE(TAG(tmap) == CLJ_MAP_TRANSIENT);
        
        // Call transient on the transient map - should return same object
        ID map_args[] = {tmap};
        CljObject *map_result = (CljObject*)native_transient(map_args, 1);
        TEST_ASSERT_NOT_NULL(map_result);
        TEST_ASSERT_EQUAL_PTR(tmap, map_result);  // Should be the same pointer
        TEST_ASSERT_TRUE(TAG(map_result) == CLJ_MAP_TRANSIENT);
        
        RELEASE(tmap);
    });
}

// Clojure-compatibility test: (persistent!) on persistent collection returns the same object
TEST_SHARED(test_persistent_on_persistent_returns_same_object) {
    WITH_AUTORELEASE_POOL({
        // Test 1: (persistent!) on persistent vector returns the same object
        CljObject *vec1 = eval_string("(vector 1 2 3)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(vec1);
        TEST_ASSERT_TRUE(TAG(vec1) == CLJ_VECTOR_PERSISTENT);
        
        // Call persistent! on the persistent vector
        CljObject *vec2 = eval_string("(persistent! (vector 1 2 3))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(vec2);
        TEST_ASSERT_TRUE(TAG(vec2) == CLJ_VECTOR_PERSISTENT);
        
        // They should be equal (same elements)
        CljObject *equal_result = eval_string("(= (vector 1 2 3) (persistent! (vector 1 2 3)))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(equal_result);
        TEST_ASSERT_TRUE(is_special(equal_result));
        TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(equal_result));
        
        // Test 2: Direct test using native_persistent_bang function
        CljPersistentVector *vec = make_vector(3, CLJ_VECTOR_PERSISTENT);
        vec = vector_conj(vec, fixnum(1));
        vec = vector_conj(vec, fixnum(2));
        vec = vector_conj(vec, fixnum(3));
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_TRUE(TAG(vec) == CLJ_VECTOR_PERSISTENT);
        
        // Call persistent! on the persistent vector - should return same object
        ID args[] = {vec};
        CljObject *result = (CljObject*)native_persistent_bang(args, 1);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(vec, result);  // Should be the same pointer
        TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
        
        RELEASE(vec);
        
        // Test 3: (persistent!) on persistent map returns the same object
        CljMap *map = make_map(4);
        CljObject *key1 = (CljObject*)intern_symbol(NULL, ":a");
        CljObject *key2 = (CljObject*)intern_symbol(NULL, ":b");
        map = map_assoc(map, key1, fixnum(1));
        map = map_assoc(map, key2, fixnum(2));
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_TRUE(TAG(map) == CLJ_MAP);
        
        // Call persistent! on the persistent map - should return same object
        ID map_args[] = {map};
        CljObject *map_result = (CljObject*)native_persistent_bang(map_args, 1);
        TEST_ASSERT_NOT_NULL(map_result);
        TEST_ASSERT_EQUAL_PTR(map, map_result);  // Should be the same pointer
        TEST_ASSERT_TRUE(TAG(map_result) == CLJ_MAP);
        
        RELEASE(map);
    });
}

// Test VECTOR_FOR_EACH macro - iterate over all vector elements
TEST_SHARED(test_vector_for_each_macro) {
    CljPersistentVector *vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    
    // Add elements to vector
    vec = vector_conj(vec, fixnum(1));
    vec = vector_conj(vec, fixnum(2));
    vec = vector_conj(vec, fixnum(3));
    
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    
    // Count iterations and verify elements
    int iteration_count = 0;
    CljValue found_values[3] = {NULL, NULL, NULL};
    
    VECTOR_FOR_EACH(vec, elem) {
        found_values[iteration_count] = (CljValue)elem;
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(3, iteration_count);
    
    // Verify that all values were found
    bool found_val1 = false, found_val2 = false, found_val3 = false;
    for (int i = 0; i < 3; i++) {
        if (is_fixnum(found_values[i])) {
            int val = as_fixnum(found_values[i]);
            if (val == 1) found_val1 = true;
            else if (val == 2) found_val2 = true;
            else if (val == 3) found_val3 = true;
        }
    }
    
    TEST_ASSERT_TRUE(found_val1);
    TEST_ASSERT_TRUE(found_val2);
    TEST_ASSERT_TRUE(found_val3);
}

// Test VECTOR_FOR_EACH with empty vector
TEST_SHARED(test_vector_for_each_empty_vector) {
    CljPersistentVector *vec = AUTORELEASE(make_vector(0, CLJ_VECTOR_PERSISTENT));
    
    int iteration_count = 0;
    VECTOR_FOR_EACH(vec, elem) {
        (void)elem;  // unused
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(0, iteration_count);
}

// Test VECTOR_FOR_EACH with NULL vector (should not crash)
TEST_SHARED(test_vector_for_each_null_vector) {
    CljPersistentVector *vec = NULL;
    
    int iteration_count = 0;
    VECTOR_FOR_EACH(vec, elem) {
        (void)elem;  // unused
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(0, iteration_count);
}

// Test VECTOR_FOR_EACH with NULL elements
TEST_SHARED(test_vector_for_each_with_null_elements) {
    CljPersistentVector *vec = AUTORELEASE(make_vector(4, CLJ_VECTOR_PERSISTENT));
    
    // Add elements including NULL (nil)
    vec = vector_conj(vec, fixnum(1));
    vec = vector_conj(vec, NULL);  // NULL element
    vec = vector_conj(vec, fixnum(3));
    
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    
    // Count iterations and verify elements (including NULL)
    int iteration_count = 0;
    CljValue found_values[3] = {NULL, NULL, NULL};
    bool found_null = false;
    
    VECTOR_FOR_EACH(vec, elem) {
        found_values[iteration_count] = (CljValue)elem;
        if (elem == NULL) {
            found_null = true;
        }
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(3, iteration_count);
    TEST_ASSERT_TRUE(found_null);  // NULL element should be found
}

// Test vector_set_nth with reference count checks (transient vector)
TEST_SHARED(test_vector_set_nth_with_reference_counts) {
    CljPersistentVector *vec = make_vector(4, CLJ_VECTOR_TRANSIENT);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(1, retain_count(vec));
    
    CljObject *old_val = AUTORELEASE(make_string("old"));
    CljObject *new_val = AUTORELEASE(make_string("new"));
    
    vec = vector_conj(vec, old_val);
    vec = vector_conj(vec, fixnum(30));
    vec = vector_conj(vec, fixnum(40));
    
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(1, retain_count(vec));
    
    int old_val_rc_before = retain_count(old_val);
    TEST_ASSERT_EQUAL_INT(2, old_val_rc_before);
    
    CljPersistentVector *result = vector_set_nth(vec, 0, new_val);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(vec, result);
    TEST_ASSERT_EQUAL_INT(1, retain_count(result));
    
    // For transient vectors, old value is NOT released
    int old_val_rc_after = retain_count(old_val);
    TEST_ASSERT_EQUAL_INT(old_val_rc_before, old_val_rc_after);
    
    int new_val_rc_after = retain_count(new_val);
    TEST_ASSERT_TRUE(new_val_rc_after > 0);
    
    ID elem = vector_nth(result, 0);
    TEST_ASSERT_NOT_NULL(elem);
    TEST_ASSERT_TRUE(TAG(elem) == CLJ_STRING);
    CljString *str = as_clj_string(elem);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_STRING("new", str->data);
    
    AUTORELEASE(vec);
}

// Test vector_set_nth with transient vector and multiple references
TEST_SHARED(test_vector_set_nth_copy_on_write) {
    CljPersistentVector *vec = make_vector(4, CLJ_VECTOR_TRANSIENT);
    TEST_ASSERT_NOT_NULL(vec);
    
    CljObject *old_val = AUTORELEASE(make_string("old"));
    CljObject *new_val = AUTORELEASE(make_string("new"));
    
    vec = vector_conj(vec, old_val);
    vec = vector_conj(vec, fixnum(300));
    
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(1, retain_count(vec));
    
    RETAIN(vec);
    TEST_ASSERT_EQUAL_INT(2, retain_count(vec));
    
    int old_val_rc_before = retain_count(old_val);
    TEST_ASSERT_EQUAL_INT(2, old_val_rc_before);
    
    CljPersistentVector *result = vector_set_nth(vec, 0, new_val);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(vec, result);
    TEST_ASSERT_EQUAL_INT(2, retain_count(vec));
    
    // For transient vectors, old value is NOT released
    int old_val_rc_after = retain_count(old_val);
    TEST_ASSERT_EQUAL_INT(old_val_rc_before, old_val_rc_after);
    
    int new_val_rc_after = retain_count(new_val);
    TEST_ASSERT_EQUAL_INT(2, new_val_rc_after);
    
    ID elem = vector_nth(result, 0);
    TEST_ASSERT_NOT_NULL(elem);
    TEST_ASSERT_TRUE(TAG(elem) == CLJ_STRING);
    CljString *str = as_clj_string(elem);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_STRING("new", str->data);
    
    AUTORELEASE(vec);
}

// Test vector_set_nth with transient vector
TEST_SHARED(test_vector_set_nth_transient) {
    CljPersistentVector *vec = make_vector(4, CLJ_VECTOR_TRANSIENT);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_TRUE(TAG(vec) == CLJ_VECTOR_TRANSIENT);
    
    CljObject *old_val = AUTORELEASE(make_string("old"));
    CljObject *new_val = AUTORELEASE(make_string("new"));
    
    vec = vector_conj(vec, old_val);
    vec = vector_conj(vec, fixnum(25));
    
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    
    int old_val_rc_before = retain_count(old_val);
    TEST_ASSERT_EQUAL_INT(2, old_val_rc_before);
    
    CljPersistentVector *result = vector_set_nth(vec, 0, new_val);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(vec, result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_TRANSIENT);
    
    ID elem = vector_nth(result, 0);
    TEST_ASSERT_NOT_NULL(elem);
    TEST_ASSERT_TRUE(TAG(elem) == CLJ_STRING);
    CljString *str = as_clj_string(elem);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_STRING("new", str->data);
    
    // For transient vectors, old value is NOT released
    int old_val_rc_after = retain_count(old_val);
    TEST_ASSERT_EQUAL_INT(old_val_rc_before, old_val_rc_after);
    
    int new_val_rc_after = retain_count(new_val);
    TEST_ASSERT_TRUE(new_val_rc_after > 0);
    
    AUTORELEASE(vec);
}

// Test vector_set_nth edge cases (NULL vector, out-of-bounds, NULL value)
TEST_SHARED(test_vector_set_nth_edge_cases) {
    // Test 1: NULL vector should return NULL
    CljPersistentVector *result1 = vector_set_nth(NULL, 0, fixnum(42));
    TEST_ASSERT_NULL(result1);
    
    // Test 2: Out-of-bounds index should return NULL
    CljPersistentVector *vec = make_vector(4, CLJ_VECTOR_TRANSIENT);
    TEST_ASSERT_NOT_NULL(vec);
    vec = vector_conj(vec, fixnum(10));
    vec = vector_conj(vec, fixnum(20));
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
    
    // Index 2 is out of bounds (count is 2, valid indices are 0 and 1)
    CljPersistentVector *result2 = vector_set_nth(vec, 2, fixnum(30));
    TEST_ASSERT_NULL(result2);
    
    // Test 3: NULL value is allowed (represents nil) and should succeed
    CLJException *exception_caught = NULL;
    CljPersistentVector *result3 = NULL;
    TRY {
        result3 = vector_set_nth(vec, 0, NULL);
    } CATCH(ex) {
        exception_caught = ex;
    } END_TRY
    TEST_ASSERT_NULL(exception_caught);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_PTR(vec, result3);
    TEST_ASSERT_NULL(vector_nth(result3, 0));
    
    AUTORELEASE(vec);
}

// Test that vector_set_nth throws exception for persistent vectors
TEST_SHARED(test_vector_set_nth_persistent_throws_exception) {
    CljPersistentVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_NOT_NULL(vec);
    vec = vector_conj(vec, fixnum(10));
    vec = vector_conj(vec, fixnum(20));
    vec = vector_conj(vec, fixnum(30));
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    
    CLJException *exception_caught = NULL;
    TRY {
        vector_set_nth(vec, 0, fixnum(100));
    } CATCH(ex) {
        exception_caught = ex;
    } END_TRY
    TEST_ASSERT_NOT_NULL(exception_caught);
    
    AUTORELEASE(vec);
}
