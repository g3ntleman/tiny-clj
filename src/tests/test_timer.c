/*
 * Timer Tests using Unity Framework
 * 
 * Tests for schedule and schedule-periodic timer implementations.
 */

#include "tests_common.h"
#include "../event_loop.h"
#include "../channel.h"
// test_registry.h is included via tests_common.h (uses subjective-c test infrastructure)
#include <sys/time.h>
#include <unistd.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// TIMER TESTS
// ============================================================================

// Test that schedule creates a timer that executes after delay
TEST(test_schedule_executes_after_delay) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a timer that prints after 1ms
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule 1 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule should not throw exception");
        return;
    } END_TRY
    
    // schedule should return nil (like go blocks)
    TEST_ASSERT_NULL(result);
    
    // Wait a bit and run tasks - timer should execute
    usleep(5000); // 5ms
    
    // Run timer processing and task execution
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        return;
    } END_TRY
    
    // Task should have been executed
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);
    
    // Clean up: Run any remaining tasks to avoid memory leaks
    while (event_loop_run_next(NULL, g_test_eval_state)) {
        // Continue until queue is empty
    }
}

// Test that schedule with 0ms delay executes immediately
TEST(test_schedule_zero_delay_executes_immediately) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a timer with 0ms delay
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule 0 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NULL(result);
    
    // Timer should be ready immediately - run task
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        return;
    } END_TRY
    
    // Task should have been executed
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);
    
    // Clean up: Run any remaining tasks to avoid memory leaks
    while (event_loop_run_next(NULL, g_test_eval_state)) {
        // Continue until queue is empty
    }
}

// High-level regression: zero-delay timer callback with lexical SlotRef capture
// must execute without assertion/crash and must observe captured state.
TEST(test_schedule_zero_delay_lexical_capture_regression_high_level) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    event_loop_clear();

    // Keep observed state in a global atom, but capture it through a local let
    // symbol to exercise closure SlotRef resolution in async callback context.
    TRY {
        (void)eval_string(
            "(do "
            "  (def timer-reg-hits (atom 0)) "
            "  (let [hits timer-reg-hits] "
            "    (schedule 0 (fn [] (swap! hits inc)))))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("setup expression should not throw");
        return;
    } END_TRY

    // Execute queued timer callback (high-level event loop path).
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute scheduled callback");

    ID hits_val = NULL;
    TRY {
        hits_val = eval_string("(deref timer-reg-hits)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("reading timer-reg-hits should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(hits_val);
    TEST_ASSERT_TRUE(is_fixnum(hits_val));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, as_fixnum(hits_val),
        "captured lexical state should be incremented exactly once");

    event_loop_clear();
}

// Test that schedule-periodic creates a repeating timer
TEST(test_schedule_periodic_repeats) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a periodic timer that runs every 1ms, starting immediately
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule-periodic 0 1 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule-periodic should not throw exception");
        return;
    } END_TRY
    
    // schedule-periodic should return a timer ID (integer), not nil
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    
    // First execution should be immediate
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);
    
    // Wait for next period
    usleep(5000); // 5ms
    
    // Second execution should be ready
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);
    
    // Clean up: Run any remaining tasks to avoid memory leaks
    // Note: Periodic timer will re-schedule, so we need to clear it manually
    event_loop_clear();
}

// Test that schedule validates arguments
TEST(test_schedule_validates_arguments) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test with non-integer delay
    TRY {
        (void)eval_string("(schedule \"100\" (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        // Should throw exception
        TEST_PASS();
        return;
    } END_TRY
    
    // Should have thrown exception
    TEST_FAIL_MESSAGE("schedule should throw exception for non-integer delay");
}

// Test that schedule validates function argument
TEST(test_schedule_validates_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test with non-function
    TRY {
        (void)eval_string("(schedule 100 42)", g_test_eval_state);
    } CATCH(ex) {
        // Should throw exception
        TEST_PASS();
        return;
    } END_TRY
    
    // Should have thrown exception
    TEST_FAIL_MESSAGE("schedule should throw exception for non-function");
}

// Test that schedule-periodic validates arguments
TEST(test_schedule_periodic_validates_arguments) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test with non-integer delay
    TRY {
        (void)eval_string("(schedule-periodic \"0\" 100 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        // Should throw exception
        TEST_PASS();
        return;
    } END_TRY
    
    // Should have thrown exception
    TEST_FAIL_MESSAGE("schedule-periodic should throw exception for non-integer delay");
}

