// Vector-spezifische Tests
#include "tests_common.h"

TEST(test_vector_builtin_basic) {
    TEST_ASSERT_NOT_NULL(st);

    // (vector) => []
    CljObject *v0 = eval_string("(vector)", st);
    TEST_ASSERT_NOT_NULL(v0);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v0->type);
    CljObject *c0 = eval_string("(count (vector))", st);
    TEST_ASSERT_NOT_NULL(c0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c0));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)c0));

    // (vector 1 2 3) => [1 2 3]
    CljObject *v3 = eval_string("(vector 1 2 3)", st);
    TEST_ASSERT_NOT_NULL(v3);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v3->type);
    CljObject *n0 = eval_string("(nth (vector 1 2 3) 0)", st);
    TEST_ASSERT_NOT_NULL(n0);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n0));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)n0));
    CljObject *n2 = eval_string("(nth (vector 1 2 3) 2)", st);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)n2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)n2));

}

TEST(test_nth_with_default_and_bounds) {
    TEST_ASSERT_NOT_NULL(st);

    // In-bounds ohne Default
    CljObject *x = eval_string("(nth [10 20 30] 1)", st);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)x));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)x));

    // Out-of-bounds mit Default
    CljObject *d = eval_string("(nth [10 20 30] 5 :na)", st);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_TRUE(is_type(d, CLJ_SYMBOL));

}

TEST(test_peek_and_pop_vector) {
    TEST_ASSERT_NOT_NULL(st);

    // peek
    CljObject *p1 = eval_string("(peek [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)p1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)p1));
    CljObject *p0 = eval_string("(peek [])", st);
    TEST_ASSERT_NULL(p0);

    // pop
    CljObject *pop1 = eval_string("(pop [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(pop1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, pop1->type);
    CljObject *cnt = eval_string("(count (pop [1 2 3]))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)cnt));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)cnt));

}

TEST(test_subvec_bounds_and_slices) {
    TEST_ASSERT_NOT_NULL(st);

    CljObject *s1 = eval_string("(subvec [1 2 3 4] 1 3)", st);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, s1->type);
    CljObject *s1n0 = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s1n0));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s1n0));

    // start-only
    CljObject *s2c = eval_string("(count (subvec [1 2 3 4] 2))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)s2c));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)s2c));

}

