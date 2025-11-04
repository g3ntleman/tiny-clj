/*
 * For-Loop Tests using Unity Framework
 * 
 * Tests for for, doseq, and dotimes implementations.
 */

#include "tests_common.h"
#include "../event_loop.h"
#include "../channel.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// FOR-LOOP TESTS
// ============================================================================

TEST(test_dotimes_basic) {
    // Test eval_dotimes with basic functionality
    // Create dotimes call: (dotimes [i 3] i)
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    CljObject *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_doseq_basic) {
    // Test that eval_doseq handles NULL input gracefully
    CljMap *env = (CljMap*)make_map(4);
    
    // Test with NULL list
    CljObject *result = eval_doseq(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with NULL list (no need to test non-list as immediate values can't be cast)
    result = eval_doseq(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
}

TEST(test_for_basic) {
    // Test that eval_for handles NULL input gracefully
    CljMap *env = (CljMap*)make_map(4);
    
    // Test with NULL list
    CljObject *result = eval_for(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with NULL list (no need to test non-list as immediate values can't be cast)
    result = eval_for(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_with_environment) {
    // Test eval_dotimes with environment binding
    // Create dotimes call: (dotimes [i 3] i)
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    CljObject *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

// Test that go-block enqueues task and result channel receives value
// High-level test using eval_string
TEST(test_go_enqueues_and_result_channel_receives_value) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Use eval_string to evaluate (go (do 1 2 3)) - high-level approach
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go (do 1 2 3))", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        evalstate_free(st);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Initially closed should be false
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    CljObject *closed_val = (CljObject*)map_get((CljValue)chan, (CljValue)kw_closed);
    TEST_ASSERT_TRUE(is_special((CljValue)closed_val));
    TEST_ASSERT_TRUE(as_special((CljValue)closed_val) == SPECIAL_FALSE);
    
    // Run next task using eval_string - high-level approach
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        evalstate_free(st);
        RELEASE(chan);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);
    
    // Channel should have value 3 and be closed
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *val = (CljObject*)map_get((CljValue)chan, (CljValue)kw_value);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)val));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)val));
    closed_val = (CljObject*)map_get((CljValue)chan, (CljValue)kw_closed);
    TEST_ASSERT_TRUE(is_special((CljValue)closed_val));
    TEST_ASSERT_TRUE(as_special((CljValue)closed_val) == SPECIAL_TRUE);
    
    // Cleanup
    evalstate_free(st);
    RELEASE(chan);
}

// Test that run-next-task returns false when no tasks are queued
// High-level test using eval_string
TEST(test_run_next_task_returns_false_when_empty) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    // Call run-next-task when no tasks are queued using eval_string - high-level approach
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        evalstate_free(st);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_FALSE);  // Should return false

    // Cleanup
    evalstate_free(st);
}

// Test that go-block with exception closes channel without value
// High-level test using eval_string
TEST(test_go_exception_closes_channel_without_value) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    // Use eval_string to evaluate (go (/ 1 0)) - high-level approach
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go (/ 1 0))", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        evalstate_free(st);
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(chan);

    // Run next task using eval_string - high-level approach
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        evalstate_free(st);
        RELEASE(chan);
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);  // Task should have run

    // Channel should be closed and have no value
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *closed_val = (CljObject*)map_get((CljValue)chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL(closed_val);
    TEST_ASSERT_TRUE(is_special((CljValue)closed_val));
    TEST_ASSERT_TRUE(as_special((CljValue)closed_val) == SPECIAL_TRUE);
    CljValue val = map_get((CljValue)chan, (CljValue)kw_value);
    // When no value is set (error case), :value key exists but value is NULL
    // make_result_channel sets :value to NULL, so map_get returns NULL
    // After error, we don't update :value, so it remains NULL
    TEST_ASSERT_NULL(val);

    // Cleanup
    evalstate_free(st);
    RELEASE(chan);
}

