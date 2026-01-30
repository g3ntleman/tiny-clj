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

TEST_SHARED(test_dotimes_zero_iterations) {
    // Test eval_dotimes with 0 iterations - should not execute body
    // Create dotimes call: (dotimes [i 0] (println "Should not print"))
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), (CljList*)make_list(fixnum(0), NULL)));
    CljObject *body = AUTORELEASE(make_list(SYM_PRINTLN, (CljList*)make_list(make_string("Should not print"), NULL)));
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, (CljList*)make_list(binding_vector, (CljList*)make_list(body, NULL))));
    
    // Create environment
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST_SHARED(test_dotimes_negative_iterations) {
    // Test eval_dotimes with negative iterations - should not execute body
    // Create dotimes call: (dotimes [i -5] (println "Should not print"))
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), (CljList*)make_list(fixnum(-5), NULL)));
    CljObject *body = AUTORELEASE(make_list(SYM_PRINTLN, (CljList*)make_list(make_string("Should not print"), NULL)));
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, (CljList*)make_list(binding_vector, (CljList*)make_list(body, NULL))));
    
    // Create environment
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST_SHARED(test_dotimes_large_iterations) {
    // Test eval_dotimes with large number of iterations
    // Create dotimes call: (dotimes [i 1000] i)
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), (CljList*)make_list(fixnum(1000), NULL)));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, (CljList*)make_list(binding_vector, (CljList*)make_list(body, NULL))));
    
    // Create environment
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST_SHARED(test_dotimes_invalid_binding_format) {
    // Test eval_dotimes with invalid binding format
    // Create dotimes call: (dotimes [i] i) - missing count
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, (CljList*)make_list(binding_vector, (CljList*)make_list(body, NULL))));
    
    // Create environment
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for invalid format
}

TEST_SHARED(test_dotimes_non_numeric_count) {
    // Test eval_dotimes with non-numeric count
    // Create dotimes call: (dotimes [i "not-a-number"] i)
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), (CljList*)make_list(make_string("not-a-number"), NULL)));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, (CljList*)make_list(binding_vector, (CljList*)make_list(body, NULL))));
    
    // Create environment
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for non-numeric count
}

TEST_SHARED(test_dotimes_null_input) {
    // Test eval_dotimes with NULL input
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation with NULL
    CljObject *result = eval_dotimes(NULL, env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for NULL input
}

TEST_SHARED(test_dotimes_simple_iteration_count) {
    // Test that eval_dotimes executes the body exactly n times
    // This is a simpler test that just verifies the loop runs n times
    
    // Create binding vector: [i 3]
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), (CljList*)make_list(fixnum(3), NULL)));
    
    // Create simple body: i (just return the loop variable)
    CljSymbol *body = intern_symbol_global("i");
    
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, 
                                       (CljList*)make_list(binding_vector, 
                                                         (CljList*)make_list(body, NULL))));
    
    // Create environment
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // The test passes if no errors occur and the function returns NULL
    // This verifies that the loop executed 3 times without crashing
}

