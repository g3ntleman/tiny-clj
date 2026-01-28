#include "test_common.h"

static CljVector* make_vector_from_ints(const int *values, size_t count) {
    CljVector *vec = make_vector(0, CLJ_VECTOR_PERSISTENT);
    for (size_t i = 0; i < count; ++i) {
        CljVector *updated = vector_conj_owned(vec, fixnum(values[i]));
        if (updated != vec) {
            if (vec != empty_vector()) RELEASE(vec);
            vec = updated;
        }
    }
    return vec;
}

TEST(test_vector_make_and_count) {
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_UINT(0, vector_count(vec));
    CljVector *vec2 = vector_conj(vec, fixnum(10));
    if (vec2 != vec) {
        RELEASE(vec);
        vec = vec2;
    }
    TEST_ASSERT_EQUAL_UINT(1, vector_count(vec));
    RELEASE(vec);
}

TEST(test_vector_nth_returns_elements) {
    int values[] = {1, 2, 3};
    CljVector *vec = make_vector_from_ints(values, 3);
    CljValue first = vector_nth(vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first));
    CljValue last = vector_nth(vec, 2);
    TEST_ASSERT_TRUE(is_fixnum(last));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(last));
    RELEASE(vec);
}

TEST(test_vector_set_nth_on_transient) {
    int values[] = {4, 5, 6};
    CljVector *vec = make_vector_from_ints(values, 3);
    CljVector *transient = vector_transient(vec);
    if (transient != vec) {
        RELEASE(vec);
        vec = transient;
    }
    vec = vector_set_nth(vec, 1, fixnum(99));
    CljValue mid = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(mid));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(mid));
    RELEASE(vec);
}

TEST(test_vector_pop_and_insert) {
    int values[] = {7, 8, 9};
    CljVector *vec = make_vector_from_ints(values, 3);
    vec = vector_insert_at(vec, 1, fixnum(100));
    TEST_ASSERT_EQUAL_UINT(4, vector_count(vec));
    CljValue inserted = vector_nth(vec, 1);
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(inserted));
    CljVector *popped = vector_pop(vec);
    if (popped != vec) {
        RELEASE(vec);
        vec = popped;
    }
    TEST_ASSERT_EQUAL_UINT(3, vector_count(vec));
    RELEASE(vec);
}

TEST(test_vector_nil_as_value_conj) {
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    
    // Add nil (NULL) as value using conj
    CljVector *updated = vector_conj(vec, NULL);
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(1, vector_count(vec));
    
    // Get nil value
    CljValue result = vector_nth(vec, 0);
    TEST_ASSERT_NULL(result);
    
    // Add non-nil value
    updated = vector_conj(vec, fixnum(42));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Verify nil is still there
    CljValue nil_result = vector_nth(vec, 0);
    TEST_ASSERT_NIL(nil_result);
    
    // Verify non-nil value
    CljValue non_nil_result = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(non_nil_result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(non_nil_result));
    
    RELEASE(vec);
}

TEST(test_vector_nil_as_value_assoc) {
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    
    // Add some values first
    CljVector *updated = vector_conj(vec, fixnum(10));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, fixnum(20));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Set nil (NULL) as value at index 0 using assoc
    updated = vector_assoc(vec, 0, NULL);
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    
    // Get nil value
    CljValue result = vector_nth(vec, 0);
    TEST_ASSERT_NULL(result);
    
    // Verify other value is still there
    CljValue other_result = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(other_result));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(other_result));
    
    RELEASE(vec);
}

TEST(test_vector_nil_as_value_insert_at) {
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    
    // Add some values first
    CljVector *updated = vector_conj(vec, fixnum(10));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, fixnum(20));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Insert nil (NULL) as value at index 1
    updated = vector_insert_at(vec, 1, NULL);
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(3, vector_count(vec));
    
    // Verify nil value at index 1
    CljValue result = vector_nth(vec, 1);
    TEST_ASSERT_NULL(result);
    
    // Verify other values are still there
    CljValue first = vector_nth(vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(first));
    
    CljValue last = vector_nth(vec, 2);
    TEST_ASSERT_TRUE(is_fixnum(last));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(last));
    
    RELEASE(vec);
}