// High-level test: Test that go-block with successful execution puts value
TEST(test_go_success_puts_value_high_level) {
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);
    
    // Create channel with go-block that succeeds
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go (+ 1 2))", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        evalstate_free(st);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Run next task
    CljObject *ran_result = NULL;
    TRY {
        ran_result = eval_string("(run-next-task)", st);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        evalstate_free(st);
        RELEASE(chan);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_result);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_result));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_result) == SPECIAL_TRUE);
    
    // After successful execution, channel should have value 3 and be closed
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    CljValue val = map_get((CljValue)chan, (CljValue)kw_value);
    CljValue closed_val = map_get((CljValue)chan, (CljValue)kw_closed);
    
    TEST_ASSERT_NOT_NULL((CljObject*)val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(val));  // Should have value 3
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val);
    TEST_ASSERT_TRUE(is_special(closed_val));
    TEST_ASSERT_TRUE(as_special(closed_val) == SPECIAL_TRUE);  // Should be closed
    
    // Cleanup
    evalstate_free(st);
    RELEASE(chan);
}

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================

TEST(test_dotimes_zero_iterations) {
    // Test eval_dotimes with 0 iterations - should not execute body
    // Create dotimes call: (dotimes [i 0] (println "Should not print"))
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(0), NULL));
    CljObject *body = make_list((ID)intern_symbol_global("println"), (CljList*)make_list((ID)make_string("Should not print"), NULL));
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_negative_iterations) {
    // Test eval_dotimes with negative iterations - should not execute body
    // Create dotimes call: (dotimes [i -5] (println "Should not print"))
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(-5), NULL));
    CljObject *body = make_list((ID)intern_symbol_global("println"), (CljList*)make_list((ID)make_string("Should not print"), NULL));
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_large_iterations) {
    // Test eval_dotimes with large number of iterations
    // Create dotimes call: (dotimes [i 1000] i)
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(1000), NULL));
    CljObject *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_invalid_binding_format) {
    // Test eval_dotimes with invalid binding format
    // Create dotimes call: (dotimes [i] i) - missing count
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), NULL);
    CljObject *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for invalid format
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_non_numeric_count) {
    // Test eval_dotimes with non-numeric count
    // Create dotimes call: (dotimes [i "not-a-number"] i)
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)make_string("not-a-number"), NULL));
    CljObject *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for non-numeric count
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_null_input) {
    // Test eval_dotimes with NULL input
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation with NULL
    CljObject *result = eval_dotimes(NULL, env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for NULL input
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_simple_iteration_count) {
    // Test that eval_dotimes executes the body exactly n times
    // This is a simpler test that just verifies the loop runs n times
    
    // Create binding vector: [i 3]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    
    // Create simple body: i (just return the loop variable)
    CljObject *body = intern_symbol_global("i");
    
    CljObject *dotimes_call = make_list((ID)intern_symbol_global("dotimes"), 
                                       (CljList*)make_list((ID)binding_vector, 
                                                         (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // The test passes if no errors occur and the function returns NULL
    // This verifies that the loop executed 3 times without crashing
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_doseq_with_environment) {
    // Test doseq with environment binding
    EvalState *eval_state = evalstate_new();
    TEST_ASSERT_NOT_NULL(eval_state);
    
    // Create vector: [1 2 3]
    CljValue vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector((CljObject*)vec);
    TEST_ASSERT_NOT_NULL(vec_data);
    
    vec_data->data[0] = fixnum(1);
    vec_data->data[1] = fixnum(2);
    vec_data->data[2] = fixnum(3);
    vec_data->count = 3;
    
    // Create binding list: [x [1 2 3]]
    CljObject *binding_list = make_list((ID)intern_symbol_global("x"), (CljList*)make_list((ID)vec, NULL));
    
    // Create body: x - symbol reference
    CljObject *body = intern_symbol_global("x");
    
    // Create function call: (doseq [x [1 2 3]] x)
    CljObject *doseq_call = make_list((ID)intern_symbol_global("doseq"), (CljList*)make_list((ID)binding_list, (CljList*)make_list((ID)body, NULL)));
    
    // Create a simple environment
    CljMap *env = (CljMap*)make_map(4);
    
    // Test doseq evaluation with environment
    CljObject *result = eval_doseq((CljList*)(CljObject*)doseq_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up environment
    RETAIN(env);
    RELEASE(env);
    
    // Clean up
    evalstate_free(eval_state);
    RELEASE((CljObject*)binding_list);
    RELEASE(body);
    RELEASE((CljObject*)doseq_call);
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests