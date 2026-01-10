/*
 * Unit tests for memory macros (RETAIN/RELEASE/AUTORELEASE)
 *
 * These macros are expected to be NULL-safe and immediate-safe.
 */

#include "tests_common.h"
#include "value.h"
#include "memory.h"

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

TEST(test_is_pointer_on_stack_local_var_highlevel) {
    int local_var = 7;
    TEST_ASSERT_TRUE(is_pointer_on_stack(&local_var));
}

TEST(test_is_pointer_on_stack_heap_obj_highlevel) {
    CljString *s = make_string("heap");
    TEST_ASSERT_FALSE(is_pointer_on_stack(s));
    RELEASE(s);
}

TEST(test_is_pointer_on_stack_null_highlevel) {
    TEST_ASSERT_FALSE(is_pointer_on_stack(NULL));
}

TEST(test_is_pointer_on_stack_static_highlevel) {
    static int static_var = 9;
    TEST_ASSERT_FALSE(is_pointer_on_stack(&static_var));
}