// Test that schedule-periodic returns a timer ID (integer)
TEST(test_schedule_periodic_returns_timer_id) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule-periodic 0 1 (fn [] (println \"Tick\")))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule-periodic should not throw exception");
        return;
    } END_TRY
    
    // schedule-periodic should return a timer ID (integer), not nil
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    
    // Timer ID should be a positive integer
    int timer_id = as_fixnum((CljValue)result);
    TEST_ASSERT_TRUE(timer_id > 0);
    
    // Clean up
    event_loop_clear();
}

// Test that cancel-timer stops a periodic timer
TEST(test_cancel_timer_stops_periodic_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a periodic timer
    CljObject *timer_id_obj = NULL;
    TRY {
        timer_id_obj = eval_string("(schedule-periodic 0 1 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule-periodic should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(timer_id_obj);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)timer_id_obj));
    
    // Cancel the timer
    char cancel_expr[256];
    int timer_id = as_fixnum((CljValue)timer_id_obj);
    test_snprintf(cancel_expr, sizeof(cancel_expr), "(cancel-timer %d)", timer_id);
    
    CljObject *cancel_result = NULL;
    TRY {
        cancel_result = eval_string(cancel_expr, g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("cancel-timer should not throw exception");
        return;
    } END_TRY
    
    // cancel-timer should return true
    TEST_ASSERT_NOT_NULL(cancel_result);
    TEST_ASSERT_TRUE(is_special((CljValue)cancel_result));
    TEST_ASSERT_TRUE(as_special((CljValue)cancel_result) == SPECIAL_TRUE);
    
    // Wait a bit - timer should not execute anymore
    usleep(5000); // 5ms
    
    // Try to run task - should be empty (timer was cancelled)
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        return;
    } END_TRY
    
    // Should return false (no task to run)
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_FALSE);
    
    // Clean up
    event_loop_clear();
}

// Test that cancel-timer returns true when timer is found
TEST(test_cancel_timer_returns_true_when_found) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a periodic timer
    CljObject *timer_id_obj = NULL;
    TRY {
        timer_id_obj = eval_string("(schedule-periodic 0 10 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule-periodic should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(timer_id_obj);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)timer_id_obj));
    
    int timer_id = as_fixnum((CljValue)timer_id_obj);
    char cancel_expr[256];
    test_snprintf(cancel_expr, sizeof(cancel_expr), "(cancel-timer %d)", timer_id);
    
    CljObject *cancel_result = NULL;
    TRY {
        cancel_result = eval_string(cancel_expr, g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("cancel-timer should not throw exception");
        return;
    } END_TRY
    
    // Should return true
    TEST_ASSERT_NOT_NULL(cancel_result);
    TEST_ASSERT_TRUE(is_special((CljValue)cancel_result));
    TEST_ASSERT_TRUE(as_special((CljValue)cancel_result) == SPECIAL_TRUE);
    
    // Clean up
    event_loop_clear();
}

// Test that cancel-timer returns false when timer is not found
TEST(test_cancel_timer_returns_false_when_not_found) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Try to cancel a non-existent timer
    CljObject *cancel_result = NULL;
    TRY {
        cancel_result = eval_string("(cancel-timer 99999)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("cancel-timer should not throw exception");
        return;
    } END_TRY
    
    // Should return false
    TEST_ASSERT_NOT_NULL(cancel_result);
    TEST_ASSERT_TRUE(is_special((CljValue)cancel_result));
    TEST_ASSERT_TRUE(as_special((CljValue)cancel_result) == SPECIAL_FALSE);
}

// Test that cancel-timer validates argument
TEST(test_cancel_timer_validates_argument) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Non-integer argument is treated as named timer key.
    // Unknown key should not throw and should return false.
    CljObject *cancel_result = NULL;
    TRY {
        cancel_result = eval_string("(cancel-timer \"123\")", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("cancel-timer should not throw for named key");
        return;
    } END_TRY
    TEST_ASSERT_NOT_NULL(cancel_result);
    TEST_ASSERT_TRUE(is_special((CljValue)cancel_result));
    TEST_ASSERT_TRUE(as_special((CljValue)cancel_result) == SPECIAL_FALSE);
}

TEST(test_schedule_options_id_reuses_named_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(schedule 50 {:fn (fn [] 1) :id \"debounce\"})", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule with options map should not throw");
        return;
    } END_TRY

    TRY {
        (void)eval_string("(schedule 1 {:fn (fn [] 2) :id \"debounce\"})", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("second schedule upsert should not throw");
        return;
    } END_TRY

    usleep(5000);
    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_FALSE(event_loop_run_next(NULL, g_test_eval_state));
}

TEST(test_cancel_timer_accepts_named_key) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(schedule 0 {:fn (fn [] 42) :id :periodic :period-ms 1})", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule periodic via options should not throw");
        return;
    } END_TRY

    CljObject *cancel_result = NULL;
    TRY {
        cancel_result = eval_string("(cancel-timer :periodic)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("cancel-timer by key should not throw");
        return;
    } END_TRY
    TEST_ASSERT_NOT_NULL(cancel_result);
    TEST_ASSERT_TRUE(is_special((CljValue)cancel_result));
    TEST_ASSERT_TRUE(as_special((CljValue)cancel_result) == SPECIAL_TRUE);

    usleep(5000);
    TEST_ASSERT_FALSE(event_loop_run_next(NULL, g_test_eval_state));
}

// Test that timer_enqueue with delay 0 correctly enqueues task
TEST(test_timer_enqueue_zero_delay_enqueues_task) {
    event_loop_clear();

    CljObject *fn = eval_string("(fn [] 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    RETAIN(fn);

    CljTransientVector *task_queue = g_runtime.task_queue;
    CljPersistentVector *task_vec = task_queue ? vector_persistent(task_queue) : NULL;
    TEST_ASSERT_NOT_NULL(task_vec);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, vector_count(task_vec),
        "Initial count should be 0");

    int32_t timer_id = timer_enqueue(fn, 0, false, 0);
    TEST_ASSERT_TRUE_MESSAGE(timer_id > 0,
        "timer_enqueue should return a valid timer ID");

    task_queue = g_runtime.task_queue;
    task_vec = task_queue ? vector_persistent(task_queue) : NULL;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, vector_count(task_vec),
        "timer_enqueue with delay 0 should increment count from 0 to 1");

    ID *data = vector_as_array(task_vec);
    TEST_ASSERT_NOT_NULL_MESSAGE(data,
        "vector_as_array should return non-NULL after timer_enqueue");

    TEST_ASSERT_NOT_NULL_MESSAGE(data[0],
        "data[0] should contain the enqueued task");

    if (g_runtime.task_queue) {
        vector_remove_at(g_runtime.task_queue, 0);
    }
    RELEASE(fn);
    // fn is autoreleased from eval_string; TEST() pool handles cleanup.
}

// Test that timer_enqueue with delay 0 works correctly with event_loop_run_next
TEST(test_timer_enqueue_zero_delay_with_run_next) {
    event_loop_clear();

    CljObject *fn = eval_string("(fn [] 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    RETAIN(fn);

    CljTransientVector *task_queue = g_runtime.task_queue;
    CljPersistentVector *task_vec = task_queue ? vector_persistent(task_queue) : NULL;
    TEST_ASSERT_NOT_NULL(task_vec);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, vector_count(task_vec),
        "Initial count should be 0");

    int32_t timer_id = timer_enqueue(fn, 0, false, 0);
    TEST_ASSERT_TRUE_MESSAGE(timer_id > 0,
        "timer_enqueue should return a valid timer ID");

    task_queue = g_runtime.task_queue;
    task_vec = task_queue ? vector_persistent(task_queue) : NULL;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, vector_count(task_vec),
        "timer_enqueue with delay 0 should increment count from 0 to 1");

    ID *data = vector_as_array(task_vec);
    TEST_ASSERT_NOT_NULL_MESSAGE(data,
        "vector_as_array should return non-NULL after timer_enqueue");

    task_queue = g_runtime.task_queue;
    CljPersistentVector *task_vec_fresh = task_queue ? vector_persistent(task_queue) : NULL;
    TEST_ASSERT_NOT_NULL(task_vec_fresh);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, vector_count(task_vec_fresh),
        "Count should be 1 before event_loop_run_next");

    if (g_runtime.task_queue) {
        vector_remove_at(g_runtime.task_queue, 0);
    }
    RELEASE(fn);
    // fn is autoreleased from eval_string; TEST() pool handles cleanup.
}
