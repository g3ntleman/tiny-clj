/*
 * Unit tests for memory macros (RETAIN/RELEASE/AUTORELEASE)
 *
 * These macros are expected to be NULL-safe and immediate-safe.
 */

#include "tests_common.h"
#include <subjective-c/value.h>
#include <subjective-c/memory.h>

TEST(test_memory_macros_null_safe) {
    // nil is represented as NULL; macros must tolerate this.
    TEST_ASSERT_NULL(RETAIN(NULL));
    TEST_ASSERT_NULL(RELEASE(NULL));
    TEST_ASSERT_NULL(AUTORELEASE(NULL));
}

TEST(test_memory_macros_immediate_safe) {
    // Immediates should pass through unchanged.
    ID v = fixnum(123);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)v));

    TEST_ASSERT_EQUAL_PTR(v, RETAIN(v));
    TEST_ASSERT_EQUAL_PTR(v, RELEASE(v));
    TEST_ASSERT_EQUAL_PTR(v, AUTORELEASE(v));
}
