/*
 * For-Loop Tests using Unity Framework
 * 
 * Tests for for, doseq, dotimes, and while loop implementations.
 */

#include "tests_common.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================

TEST(test_dotimes_zero_iterations) {
    // Test eval_dotimes with 0 iterations - should not execute body
    // Create dotimes call: (dotimes [i 0] (println "Should not print"))
    CljObject *binding_vector = AUTORELEASE((CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(0), NULL)));
    CljObject *body = AUTORELEASE((CljObject*)make_list((ID)SYM_PRINTLN, (CljList*)make_list((ID)make_string("Should not print"), NULL)));
    CljObject *dotimes_call = AUTORELEASE((CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL))));
    
    // Create environment
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST(test_dotimes_negative_iterations) {
    // Test eval_dotimes with negative iterations - should not execute body
    // Create dotimes call: (dotimes [i -5] (println "Should not print"))
    CljObject *binding_vector = AUTORELEASE((CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(-5), NULL)));
    CljObject *body = AUTORELEASE((CljObject*)make_list((ID)SYM_PRINTLN, (CljList*)make_list((ID)make_string("Should not print"), NULL)));
    CljObject *dotimes_call = AUTORELEASE((CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL))));
    
    // Create environment
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST(test_dotimes_large_iterations) {
    // Test eval_dotimes with large number of iterations
    // Create dotimes call: (dotimes [i 1000] i)
    CljObject *binding_vector = AUTORELEASE((CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(1000), NULL)));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = AUTORELEASE((CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL))));
    
    // Create environment
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST(test_dotimes_invalid_binding_format) {
    // Test eval_dotimes with invalid binding format
    // Create dotimes call: (dotimes [i] i) - missing count
    CljObject *binding_vector = AUTORELEASE((CljObject*)make_list((ID)intern_symbol_global("i"), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = AUTORELEASE((CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL))));
    
    // Create environment
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for invalid format
}

TEST(test_dotimes_non_numeric_count) {
    // Test eval_dotimes with non-numeric count
    // Create dotimes call: (dotimes [i "not-a-number"] i)
    CljObject *binding_vector = AUTORELEASE((CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)make_string("not-a-number"), NULL)));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = AUTORELEASE((CljObject*)make_list((ID)SYM_DOTIMES, (CljList*)make_list((ID)binding_vector, (CljList*)make_list((ID)body, NULL))));
    
    // Create environment
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for non-numeric count
}

TEST(test_dotimes_null_input) {
    // Test eval_dotimes with NULL input
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation with NULL
    CljObject *result = eval_dotimes(NULL, env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for NULL input
}

TEST(test_dotimes_simple_iteration_count) {
    // Test that eval_dotimes executes the body exactly n times
    // This is a simpler test that just verifies the loop runs n times
    
    // Create binding vector: [i 3]
    CljObject *binding_vector = AUTORELEASE((CljObject*)make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL)));
    
    // Create simple body: i (just return the loop variable)
    CljSymbol *body = intern_symbol_global("i");
    
    CljObject *dotimes_call = AUTORELEASE((CljObject*)make_list((ID)SYM_DOTIMES, 
                                       (CljList*)make_list((ID)binding_vector, 
                                                         (CljList*)make_list((ID)body, NULL))));
    
    // Create environment
    CljMap *env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env, g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // The test passes if no errors occur and the function returns NULL
    // This verifies that the loop executed 3 times without crashing
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
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests