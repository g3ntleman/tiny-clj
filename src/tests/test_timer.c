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
    
    TEST_ASSERT_NULL(result);
    
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
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule \"100\" (fn [] 42))", g_test_eval_state);
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
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule 100 42)", g_test_eval_state);
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
    CljObject *result = NULL;
    TRY {
        result = eval_string("(schedule-periodic \"0\" 100 (fn [] 42))", g_test_eval_state);
    } CATCH(ex) {
        // Should throw exception
        TEST_PASS();
        return;
    } END_TRY
    
    // Should have thrown exception
    TEST_FAIL_MESSAGE("schedule-periodic should throw exception for non-integer delay");
}