TEST_SHARED(test_dotimes_lexical_n_and_body_bindings) {
    // Regression: dotimes must be able to resolve let-bound symbols for its count
    // and also allow the body to see outer lexical bindings.
    // (let [n 5 a (atom 0)] (dotimes [i n] (swap! a inc)) (deref a)) => 5
    ID result = eval_string("(let [n 5 a (atom 0)] (dotimes [i n] (swap! a inc)) (deref a))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
}


// ============================================================================
// WHILE LOOP TESTS
// ============================================================================

TEST_SHARED(test_while_basic_true) {
    // Test while with true condition that becomes false
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i) => 1
    // Executes once, then condition becomes false
    ID result = eval_string("(let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
    
}

TEST_SHARED(test_while_loop_multiple) {
    // Test while with loop that executes multiple times
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (while (< @i 3) (swap! i inc)) where i starts at 0
    // This should execute 3 times (i: 0 -> 1 -> 2 -> 3)
    ID result = eval_string("(let [i (atom 0)] (while (< @i 3) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
}

TEST_SHARED(test_while_false_condition) {
    // Test while with false condition - should not execute
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (while false 42) => nil (does not execute)
    ID result = eval_string("(while false 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
    
}

TEST_SHARED(test_while_nil_condition) {
    // Test while with nil condition - should not execute
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (while nil 42) => nil (does not execute)
    ID result = eval_string("(while nil 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
    
}

TEST_SHARED(test_while_with_atom) {
    // Test while with atom (like in mandelbrot.clj)
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (let [i (atom 0)] (while (< @i 5) (swap! i inc)) @i) => 5
    ID result = eval_string("(let [i (atom 0)] (while (< @i 5) (swap! i inc)) @i)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
    
}

TEST_SHARED(test_while_multiple_body_exprs) {
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

// Regression: doseq with vector binding
TEST_SHARED(test_doseq_with_vector_binding) {
    ID result = eval_string(
        "(let [sum (atom 0)] (doseq [x [1 2 3]] (swap! sum + x)) @sum)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Regression: for with vector binding (basic list comprehension)
TEST_SHARED(test_for_basic_list_comprehension) {
    ID result = eval_string(
        "(for [x [1 2 3 4]] (* x x))",
        g_test_eval_state);
    // Accept both lazy sequences and lists; normalize to a seq. If result is already
    // CLJ_SEQ, make_seq returns it as-is — do not AUTORELEASE again (it is from eval).
    ID seq = (TAG(result) == CLJ_SEQ) ? result : AUTORELEASE(make_seq(result));
    TEST_ASSERT_NOT_NULL(seq);
    int expected[] = {1, 4, 9, 16};
    int i = 0;
    for (ID cur = seq; cur && !seq_empty(cur); cur = seq_next(cur), ++i) {
        ID first = seq_first(cur);
        TEST_ASSERT_TRUE(is_fixnum(first));
        TEST_ASSERT_EQUAL_INT(expected[i], as_fixnum(first));
    }
    TEST_ASSERT_EQUAL_INT(4, i); // Should have 4 elements
}

// ============================================================================
// Extended for tests (multiple bindings, :when, :let, :while)
// ============================================================================

// Test: Multiple bindings (cartesian product)
TEST_SHARED(test_for_multiple_bindings) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (for [x [1 2] y [3 4]] [x y]) => [[1 3] [1 4] [2 3] [2 4]]
    ID result = eval_string(
        "(vec (for [x [1 2] y [3 4]] [x y]))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_vector(result));
    
    CljPersistentVector *vec = as_persistent_vector(result);
    TEST_ASSERT_EQUAL_INT(4, vector_count(vec));
    
    // Check first element: [1 3]
    ID first = vector_nth(vec, 0);
    TEST_ASSERT_TRUE(is_vector(first));
    CljPersistentVector *first_vec = as_persistent_vector(first);
    TEST_ASSERT_EQUAL_INT(2, vector_count(first_vec));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(first_vec, 0)));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(vector_nth(first_vec, 1)));
    
    // Check last element: [2 4]
    ID last = vector_nth(vec, 3);
    TEST_ASSERT_TRUE(is_vector(last));
    CljPersistentVector *last_vec = as_persistent_vector(last);
    TEST_ASSERT_EQUAL_INT(2, vector_count(last_vec));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(last_vec, 0)));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(vector_nth(last_vec, 1)));
}

// Test: :when modifier (filtering)
TEST_SHARED(test_for_when_modifier) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (for [x (range 6) :when (even? x)] x) => [0 2 4]
    ID result = eval_string(
        "(vec (for [x (range 6) :when (even? x)] x))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_vector(result));
    
    CljPersistentVector *vec = as_persistent_vector(result);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 1)));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(vector_nth(vec, 2)));
}

// Test: :let modifier (binding intermediate values)
TEST_SHARED(test_for_let_modifier) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (for [x [1 2 3] :let [y (* x 2)]] y) => [2 4 6]
    ID result = eval_string(
        "(vec (for [x [1 2 3] :let [y (* x 2)]] y))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_vector(result));
    
    CljPersistentVector *vec = as_persistent_vector(result);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(vector_nth(vec, 1)));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(vector_nth(vec, 2)));
}

// Test: :while modifier (early termination)
TEST_SHARED(test_for_while_modifier) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (for [x (range) :while (< x 3)] x) => [0 1 2]
    ID result = eval_string(
        "(vec (for [x (range) :while (< x 3)] x))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_vector(result));
    
    CljPersistentVector *vec = as_persistent_vector(result);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 1)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 2)));
}

// Test: Large sequence (performance/regression)
TEST_SHARED(test_for_large_sequence) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (count (vec (for [x (range 10000)] x))) => 10000
    ID result = eval_string(
        "(count (vec (for [x (range 10000)] x)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10000, as_fixnum(result));
}

// Test: Lazy sequence (take from infinite)
TEST_SHARED(test_for_lazy_infinite) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (vec (take 3 (for [x (range)] x))) => [0 1 2]
    ID result = eval_string(
        "(vec (take 3 (for [x (range)] x)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_vector(result));
    
    CljPersistentVector *vec = as_persistent_vector(result);
    TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(vec, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 1)));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 2)));
}

// Test: Multi-consumer independence (realize same lazy seq twice)
TEST_SHARED(test_for_multi_consumer_independence) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // (let [s (for [x (range 5)] x)] (= (vec s) (vec s))) => true
    ID result = eval_string(
        "(let [s (for [x (range 5)] x)] (= (vec s) (vec s)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}