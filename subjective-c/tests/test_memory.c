#include "test_common.h"

TEST(test_nested_autorelease_pools) {
    CljString *outer = NULL, *inner = NULL;
    WITH_AUTORELEASE_POOL({
        outer = make_string("outer");
        RETAIN(outer);
        AUTORELEASE(outer);
        TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(outer));
        inner = make_string("inner");
        RETAIN(inner);
        AUTORELEASE(inner);
        TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(inner));
    });
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(outer));
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(inner));
    RELEASE(inner);
    RELEASE(outer);
}

TEST(test_autorelease_pool_many_objects) {
    CljString *handles[100];
    WITH_AUTORELEASE_POOL({
        for (int i = 0; i < 100; i++) {
            handles[i] = make_string("test");
            RETAIN(handles[i]);
            AUTORELEASE(handles[i]);
        }
    });
    for (int i = 0; i < 100; i++) RELEASE(handles[i]);
}

TEST(test_retain_release_reference_count) {
    CljString *s = make_string("retain-release");
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(s));

    RETAIN(s);
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(s));

    RELEASE(s);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(s));

    RELEASE(s); // final release
}

TEST(test_autorelease_pool_drains_objects) {
    CljString *s = NULL;
    WITH_AUTORELEASE_POOL({
        s = make_string("autorelease");
        RETAIN(s);
        AUTORELEASE(s);
    });
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(s));
    RELEASE(s);
}

// =============================================================================
// is_pointer_on_stack() Tests
// =============================================================================

TEST(test_is_pointer_on_stack_local_var) {
    int local_var = 42;
    TEST_ASSERT_TRUE(is_pointer_on_stack(&local_var));
}

TEST(test_is_pointer_on_stack_heap_object) {
    CljString *heap_obj = make_string("heap");
    TEST_ASSERT_FALSE(is_pointer_on_stack(heap_obj));
    RELEASE(heap_obj);
}

TEST(test_is_pointer_on_stack_null) {
    TEST_ASSERT_FALSE(is_pointer_on_stack(NULL));
}

TEST(test_is_pointer_on_stack_static_var) {
    static int static_var = 42;
    TEST_ASSERT_FALSE(is_pointer_on_stack(&static_var));
}

TEST(test_is_pointer_on_stack_caller_frame) {
    // Test that we can detect a variable from a caller's stack frame
    int caller_local = 123;
    
    // Simulate what happens in a nested function call
    // The caller's local should still be detected as on-stack
    volatile int *ptr = &caller_local;
    TEST_ASSERT_TRUE(is_pointer_on_stack((void*)ptr));
}