TEST(test_subvec_edge_cases) {
    TEST_ASSERT_NOT_NULL(st);

    // Normal cases
    // (subvec [1 2 3 4] 0 4) → [1 2 3 4] (complete vector)
    CljObject *full = eval_string("(subvec [1 2 3 4] 0 4)", st);
    TEST_ASSERT_NOT_NULL(full);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, full->type);
    CljObject *full_count = eval_string("(count (subvec [1 2 3 4] 0 4))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)full_count));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)full_count));

    // (subvec [1 2 3 4] 0 1) → [1] (first element)
    CljObject *first = eval_string("(subvec [1 2 3 4] 0 1)", st);
    TEST_ASSERT_NOT_NULL(first);
    CljObject *first_val = eval_string("(nth (subvec [1 2 3 4] 0 1) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)first_val));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)first_val));

    // (subvec [1 2 3 4] 3 4) → [4] (last element)
    CljObject *last = eval_string("(subvec [1 2 3 4] 3 4)", st);
    TEST_ASSERT_NOT_NULL(last);
    CljObject *last_val = eval_string("(nth (subvec [1 2 3 4] 3 4) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)last_val));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)last_val));

    // Edge cases
    // (subvec [] 0 0) → [] (empty vector)
    CljObject *empty = eval_string("(subvec [] 0 0)", st);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, empty->type);
    CljObject *empty_count = eval_string("(count (subvec [] 0 0))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_count));

    // (subvec [1 2 3] 1 1) → [] (start == end, empty result)
    CljObject *empty_result = eval_string("(subvec [1 2 3] 1 1)", st);
    TEST_ASSERT_NOT_NULL(empty_result);
    CljObject *empty_result_count = eval_string("(count (subvec [1 2 3] 1 1))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_result_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_result_count));

    // (subvec [1] 0 1) → [1] (single-element vector)
    CljObject *single = eval_string("(subvec [1] 0 1)", st);
    TEST_ASSERT_NOT_NULL(single);
    CljObject *single_val = eval_string("(nth (subvec [1] 0 1) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)single_val));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)single_val));

    // (subvec [1 2 3] 0) → [1 2 3] (from start, end missing)
    CljObject *from_start = eval_string("(subvec [1 2 3] 0)", st);
    TEST_ASSERT_NOT_NULL(from_start);
    CljObject *from_start_count = eval_string("(count (subvec [1 2 3] 0))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)from_start_count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)from_start_count));

    // (subvec [1 2 3] 0 3) → [1 2 3] (complete vector explicitly)
    CljObject *complete = eval_string("(subvec [1 2 3] 0 3)", st);
    TEST_ASSERT_NOT_NULL(complete);
    CljObject *complete_count = eval_string("(count (subvec [1 2 3] 0 3))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)complete_count));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)complete_count));

    // Nested/Integration
    // (nth (subvec [1 2 3 4] 1 3) 0) → 2 (subvec with nth)
    CljObject *nested_nth = eval_string("(nth (subvec [1 2 3 4] 1 3) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nested_nth));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nested_nth));

    // (count (subvec [1 2 3 4] 2)) → 2 (subvec with count)
    CljObject *nested_count = eval_string("(count (subvec [1 2 3 4] 2))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nested_count));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nested_count));

    // (subvec (subvec [1 2 3 4 5] 1 4) 0 2) → [2 3] (nested subvec)
    CljObject *nested_subvec = eval_string("(subvec (subvec [1 2 3 4 5] 1 4) 0 2)", st);
    TEST_ASSERT_NOT_NULL(nested_subvec);
    CljObject *nested_subvec_val = eval_string("(nth (subvec (subvec [1 2 3 4 5] 1 4) 0 2) 0)", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)nested_subvec_val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)nested_subvec_val));

}

TEST(test_subvec_error_cases) {
    TEST_ASSERT_NOT_NULL(st);

    // Index out of bounds: start < 0
    CljObject *result1 = NULL;
    TRY {
        result1 = eval_string("(subvec [1 2 3] -1 2)", st);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result1);  // Should fail

    // Index out of bounds: end > count
    CljObject *result2 = NULL;
    TRY {
        result2 = eval_string("(subvec [1 2 3] 0 4)", st);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result2);  // Should fail

    // Index out of bounds: start > end
    CljObject *result3 = NULL;
    TRY {
        result3 = eval_string("(subvec [1 2 3] 2 1)", st);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result3);  // Should fail

    // Index out of bounds: start > count
    CljObject *result4 = NULL;
    TRY {
        result4 = eval_string("(subvec [1 2 3] 4 5)", st);
    } CATCH(ex) {
        // Expected: IndexOutOfBoundsException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result4);  // Should fail

    // Type error: nil as vector
    CljObject *result5 = NULL;
    TRY {
        result5 = eval_string("(subvec nil 0 1)", st);
    } CATCH(ex) {
        // Expected: IllegalArgumentException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result5);  // Should fail

    // Type error: wrong type
    CljObject *result6 = NULL;
    TRY {
        result6 = eval_string("(subvec \"not-vector\" 0 1)", st);
    } CATCH(ex) {
        // Expected: IllegalArgumentException
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result6);  // Should fail

    // Arity error: only 1 argument
    CljObject *result7 = NULL;
    TRY {
        result7 = eval_string("(subvec [1 2 3])", st);
    } CATCH(ex) {
        // Expected: ArityError
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result7);  // Should fail

    // Arity error: 4 arguments
    CljObject *result8 = NULL;
    TRY {
        result8 = eval_string("(subvec [1 2 3] 0 1 2)", st);
    } CATCH(ex) {
        // Expected: ArityError
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_NULL(result8);  // Should fail

}

TEST(test_vec_from_list_and_vector_id) {
    TEST_ASSERT_NOT_NULL(st);

    // Test: vec converts list to vector
    // (vec '(1 2 3)) => [1 2 3]
    CljObject *v = eval_string("(vec '(1 2 3))", st);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v->type);
    CljObject *c = eval_string("(count (vec '(1 2 3)))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c));
    
    // Test: vec on vector is No-Op (returns same vector)
    // (vec [1 2 3]) => [1 2 3] (same object)
    CljObject *v1 = eval_string("(vec [1 2 3])", st);
    CljObject *v2 = eval_string("(vec [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(v1);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v1->type);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v2->type);
    // Note: They might be different objects (new evaluation), but same content
    CljObject *c1 = eval_string("(count (vec [1 2 3]))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c1));
    
    // Test: vec on empty list => empty vector
    // (vec '()) => []
    CljObject *empty = eval_string("(vec '())", st);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, empty->type);
    CljObject *empty_count = eval_string("(count (vec '()))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)empty_count));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)empty_count));

}

TEST(test_vec_with_nil_elements) {
    TEST_ASSERT_NOT_NULL(st);

    // Test: vec converts list with nil element to vector
    // (vec '(1 nil 3)) => [1 nil 3]
    CljObject *v = eval_string("(vec '(1 nil 3))", st);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v->type);
    
    // Check count
    CljObject *c = eval_string("(count (vec '(1 nil 3)))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c));
    
    // Check first element (should be 1)
    CljObject *first = eval_string("(nth (vec '(1 nil 3)) 0)", st);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)first));
    
    // Check second element (should be nil)
    CljObject *second = eval_string("(nth (vec '(1 nil 3)) 1)", st);
    TEST_ASSERT_NULL(second);  // nil is represented as NULL
    
    // Check third element (should be 3)
    CljObject *third = eval_string("(nth (vec '(1 nil 3)) 2)", st);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)third));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)third));
    
    // Test: vec on vector with nil element (using list syntax since vector literal may not parse nil correctly)
    // (vec '(nil 2 nil)) => [nil 2 nil]
    CljObject *v2 = eval_string("(vec '(nil 2 nil))", st);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, v2->type);
    CljObject *c2 = eval_string("(count (vec '(nil 2 nil)))", st);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)c2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)c2));
    
    // Check first element (should be nil)
    CljObject *first2 = eval_string("(nth (vec '(nil 2 nil)) 0)", st);
    TEST_ASSERT_NULL(first2);  // nil is represented as NULL
    
    // Check second element (should be 2)
    CljObject *second2 = eval_string("(nth (vec '(nil 2 nil)) 1)", st);
    TEST_ASSERT_NOT_NULL(second2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)second2));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)second2));
    
    // Check third element (should be nil)
    CljObject *third2 = eval_string("(nth (vec '(nil 2 nil)) 2)", st);
    TEST_ASSERT_NULL(third2);  // nil is represented as NULL

}

