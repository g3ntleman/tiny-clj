#include "test_common.h"

static CljVector* make_vector_from_ints(const int *values, size_t count) {
    CljVector *vec = make_vector(0, CLJ_VECTOR);
    for (size_t i = 0; i < count; ++i) {
        CljVector *updated = vector_conj(vec, fixnum(values[i]));
        if (updated != vec) {
            RELEASE(vec);
            vec = updated;
        }
    }
    return vec;
}

TEST(test_vector_make_and_count) {
    CljVector *vec = make_vector(4, CLJ_VECTOR);
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

