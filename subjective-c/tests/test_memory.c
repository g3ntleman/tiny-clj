#include "test_common.h"

// Test nested autorelease pools with checkpoints
TEST(test_nested_autorelease_pools) {
    // Outer pool
    autorelease_pool_push();
    
    CljString *outer = make_string("outer");  // rc=1
    RETAIN(outer);  // rc=2, keep handle after pool pop
    AUTORELEASE(outer);  // adds to pool, no RC change
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(outer));
    
    // Inner pool
    autorelease_pool_push();
    
    CljString *inner = make_string("inner");  // rc=1
    RETAIN(inner);  // rc=2, keep handle
    AUTORELEASE(inner);  // adds to pool, no RC change
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(inner));
    
    // Pop inner pool - no RC changes (weak reference semantics)
    autorelease_pool_pop();
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(outer));  // outer unchanged
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(inner));  // inner unchanged
    
    // Pop outer pool - no RC changes
    autorelease_pool_pop();
    
    RELEASE(inner);  // rc=1
    RELEASE(outer);  // rc=1
    RELEASE(inner);  // rc=0, freed
    RELEASE(outer);  // rc=0, freed
}

// Test that pool resize works correctly
TEST(test_autorelease_pool_many_objects) {
    autorelease_pool_push();
    
    // Create many objects to trigger resize
    CljString *handles[100];
    for (int i = 0; i < 100; i++) {
        handles[i] = make_string("test");
        RETAIN(handles[i]);  // Keep handle
        AUTORELEASE(handles[i]);
    }
    
    autorelease_pool_pop();
    
    // All handles should still be valid (we retained them)
    for (int i = 0; i < 100; i++) {
        RELEASE(handles[i]);
    }
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
    autorelease_pool_push();

    CljString *s = make_string("autorelease");
    RETAIN(s); // keep handle after draining
    int before = REFERENCE_COUNT(s);

    AUTORELEASE(s);
    autorelease_pool_pop();

    TEST_ASSERT_EQUAL_INT(before, REFERENCE_COUNT(s));
    RELEASE(s);
}