// ============================================================================
// Tests for vector_conj() COW implementation
// ============================================================================

// Test that vector_conj uses in-place mutation when RC=1
TEST(test_vector_conj_cow_rc_one_inplace) {
    WITH_AUTORELEASE_POOL({
        CljVector vec = make_vector(4, false);
        TEST_ASSERT_EQUAL(1, vec->base.rc);
        
        // First conj should be in-place (RC=1, capacity allows)
        CljValue new_vec1 = (CljValue)vector_conj((CljVector)vec, (ID)fixnum(10));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, new_vec1); // Same pointer!
        TEST_ASSERT_EQUAL(1, vec->base.rc);
        TEST_ASSERT_EQUAL_INT(1, vec->count);
        
        // Second conj should also be in-place
        CljValue new_vec2 = (CljValue)vector_conj((CljVector)vec, (ID)fixnum(20));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, new_vec2); // Same pointer!
        TEST_ASSERT_EQUAL(1, vec->base.rc);
        TEST_ASSERT_EQUAL_INT(2, vec->count);
        
        // Verify entries
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vec->data[0]));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vec->data[1]));
    });
}

// Test that vector_conj uses Copy-on-Write when RC>1
TEST(test_vector_conj_cow_rc_greater_one) {
    WITH_AUTORELEASE_POOL({
        CljVector vec = make_vector(4, false);
        TEST_ASSERT_EQUAL(1, vec->base.rc);
        
        // Add some entries
        vector_conj((CljVector)vec, (ID)fixnum(10));
        
        // RETAIN to increase RC
        RETAIN((CljValue)vec);
        TEST_ASSERT_EQUAL(2, vec->base.rc);
        
        // Now COW should trigger
        CljValue new_vec = (CljValue)vector_conj((CljVector)vec, (ID)fixnum(20));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, new_vec); // NEW pointer!
        TEST_ASSERT_EQUAL(2, vec->base.rc); // Original RC unchanged
        
        // Verify original vector unchanged
        TEST_ASSERT_EQUAL_INT(1, vec->count);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vec->data[0]));
        
        // Verify new vector has both entries
        CljPersistentVector *new_vec_data = as_vector(new_vec);
        TEST_ASSERT_EQUAL_INT(2, new_vec_data->count);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)new_vec_data->data[0]));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)new_vec_data->data[1]));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
    });
}

// Test that vector_conj handles capacity growth with COW
TEST(test_vector_conj_cow_capacity_growth) {
    WITH_AUTORELEASE_POOL({
        CljPersistentVector *vec = (CljPersistentVector*)make_vector(2, false);
        TEST_ASSERT_EQUAL(1, vec->base.rc);
        
        // Fill capacity
        vector_conj((CljVector)vec, (ID)fixnum(10));
        vector_conj((CljVector)vec, (ID)fixnum(20));
        TEST_ASSERT_EQUAL_INT(2, vec->count);
        TEST_ASSERT_EQUAL_INT(2, vec->capacity);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)vec);
        
        // Add more - should trigger COW with growth
        CljValue new_vec = (CljValue)vector_conj((CljVector)vec, (ID)fixnum(30));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, new_vec); // NEW pointer!
        
        CljPersistentVector *new_vec_data = as_vector(new_vec);
        // Capacity should be grown (2 * 2 = 4)
        TEST_ASSERT_EQUAL_INT(4, new_vec_data->capacity);
        TEST_ASSERT_EQUAL_INT(3, new_vec_data->count);
        
        // Verify all entries
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)new_vec_data->data[0]));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)new_vec_data->data[1]));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)new_vec_data->data[2]));
        
        // Original unchanged
        TEST_ASSERT_EQUAL_INT(2, vec->count);
        TEST_ASSERT_EQUAL_INT(2, vec->capacity);
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
    });
}

// Test that original vector remains unchanged after COW
TEST(test_vector_conj_cow_original_unchanged) {
    WITH_AUTORELEASE_POOL({
        CljVector vec = make_vector(4, false);
        
        // Add entries
        vector_conj((CljVector)vec, (ID)fixnum(10));
        vector_conj((CljVector)vec, (ID)fixnum(20));
        TEST_ASSERT_EQUAL_INT(2, vec->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)vec);
        CljValue new_vec = (CljValue)vector_conj((CljVector)vec, (ID)fixnum(30));
        
        // Original should be unchanged
        TEST_ASSERT_EQUAL_INT(2, vec->count);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vec->data[0]));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vec->data[1]));
        
        // New vector should have all entries
        CljPersistentVector *new_vec_data = as_vector(new_vec);
        TEST_ASSERT_EQUAL_INT(3, new_vec_data->count);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)new_vec_data->data[0]));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)new_vec_data->data[1]));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)new_vec_data->data[2]));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
    });
}

// Test memory leak detection for vector_conj COW
TEST(test_vector_conj_cow_memory_leak) {
    WITH_MEMORY_PROFILING({
        CljVector vec = make_vector(4, false);
        
        // Add entries
        vector_conj((CljVector)vec, (ID)fixnum(10));
        vector_conj((CljVector)vec, (ID)fixnum(20));
        vector_conj((CljVector)vec, (ID)fixnum(30));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)vec);
        CljValue new_vec = (CljValue)vector_conj((CljVector)vec, (ID)fixnum(40));
        
        // Cleanup
        RELEASE((CljValue)vec);
        RELEASE(new_vec);
        
        // Memory should be clean (no leaks)
        printf("✓ Keine Memory Leaks bei vector_conj COW-Operationen\n");
    });
}

