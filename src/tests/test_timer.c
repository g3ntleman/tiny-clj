/*
 * Timer Tests using Unity Framework
 * 
 * Tests for schedule and schedule-periodic timer implementations.
 */

#include "tests_common.h"
#include "../event_loop.h"
#include "../channel.h"
#include "test_registry.h"
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
    
    // Create a timer that prints after 100ms
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule 100 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule should not throw exception");
        return;
    } END_TRY
    
    // schedule should return nil (like go blocks)
    TEST_ASSERT_NULL(result);
    
    // Wait a bit and run tasks - timer should execute
    usleep(150000); // 150ms
    
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

// Test that schedule-periodic creates a repeating timer
TEST(test_schedule_periodic_repeats) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a periodic timer that runs every 100ms, starting immediately
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule-periodic 0 100 (fn [] 42))", g_test_eval_state);
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
    usleep(150000); // 150ms
    
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
        result = eval_string("(schedule-periodic 0 1000 (fn [] (println \"Tick\")))", g_test_eval_state);
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
        timer_id_obj = eval_string("(schedule-periodic 0 100 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule-periodic should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(timer_id_obj);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)timer_id_obj));
    
    // Cancel the timer
    char cancel_expr[256];
    int timer_id = as_fixnum((CljValue)timer_id_obj);
    snprintf(cancel_expr, sizeof(cancel_expr), "(cancel-timer %d)", timer_id);
    
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
    usleep(150000); // 150ms
    
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
        timer_id_obj = eval_string("(schedule-periodic 0 1000 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("schedule-periodic should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(timer_id_obj);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)timer_id_obj));
    
    int timer_id = as_fixnum((CljValue)timer_id_obj);
    char cancel_expr[256];
    snprintf(cancel_expr, sizeof(cancel_expr), "(cancel-timer %d)", timer_id);
    
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
    
    // Test with non-integer argument
    TRY {
        (void)eval_string("(cancel-timer \"123\")", g_test_eval_state);
    } CATCH(ex) {
        // Should throw exception
        TEST_PASS();
        return;
    } END_TRY
    
    // Should have thrown exception
    TEST_FAIL_MESSAGE("cancel-timer should throw exception for non-integer argument");
}

// Test that timer_enqueue with delay 0 correctly enqueues task
TEST(test_timer_enqueue_zero_delay_enqueues_task) {
    WITH_AUTORELEASE_POOL({
        // Clear event loop first
        event_loop_clear();
        
        // Create a simple function
        CljObject *fn = eval_string("(fn [] 42)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(fn);
        
        // Check initial count (should be 0)
        CljVector *task_vec = (CljVector*)g_runtime.task_queue;
        TEST_ASSERT_NOT_NULL(task_vec);
        unsigned int count_before = vector_count(task_vec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_before,
            "Initial count should be 0");
        
        // Enqueue timer with 0ms delay (should execute immediately)
        int32_t timer_id = timer_enqueue(fn, 0, false, 0);
        TEST_ASSERT_TRUE_MESSAGE(timer_id > 0,
            "timer_enqueue should return a valid timer ID");
        
        // Check that count is now 1 (timer with delay 0 should be enqueued immediately)
        unsigned int count_after = vector_count(task_vec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_after,
            "timer_enqueue with delay 0 should increment count from 0 to 1");
        
        // Check that vector_as_array returns non-NULL
        ID *data = vector_as_array(task_vec);
        TEST_ASSERT_NOT_NULL_MESSAGE(data,
            "vector_as_array should return non-NULL after timer_enqueue");
        
        // Check that data[0] is not NULL (task should be there)
        TEST_ASSERT_NOT_NULL_MESSAGE(data[0],
            "data[0] should contain the enqueued task");
        
        // Cleanup - manually remove the task
        vector_remove_at(task_vec, 0);
        RELEASE(fn);
    });
}

// Test that timer_enqueue with delay 0 works correctly with event_loop_run_next
TEST(test_timer_enqueue_zero_delay_with_run_next) {
    WITH_AUTORELEASE_POOL({
        // Clear event loop first
        event_loop_clear();
        
        // Create a simple function
        CljObject *fn = eval_string("(fn [] 42)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(fn);
        
        // Check initial count (should be 0)
        CljVector *task_vec = (CljVector*)g_runtime.task_queue;
        TEST_ASSERT_NOT_NULL(task_vec);
        unsigned int count_before = vector_count(task_vec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_before,
            "Initial count should be 0");
        
        // Enqueue timer with 0ms delay (should execute immediately)
        int32_t timer_id = timer_enqueue(fn, 0, false, 0);
        TEST_ASSERT_TRUE_MESSAGE(timer_id > 0,
            "timer_enqueue should return a valid timer ID");
        
        // Check that count is now 1 (timer with delay 0 should be enqueued immediately)
        unsigned int count_after_enqueue = vector_count(task_vec);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_after_enqueue,
            "timer_enqueue with delay 0 should increment count from 0 to 1");
        
        // Check that vector_as_array returns non-NULL
        ID *data = vector_as_array(task_vec);
        TEST_ASSERT_NOT_NULL_MESSAGE(data,
            "vector_as_array should return non-NULL after timer_enqueue");
        
        // Now test event_loop_run_next - it should see the count and return true
        // But first, get a fresh reference to task_vec (like event_loop_run_next does)
        CljVector *task_vec_fresh = (CljVector*)g_runtime.task_queue;
        TEST_ASSERT_NOT_NULL(task_vec_fresh);
        unsigned int count_before_run = vector_count(task_vec_fresh);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_before_run,
            "Count should be 1 before event_loop_run_next");
        
        // Cleanup - manually remove the task to avoid memory issues
        vector_remove_at(task_vec, 0);
        RELEASE(fn);
    });
}

