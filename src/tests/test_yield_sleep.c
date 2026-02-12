#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 50
#include "tests_common.h"

// Test hook controls implemented in test_yield_sleep_hooks.c
void test_yield_sleep_hooks_enable(uint32_t start_ms);
void test_yield_sleep_hooks_disable(void);
unsigned int test_yield_sleep_yield_calls(void);
unsigned int test_yield_sleep_last_timeout_ms(void);
uint32_t test_yield_sleep_now_ms(void);

TEST_SHARED(test_yield_zero_returns_nil_and_runs_once) {
    test_yield_sleep_hooks_enable(123);

    CljValue result = eval_string("(yield 0)", g_test_eval_state);
    TEST_ASSERT_NIL(result);
    TEST_ASSERT_EQUAL_UINT(1u, test_yield_sleep_yield_calls());
    TEST_ASSERT_EQUAL_UINT(0u, test_yield_sleep_last_timeout_ms());
    TEST_ASSERT_EQUAL_UINT32(123u, test_yield_sleep_now_ms());

    test_yield_sleep_hooks_disable();
}

TEST_SHARED(test_current_time_ms_returns_fixnum_and_matches_override) {
    test_yield_sleep_hooks_enable(456);

    CljValue result = eval_string("(current-time-ms)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(456, as_fixnum(result));

    test_yield_sleep_hooks_disable();
}

TEST_SHARED(test_sleep_advances_time_and_returns_nil) {
    test_yield_sleep_hooks_enable(10);

    CljValue result = eval_string("(sleep 25)", g_test_eval_state);
    TEST_ASSERT_NIL(result);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(1u, test_yield_sleep_yield_calls());
    TEST_ASSERT_EQUAL_UINT32(35u, test_yield_sleep_now_ms());

    test_yield_sleep_hooks_disable();
}

TEST_SHARED(test_sleep_wraps_over_24h_boundary) {
    test_yield_sleep_hooks_enable(86399990u);

    CljValue result = eval_string("(sleep 20)", g_test_eval_state);
    TEST_ASSERT_NIL(result);
    TEST_ASSERT_EQUAL_UINT32(10u, test_yield_sleep_now_ms());

    test_yield_sleep_hooks_disable();
}

