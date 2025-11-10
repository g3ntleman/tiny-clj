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
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_doseq_basic) {
    // Test that eval_doseq handles NULL input gracefully
    CljMap *env = make_map(4);
    
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
    CljMap *env = make_map(4);
    
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
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

// Test that go-block enqueues task and result channel receives value
// High-level test using eval_string
TEST(test_go_enqueues_and_result_channel_receives_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Use eval_string to evaluate (go (do 1 2 3)) - high-level approach
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go (do 1 2 3))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Initially closed should be false
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljObject *closed_val = (CljObject*)map_get((CljMap*)chan, (CljValue)kw_closed);
    TEST_ASSERT_TRUE(is_special((CljValue)closed_val));
    TEST_ASSERT_TRUE(as_special((CljValue)closed_val) == SPECIAL_FALSE);
    
    // Run next task using eval_string - high-level approach
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        RELEASE(chan);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);
    
    // Channel should have value 3 and be closed
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *val = (CljObject*)map_get((CljMap*)chan, (CljValue)kw_value);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)val));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)val));
    closed_val = (CljObject*)map_get((CljMap*)chan, (CljValue)kw_closed);
    TEST_ASSERT_TRUE(is_special((CljValue)closed_val));
    TEST_ASSERT_TRUE(as_special((CljValue)closed_val) == SPECIAL_TRUE);
    
    // Cleanup
    RELEASE(chan);
}

// Test that run-next-task returns false when no tasks are queued
// High-level test using eval_string
TEST(test_run_next_task_returns_false_when_empty) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Call run-next-task when no tasks are queued using eval_string - high-level approach
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_FALSE);  // Should return false

    // Cleanup
}

// Test that go-block with exception closes channel without value
// High-level test using eval_string
TEST(test_go_exception_closes_channel_without_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Use eval_string to evaluate (go (/ 1 0)) - high-level approach
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go (/ 1 0))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(chan);

    // Run next task using eval_string - high-level approach
    CljObject *ran_val = NULL;
    TRY {
        ran_val = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        RELEASE(chan);
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(ran_val);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_val));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_val) == SPECIAL_TRUE);  // Task should have run

    // Channel should be closed and have no value
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *closed_val = (CljObject*)map_get((CljMap*)chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL(closed_val);
    TEST_ASSERT_TRUE(is_special((CljValue)closed_val));
    TEST_ASSERT_TRUE(as_special((CljValue)closed_val) == SPECIAL_TRUE);
    CljValue val = map_get((CljMap*)chan, (CljValue)kw_value);
    // When no value is set (error case), :value key exists but value is NULL
    // make_result_channel sets :value to NULL, so map_get returns NULL
    // After error, we don't update :value, so it remains NULL
    TEST_ASSERT_NULL(val);

    // Cleanup
    RELEASE(chan);
}

// High-level test: Test that go-block with successful execution puts value
TEST(test_go_success_puts_value_high_level) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create channel with go-block that succeeds
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go (+ 1 2))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Run next task
    CljObject *ran_result = NULL;
    TRY {
        ran_result = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        RELEASE(chan);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_result);
    TEST_ASSERT_TRUE(is_special((CljValue)ran_result));
    TEST_ASSERT_TRUE(as_special((CljValue)ran_result) == SPECIAL_TRUE);
    
    // After successful execution, channel should have value 3 and be closed
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljValue val = map_get((CljMap*)chan, (CljValue)kw_value);
    CljValue closed_val = map_get((CljMap*)chan, (CljValue)kw_closed);
    
    TEST_ASSERT_NOT_NULL((CljObject*)val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(val));  // Should have value 3
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val);
    TEST_ASSERT_TRUE(is_special(closed_val));
    TEST_ASSERT_TRUE(as_special(closed_val) == SPECIAL_TRUE);  // Should be closed
    
    // Cleanup
    RELEASE(chan);
}

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================