TEST(test_vector_nil_as_value_remove_at) {
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    
    // Add values including nil
    CljVector *updated = vector_conj(vec, fixnum(10));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, NULL);
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, fixnum(20));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(3, vector_count(vec));
    
    // Remove nil value at index 1
    updated = vector_remove_at(vec, 1);
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Verify nil is gone
    CljValue first = vector_nth(vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(first));
    
    CljValue second = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(second));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(second));
    
    RELEASE(vec);
}

TEST(test_vector_nil_as_value_set_nth) {
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    
    // Add some values first
    CljVector *updated = vector_conj(vec, fixnum(10));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, fixnum(20));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    
    // Convert to transient for set_nth
    CljVector *transient = vector_transient(vec);
    if (transient != vec) {
        RELEASE(vec);
        vec = transient;
    }
    
    // Set nil (NULL) as value at index 0 using set_nth
    vec = vector_set_nth(vec, 0, NULL);
    
    // Get nil value
    CljValue result = vector_nth(vec, 0);
    TEST_ASSERT_NULL(result);
    
    // Verify other value is still there
    CljValue other_result = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(other_result));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(other_result));
    
    RELEASE(vec);
}

TEST(test_vector_assoc_append_on_transient) {
    // Create persistent vector with some values
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    CljVector *updated = vector_conj(vec, fixnum(10));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, fixnum(20));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Convert to transient
    CljVector *transient = vector_transient(vec);
    if (transient != vec) {
        RELEASE(vec);
        vec = transient;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Use assoc with index == count to append (transient behavior)
    unsigned int count_before = vector_count(vec);
    updated = vector_assoc(vec, count_before, fixnum(30));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    
    // Verify count increased
    TEST_ASSERT_EQUAL_UINT(3, vector_count(vec));
    
    // Verify existing values are still there
    CljValue first = vector_nth(vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(first));
    
    CljValue second = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(second));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(second));
    
    // Verify appended value
    CljValue appended = vector_nth(vec, 2);
    TEST_ASSERT_TRUE(is_fixnum(appended));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(appended));
    
    RELEASE(vec);
}

TEST(test_vector_assoc_append_fails_on_persistent) {
    // Create persistent vector with some values
    CljVector *vec = make_vector(4, CLJ_VECTOR_PERSISTENT);
    CljVector *updated = vector_conj(vec, fixnum(10));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    updated = vector_conj(vec, fixnum(20));
    if (updated != vec) {
        RELEASE(vec);
        vec = updated;
    }
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Try to use assoc with index == count on persistent vector (should fail)
    CLJException *caught_ex = NULL;
    unsigned int count = vector_count(vec);
    
    TRY {
        updated = vector_assoc(vec, count, fixnum(30));
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here - should throw IndexOutOfBoundsException");
        if (updated != vec) {
            RELEASE(updated);
        }
    } CATCH(ex) {
        caught_ex = ex;
        TEST_ASSERT_NOT_NULL(caught_ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_INDEX_OUT_OF_BOUNDS, caught_ex->type);
        TEST_ASSERT_NOT_NULL(strstr(caught_ex->message, "vector_assoc"));
    } END_TRY
    
    // Verify exception was thrown
    TEST_ASSERT_NOT_NULL(caught_ex);
    
    // Verify vector is unchanged
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    CljValue first = vector_nth(vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(first));
    
    CljValue second = vector_nth(vec, 1);
    TEST_ASSERT_TRUE(is_fixnum(second));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(second));
    
    RELEASE(vec);
}

// === Tests for _inplace functions ===

TEST(test_vector_conj_inplace_cow_rc_one) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    
    ID item1 = fixnum(10);
    ID item2 = fixnum(20);
    
    // RC=1: should mutate in-place
    CljVector *vec_before = vec;
    vector_conj_inplace(&vec, item1);
    TEST_ASSERT_EQUAL_PTR(vec_before, vec);  // Same pointer (in-place)
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(1, vector_count(vec));
    
    vector_conj_inplace(&vec, item2);
    TEST_ASSERT_EQUAL_PTR(vec_before, vec);  // Still same pointer
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Verify values
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(vector_nth(vec, 1)));
    
    RELEASE(vec);
}

