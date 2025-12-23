#include "test_common.h"

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
    CljVector *pool = autorelease_pool_push();
    TEST_ASSERT_NOT_NULL(pool);

    CljString *s = make_string("autorelease");
    RETAIN(s); // keep handle after draining
    int before = REFERENCE_COUNT(s);

    AUTORELEASE(s);
    autorelease_pool_pop(pool);

    TEST_ASSERT_EQUAL_INT(before, REFERENCE_COUNT(s));
    RELEASE(s);
}