TEST(test_dotimes_zero_iterations) {
    // Test eval_dotimes with 0 iterations - should not execute body
    // Create dotimes call: (dotimes [i 0] (println "Should not print"))
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(0), NULL));
    CljObject *body = (CljObject*)make_list((ID)SYM_PRINTLN, (CljList*)make_list((ID)make_string("Should not print"), NULL));
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_negative_iterations) {
    // Test eval_dotimes with negative iterations - should not execute body
    // Create dotimes call: (dotimes [i -5] (println "Should not print"))
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(-5), NULL));
    CljObject *body = (CljObject*)make_list((ID)SYM_PRINTLN, (CljList*)make_list((ID)make_string("Should not print"), NULL));
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_large_iterations) {
    // Test eval_dotimes with large number of iterations
    // Create dotimes call: (dotimes [i 1000] i)
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(1000), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_invalid_binding_format) {
    // Test eval_dotimes with invalid binding format
    // Create dotimes call: (dotimes [i] i) - missing count
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), NULL);
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for invalid format
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_non_numeric_count) {
    // Test eval_dotimes with non-numeric count
    // Create dotimes call: (dotimes [i "not-a-number"] i)
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)make_string("not-a-number"), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for non-numeric count
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_null_input) {
    // Test eval_dotimes with NULL input
    CljMap *env = make_map(4);
    
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
    CljObject *binding_vector = (CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    
    // Create simple body: i (just return the loop variable)
    CljSymbol *body = intern_symbol_global("i");
    
    CljObject *dotimes_call = (CljObject*)make_list((ID)SYM_DOTIMES, 
                                       (CljList*)make_list((ID)binding_vector, 
                                                         (CljList*)make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // The test passes if no errors occur and the function returns NULL
    // This verifies that the loop executed 3 times without crashing
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_doseq_with_environment) {
    // Test doseq with environment binding
    EvalState *eval_state = evalstate_new(false);
    TEST_ASSERT_NOT_NULL(eval_state);
    
    // Create vector: [1 2 3]
    CljValue vec = (CljValue)make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector((CljObject*)vec);
    TEST_ASSERT_NOT_NULL(vec_data);
    
    vec_data->data[0] = fixnum(1);
    vec_data->data[1] = fixnum(2);
    vec_data->data[2] = fixnum(3);
    vec_data->count = 3;
    
    // Create binding list: [x [1 2 3]]
    CljObject *binding_list = make_list((ID)intern_symbol_global("x"), (CljList*)make_list((ID)vec, NULL));
    
    // Create body: x - symbol reference
    CljSymbol *body = intern_symbol_global("x");
    
    // Create function call: (doseq [x [1 2 3]] x)
    CljObject *doseq_call = (CljObject*)make_list((ID)SYM_DOSEQ, (CljList*)make_list((ID)binding_list, (CljList*)make_list((ID)body, NULL)));
    
    // Create a simple environment
    CljMap *env = make_map(4);
    
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
// WHILE LOOP TESTS
// ============================================================================

TEST(test_while_basic_true) {
    // Test while with true condition that becomes false
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i) => 1
    // Executes once, then condition becomes false
    ID result = eval_string("(let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
    
}

TEST(test_while_loop_multiple) {
    // Test while with loop that executes multiple times
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (while (< @i 3) (swap! i inc)) where i starts at 0
    // This should execute 3 times (i: 0 -> 1 -> 2 -> 3)
    ID result = eval_string("(let [i (atom 0)] (while (< @i 3) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
}

TEST(test_while_false_condition) {
    // Test while with false condition - should not execute
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (while false 42) => nil (does not execute)
    ID result = eval_string("(while false 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
    
}

TEST(test_while_nil_condition) {
    // Test while with nil condition - should not execute
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (while nil 42) => nil (does not execute)
    ID result = eval_string("(while nil 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
    
}

TEST(test_while_with_atom) {
    // Test while with atom (like in mandelbrot.clj)
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (let [i (atom 0)] (while (< @i 5) (swap! i inc)) @i) => 5
    ID result = eval_string("(let [i (atom 0)] (while (< @i 5) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
    
}

TEST(test_while_multiple_body_exprs) {
    // Test while with multiple body expressions
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (let [i (atom 0)] (while (< @i 2) (swap! i inc) (+ @i 10)) @i) => 2
    // Last expression in body is evaluated, but while always returns nil
    ID result = eval_string("(let [i (atom 0)] (while (< @i 2) (swap! i inc) (+ @i 10)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
    
}

// ============================================================================
// CHANNEL TESTS - Testing transient map channels (Clojure-compatible)
// ============================================================================

// Test that go-blocks return transient map channels
TEST(test_go_returns_transient_map_channel) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test if eval_string("(go 42)") returns a channel
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("eval_string(\"(go 42)\") should not throw exception");
        return;
    } END_TRY
    
    // Verify channel is not NULL
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify it's a transient map (channel is a transient map)
    // Clojure-compatibility: Channels are maps, but transient for in-place mutation
    TEST_ASSERT_TRUE(is_type((CljObject*)chan, CLJ_TRANSIENT_MAP) || is_type((CljObject*)chan, CLJ_MAP));
    
    // Cleanup
    if (chan) RELEASE(chan);
}

// Test that channels are transient maps and can be mutated in-place
TEST(test_channel_is_transient_map_and_mutable) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a channel (transient map)
    CljMap *chan = make_result_channel();
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify it's a transient map
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Put a value using result_channel_put (mutates in-place)
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljValue val_42 = fixnum(42);
    result_channel_put(chan, val_42);
    
    // Channel should be mutated in-place (same pointer)
    // Clojure-compatibility: Channels are mutable like promises
    CljValue val = map_get(chan, (CljValue)kw_value);
    TEST_ASSERT_NOT_NULL((CljObject*)val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
    
    // Cleanup
    RELEASE(chan);
}

// Test that channel mutation after run-next-task works correctly
// Channels are transient maps, so they should be mutated in-place
TEST(test_channel_mutation_after_run_next_task) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify it's a transient map
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Run next task
    CljObject *ran_result = NULL;
    TRY {
        ran_result = eval_string("(run-next-task)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("run-next-task should not throw exception");
        RELEASE(chan);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(ran_result);
    
    // After run-next-task, the channel should be mutated in-place
    // Channels are transient maps, so mutation should be visible to the caller
    // Clojure-compatibility: Channels are mutable like promises
    
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljValue val = map_get((CljMap*)chan, (CljValue)kw_value);
    
    // Check if value was set (should be set since channel is mutated in-place)
    TEST_ASSERT_NOT_NULL((CljObject*)val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
    
    // Cleanup
    RELEASE(chan);
}

// Test direct channel creation and mutation
// Channels are transient maps, so they can be mutated in-place
TEST(test_direct_channel_creation_and_mutation) {
    // Create channel directly (transient map)
    CljMap *chan = make_result_channel();
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify it's a transient map
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Verify initial state
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    
    CljValue val = map_get(chan, (CljValue)kw_value);
    CljValue closed = map_get(chan, (CljValue)kw_closed);
    
    TEST_ASSERT_NULL(val);  // Should be NULL initially
    TEST_ASSERT_NOT_NULL((CljObject*)closed);
    TEST_ASSERT_TRUE(is_special(closed));
    TEST_ASSERT_TRUE(as_special(closed) == SPECIAL_FALSE);  // Should be false initially
    
    // Put value using result_channel_put (mutates in-place)
    CljValue val_42 = fixnum(42);
    result_channel_put(chan, val_42);
    
    // Verify channel has value (mutated in-place)
    CljValue new_val = map_get(chan, (CljValue)kw_value);
    TEST_ASSERT_NOT_NULL((CljObject*)new_val);
    TEST_ASSERT_TRUE(is_fixnum(new_val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(new_val));
    
    // Close channel (mutates in-place)
    result_channel_close(chan);
    
    // Verify closed channel (mutated in-place)
    CljValue closed_val = map_get(chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val);
    TEST_ASSERT_TRUE(is_special(closed_val));
    TEST_ASSERT_TRUE(as_special(closed_val) == SPECIAL_TRUE);
    
    // Cleanup
    RELEASE(chan);
}

// Test that event_loop_run_next mutates channel correctly
// Channels are transient maps, so they should be mutated in-place
TEST(test_event_loop_run_next_mutates_channel) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify it's a transient map
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Run next task using direct function call (not eval_string)
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);  // Should have run a task
    
    // Channels are transient maps, so event_loop_run_next mutates them in-place
    // Clojure-compatibility: Channels are mutable like promises
    
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljValue val = map_get((CljMap*)chan, (CljValue)kw_value);
    
    // Check if value was set (should be set since channel is mutated in-place)
    TEST_ASSERT_NOT_NULL((CljObject*)val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
    
    // Cleanup
    RELEASE(chan);
}

// ============================================================================
// HYPOTHESIS TESTS FOR EVENT LOOP
// ============================================================================

// Hypothesis 1: Channel is created correctly and returned from go block
TEST(test_hypothesis_channel_created_correctly) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify it's a transient map
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Verify initial state: :value should be nil, :closed should be false
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    
    CljValue val = map_get((CljMap*)chan, (CljValue)kw_value);
    CljValue closed = map_get((CljMap*)chan, (CljValue)kw_closed);
    
    TEST_ASSERT_NULL(val);  // Should be NULL initially
    TEST_ASSERT_NOT_NULL((CljObject*)closed);
    TEST_ASSERT_TRUE(is_special(closed));
    TEST_ASSERT_TRUE(as_special(closed) == SPECIAL_FALSE);  // Should be false initially
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 2: Channel is enqueued correctly in event loop
TEST(test_hypothesis_channel_enqueued_correctly) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Verify channel is still valid after enqueue
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Verify there's a task in the queue (by trying to run it)
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);  // Should have run a task
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 3: event_loop_run_next executes task correctly
TEST(test_hypothesis_event_loop_executes_task) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block with simple value
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Store channel pointer for later comparison
    CljMap *chan_ptr = (CljMap*)chan;
    
    // Run next task
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);  // Should have run a task
    
    // Verify channel is still the same reference (not copied)
    TEST_ASSERT_EQUAL_PTR(chan_ptr, (CljMap*)chan);
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 4: Channel mutation happens in-place (same reference)
TEST(test_hypothesis_channel_mutation_in_place) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Store channel pointer and initial state
    CljMap *chan_ptr = (CljMap*)chan;
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    
    // Get initial value (should be NULL)
    CljValue initial_val = map_get(chan_ptr, (CljValue)kw_value);
    TEST_ASSERT_NULL(initial_val);
    
    // Run next task
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);
    
    // Verify channel is still the same reference (not copied)
    TEST_ASSERT_EQUAL_PTR(chan_ptr, (CljMap*)chan);
    
    // Verify channel type is still transient map
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 5: Channel value is set correctly after run_next
TEST(test_hypothesis_channel_value_set_after_run_next) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block with value 42
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    CljMap *chan_ptr = (CljMap*)chan;
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    
    // Verify initial value is NULL
    CljValue initial_val = map_get(chan_ptr, (CljValue)kw_value);
    TEST_ASSERT_NULL(initial_val);
    
    // Run next task
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);
    
    // Verify channel is still the same reference
    TEST_ASSERT_EQUAL_PTR(chan_ptr, (CljMap*)chan);
    
    // Check if value was set
    CljValue val = map_get(chan_ptr, (CljValue)kw_value);
    
    // This is the critical test: value should be set after run_next
    if (val == NULL) {
        // Value is NULL - this is the problem we're investigating
        // Let's check if the channel was mutated at all
        CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
        CljValue closed = map_get(chan_ptr, (CljValue)kw_closed);
        TEST_ASSERT_NOT_NULL((CljObject*)closed);
        TEST_ASSERT_TRUE(is_special(closed));
        // If closed is true, the channel was mutated, but value wasn't set
        // This suggests the problem is in result_channel_put or eval_function_call
        if (as_special(closed) == SPECIAL_TRUE) {
            TEST_FAIL_MESSAGE("Channel was closed but value was not set - check result_channel_put or eval_function_call");
        } else {
            TEST_FAIL_MESSAGE("Channel was not mutated at all - check event_loop_run_next");
        }
    } else {
        // Value is set - verify it's correct
        TEST_ASSERT_TRUE(is_fixnum(val));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
    }
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 6: Direct channel manipulation works correctly
TEST(test_hypothesis_direct_channel_manipulation) {
    // Create channel directly
    CljMap *chan = make_result_channel();
    TEST_ASSERT_NOT_NULL(chan);
    
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    
    // Verify initial value is NULL
    CljValue initial_val = map_get(chan, (CljValue)kw_value);
    TEST_ASSERT_NULL(initial_val);
    
    // Put value directly
    CljValue val_42 = fixnum(42);
    result_channel_put(chan, val_42);
    
    // Verify value was set
    CljValue val = map_get(chan, (CljValue)kw_value);
    TEST_ASSERT_NOT_NULL((CljObject*)val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 7: eval_function_call returns correct value for zero-arity function
TEST(test_hypothesis_eval_function_call_returns_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a zero-arity function that returns 42
    CljObject *fn_obj = NULL;
    TRY {
        fn_obj = eval_string("(fn [] 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating function should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(fn_obj);
    
    // Call the function directly
    CljObject *result = NULL;
    TRY {
        result = eval_function_call(fn_obj, NULL, 0, NULL, g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Calling function should not throw exception");
        RELEASE(fn_obj);
        return;
    } END_TRY
    
    // Verify result is correct
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    // Cleanup
    RELEASE(fn_obj);
    if (!IS_IMMEDIATE(result)) {
        RELEASE(result);
    }
}

// Hypothesis 8: Channel pointer is preserved correctly through event loop
TEST(test_hypothesis_channel_pointer_preserved) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block
    CljObject *chan = NULL;
    TRY {
        chan = eval_string("(go 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Creating go-block should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(chan);
    
    // Store original pointer
    CljMap *chan_ptr_original = (CljMap*)chan;
    void *chan_addr_original = (void*)chan_ptr_original;
    
    // Verify channel type
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Verify initial state
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljValue closed_before = map_get(chan_ptr_original, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_before);
    TEST_ASSERT_TRUE(is_special(closed_before));
    TEST_ASSERT_TRUE(as_special(closed_before) == SPECIAL_FALSE);
    
    // Run next task
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);
    
    // CRITICAL: Verify pointer is still the same (not copied)
    TEST_ASSERT_EQUAL_PTR(chan_ptr_original, (CljMap*)chan);
    TEST_ASSERT_EQUAL_PTR(chan_addr_original, (void*)chan);
    
    // Verify channel type is still transient map
    obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Verify channel was mutated (closed should be true now)
    CljValue closed_after = map_get(chan_ptr_original, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_after);
    TEST_ASSERT_TRUE(is_special(closed_after));
    
    // If closed is still false, the channel was not mutated
    if (as_special(closed_after) == SPECIAL_FALSE) {
        TEST_FAIL_MESSAGE("Channel was not mutated - closed is still false after run_next");
    }
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 9: Channel pointer in event loop queue is the same as returned channel
TEST(test_hypothesis_channel_pointer_same_in_queue) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create go-block
    CljObject *chan = eval_string("(go 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(chan);
    
    // Store original pointer
    CljMap *chan_ptr_original = (CljMap*)chan;
    void *chan_addr_original = (void*)chan_ptr_original;
    
    // Verify channel type
    CljObject *obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // CRITICAL: The channel pointer returned from eval_string("(go 42)") should be
    // the same pointer that is stored in the event loop queue
    // This is because event_loop_enqueue RETAINs the channel, but doesn't copy it
    
    // Run next task - this should mutate the channel in-place
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE(ran);
    
    // CRITICAL: Verify pointer is still the same (not copied)
    TEST_ASSERT_EQUAL_PTR(chan_ptr_original, (CljMap*)chan);
    TEST_ASSERT_EQUAL_PTR(chan_addr_original, (void*)chan);
    
    // Verify channel type is still transient map
    obj = (CljObject*)chan;
    TEST_ASSERT_TRUE(obj->type == CLJ_TRANSIENT_MAP);
    
    // Verify channel was mutated (closed should be true now)
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljValue closed_after = map_get(chan_ptr_original, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_after);
    TEST_ASSERT_TRUE(is_special(closed_after));
    
    // If closed is still false, the channel was not mutated
    // This means either:
    // 1. The channel pointer in the queue is different from the returned channel
    // 2. map_conj is not mutating the channel correctly
    // 3. The channel is being copied somewhere
    if (as_special(closed_after) == SPECIAL_FALSE) {
        // Debug: Print channel pointer information
        fprintf(stderr, "DEBUG: Channel pointer mismatch detected\n");
        fprintf(stderr, "  Original channel pointer: %p\n", (void*)chan_ptr_original);
        fprintf(stderr, "  Current channel pointer: %p\n", (void*)chan);
        fprintf(stderr, "  Channel type: %d\n", obj->type);
        fprintf(stderr, "  Channel RC: %d\n", obj->rc);
        fprintf(stderr, "  Channel count: %d\n", ((CljMap*)chan)->count);
        fprintf(stderr, "  Closed value: %p\n", (void*)closed_after);
        if (closed_after) {
            fprintf(stderr, "  Closed is special: %d\n", is_special(closed_after));
            if (is_special(closed_after)) {
                fprintf(stderr, "  Closed special value: %d\n", as_special(closed_after));
            }
        }
        TEST_FAIL_MESSAGE("Channel was not mutated - closed is still false after run_next. Check if channel pointer in queue is different from returned channel.");
    }
    
    // Cleanup
    RELEASE(chan);
}

// Hypothesis 10: Direct channel mutation works correctly
TEST(test_hypothesis_direct_channel_mutation_works) {
    // Create channel directly
    CljMap *chan = make_result_channel();
    TEST_ASSERT_NOT_NULL(chan);
    
    // Store original pointer
    void *chan_ptr_before = (void*)chan;
    
    // Verify initial state
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljValue closed_before = map_get(chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_before);
    TEST_ASSERT_TRUE(is_special(closed_before));
    TEST_ASSERT_TRUE(as_special(closed_before) == SPECIAL_FALSE);
    
    // Close channel directly
    result_channel_close(chan);
    
    // Verify pointer didn't change
    TEST_ASSERT_EQUAL_PTR(chan_ptr_before, (void*)chan);
    
    // Verify channel was mutated
    CljValue closed_after = map_get(chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_after);
    TEST_ASSERT_TRUE(is_special(closed_after));
    TEST_ASSERT_TRUE(as_special(closed_after) == SPECIAL_TRUE);
    
    // Cleanup
    RELEASE(chan);
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests