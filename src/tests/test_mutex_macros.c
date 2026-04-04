/*
 * Unit tests for generic WITH_MUTEX macro in subjective-c/common.h.
 */

#include "tests_common.h"

static bool g_test_lock_held = false;
static int g_test_lock_acquire_count = 0;
static int g_test_lock_release_count = 0;
static int g_test_lock_protocol_errors = 0;

static void test_mutex_probe_reset(void) {
    g_test_lock_held = false;
    g_test_lock_acquire_count = 0;
    g_test_lock_release_count = 0;
    g_test_lock_protocol_errors = 0;
}

static void test_mutex_probe_lock_acquire(void) {
    if (g_test_lock_held) {
        g_test_lock_protocol_errors++;
    }
    g_test_lock_held = true;
    g_test_lock_acquire_count++;
}

static void test_mutex_probe_lock_release(void) {
    if (!g_test_lock_held) {
        g_test_lock_protocol_errors++;
    }
    g_test_lock_held = false;
    g_test_lock_release_count++;
}

static int test_mutex_probe_helper_return_inside(bool early_return) {
    WITH_MUTEX(test_mutex_probe_lock) {
        if (early_return) {
            return 7;
        }
    }
    return 11;
}

TEST(test_with_mutex_balances_acquire_release_once) {
    test_mutex_probe_reset();

    int value = 0;
    WITH_MUTEX(test_mutex_probe_lock) {
        value = 42;
    }

    TEST_ASSERT_EQUAL_INT(42, value);
    TEST_ASSERT_EQUAL_INT(1, g_test_lock_acquire_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_lock_release_count);
    TEST_ASSERT_FALSE(g_test_lock_held);
    TEST_ASSERT_EQUAL_INT(0, g_test_lock_protocol_errors);
}

TEST(test_with_mutex_runs_body_once) {
    test_mutex_probe_reset();

    int counter = 0;
    WITH_MUTEX(test_mutex_probe_lock) {
        counter++;
    }

    TEST_ASSERT_EQUAL_INT(1, counter);
    TEST_ASSERT_EQUAL_INT(1, g_test_lock_acquire_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_lock_release_count);
    TEST_ASSERT_EQUAL_INT(0, g_test_lock_protocol_errors);
}

#if CLJ_HAS_CLEANUP_ATTRIBUTE
TEST(test_with_mutex_releases_on_early_return) {
    test_mutex_probe_reset();

    int result = test_mutex_probe_helper_return_inside(true);
    TEST_ASSERT_EQUAL_INT(7, result);
    TEST_ASSERT_EQUAL_INT(1, g_test_lock_acquire_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_lock_release_count);
    TEST_ASSERT_FALSE(g_test_lock_held);
    TEST_ASSERT_EQUAL_INT(0, g_test_lock_protocol_errors);
}
#else
TEST(test_with_mutex_releases_on_early_return) {
    TEST_IGNORE_MESSAGE("Compiler without cleanup attribute: early-return unlock is not guaranteed.");
}
#endif
