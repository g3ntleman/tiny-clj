/*
 * Event-loop latency helper tests.
 */

#include "tests_common.h"
#include "../event_loop.h"

TEST(test_event_loop_time_until_next_timer_no_timer_returns_minus_one) {
    int t = event_loop_time_until_next_timer_ms();
    TEST_ASSERT_EQUAL_INT(-1, t);
}

TEST(test_event_loop_time_until_next_timer_for_scheduled_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID timer_id = eval_string("(schedule-periodic 30 1000 (fn [] 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(timer_id);
    TEST_ASSERT_TRUE(is_fixnum(timer_id));

    int t = event_loop_time_until_next_timer_ms();
    TEST_ASSERT_TRUE_MESSAGE(t >= 0, "scheduled timer should report non-negative remaining ms");
    TEST_ASSERT_TRUE_MESSAGE(t <= 60, "remaining ms should be close to configured delay");
}

TEST(test_event_loop_has_pending_tasks_tracks_zero_delay_schedule) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after clear");

    ID schedule_result = eval_string("(schedule 0 (fn [] 42))", g_test_eval_state);
    TEST_ASSERT_NULL_MESSAGE(schedule_result, "zero-delay schedule is expected to return nil");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(), "queue should contain immediate scheduled task");

    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued task");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after running one task");
}

TEST(test_event_loop_run_next_zero_delay_stress) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    for (int i = 0; i < 256; i++) {
        ID schedule_result = eval_string("(schedule 0 (fn [] 42))", g_test_eval_state);
        TEST_ASSERT_NULL_MESSAGE(schedule_result, "zero-delay schedule is expected to return nil");
        TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(), "queue should contain scheduled task");

        bool ran = event_loop_run_next(NULL, g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued task");
    }

    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after stress loop");
}

TEST(test_event_loop_time_until_next_timer_does_not_consume_timer_entry) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID timer_id_val = eval_string("(schedule-periodic 200 1000 (fn [] 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(timer_id_val);
    TEST_ASSERT_TRUE(is_fixnum(timer_id_val));
    int timer_id = (int)as_fixnum(timer_id_val);
    TEST_ASSERT_TRUE_MESSAGE(timer_id > 0, "timer id should be positive");

    for (int i = 0; i < 256; i++) {
        int t = event_loop_time_until_next_timer_ms();
        TEST_ASSERT_TRUE_MESSAGE(t >= 0, "time-until-next-timer should remain non-negative");
    }

    TEST_ASSERT_TRUE_MESSAGE(timer_cancel(timer_id), "timer should still exist and be cancellable");
    TEST_ASSERT_EQUAL_INT(-1, event_loop_time_until_next_timer_ms());
}