TEST(test_vector_conj_inplace_cow_rc_greater_one) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    
    ID item1 = fixnum(10);
    ID item2 = fixnum(20);
    
    // Create second reference (RC>1)
    RETAIN(vec);
    CljVector *copy = vec;
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(vec));
    
    // RC>1: should create new vector (COW)
    CljVector *vec_before = vec;
    vector_conj_inplace(&vec, item1);
    TEST_ASSERT_NOT_EQUAL(vec_before, vec);  // Different pointer (COW)
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(1, vector_count(vec));
    
    // Copy should be unchanged
    TEST_ASSERT_EQUAL_UINT(0, vector_count(copy));
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(copy));
    
    vector_conj_inplace(&vec, item2);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // Verify values
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(vector_nth(vec, 1)));
    
    RELEASE(vec);
    RELEASE(copy);
}

TEST(test_vector_assoc_inplace_cow_rc_one) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    vector_conj_inplace(&vec, fixnum(10));
    vector_conj_inplace(&vec, fixnum(20));
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    
    // RC=1: should mutate in-place
    CljVector *vec_before = vec;
    vector_assoc_inplace(&vec, 0, fixnum(99));
    TEST_ASSERT_EQUAL_PTR(vec_before, vec);  // Same pointer (in-place)
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(vector_nth(vec, 1)));
    
    RELEASE(vec);
}

TEST(test_vector_assoc_inplace_cow_rc_greater_one) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    vector_conj_inplace(&vec, fixnum(10));
    vector_conj_inplace(&vec, fixnum(20));
    
    // Create second reference (RC>1)
    RETAIN(vec);
    CljVector *copy = vec;
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(vec));
    
    // RC>1: should create new vector (COW)
    CljVector *vec_before = vec;
    vector_assoc_inplace(&vec, 0, fixnum(99));
    TEST_ASSERT_NOT_EQUAL(vec_before, vec);  // Different pointer (COW)
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(vector_nth(vec, 0)));
    
    // Copy should be unchanged
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(copy, 0)));
    
    RELEASE(vec);
    RELEASE(copy);
}

TEST(test_vector_pop_inplace_cow_rc_one) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    vector_conj_inplace(&vec, fixnum(10));
    vector_conj_inplace(&vec, fixnum(20));
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(2, vector_count(vec));
    
    // RC=1: should mutate in-place
    CljVector *vec_before = vec;
    vector_pop_inplace(&vec);
    TEST_ASSERT_EQUAL_PTR(vec_before, vec);  // Same pointer (in-place)
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(1, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(vector_nth(vec, 0)));
    
    RELEASE(vec);
}

TEST(test_vector_pop_inplace_cow_rc_greater_one) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    vector_conj_inplace(&vec, fixnum(10));
    vector_conj_inplace(&vec, fixnum(20));
    
    // Create second reference (RC>1)
    RETAIN(vec);
    CljVector *copy = vec;
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(vec));
    
    // RC>1: should create new vector (COW)
    CljVector *vec_before = vec;
    vector_pop_inplace(&vec);
    TEST_ASSERT_NOT_EQUAL(vec_before, vec);  // Different pointer (COW)
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(1, vector_count(vec));
    
    // Copy should be unchanged
    TEST_ASSERT_EQUAL_UINT(2, vector_count(copy));
    
    RELEASE(vec);
    RELEASE(copy);
}

TEST(test_vector_inplace_memory_management) {
    CljVector *vec = make_vector(10, CLJ_VECTOR_PERSISTENT);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    
    // Add items using inplace
    for (int i = 0; i < 5; i++) {
        vector_conj_inplace(&vec, fixnum(i));
        TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));  // RC should stay 1
    }
    
    TEST_ASSERT_EQUAL_UINT(5, vector_count(vec));
    
    // Remove items using inplace
    vector_remove_at_inplace(&vec, 2);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(4, vector_count(vec));
    
    // Insert using inplace
    vector_insert_at_inplace(&vec, 2, fixnum(99));
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(vec));
    TEST_ASSERT_EQUAL_UINT(5, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(vector_nth(vec, 2)));
    
    RELEASE(vec);
}

