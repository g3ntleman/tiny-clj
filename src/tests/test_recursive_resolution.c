/*
 * Test to isolate recursive function resolution problem
 * 
 * This test file helps identify where the problem occurs:
 * 1. Is the function registered in namespace before body evaluation?
 * 2. Can ns_resolve find the function during definition?
 * 3. Does symbol resolution work in eval_body_with_params?
 */

#include "tests_common.h"

// Test: Check if function is registered in namespace before body evaluation
TEST(test_function_registered_before_body_evaluation) {
    
    // Define a simple function (not recursive)
    const char *code = "(defn simple [x] (+ x 1))";
    CljValue result = eval_string(code, st);
    
    // Function should be defined
    TEST_ASSERT_NOT_NULL(result);
    
    // Check if function is in namespace
    CljObject *fn_sym = intern_symbol_global("simple");
    CljObject *fn_value = (CljObject*)ns_resolve(st, fn_sym);
    
    TEST_ASSERT_NOT_NULL(fn_value);
    TEST_ASSERT_TRUE(is_type(fn_value, CLJ_FUNC) || is_type(fn_value, CLJ_CLOSURE));
    
}

// Test: Check if recursive function name is resolvable during body evaluation
TEST(test_recursive_function_name_resolvable) {
    
    // Try to define a recursive function
    // The function name should be resolvable during body evaluation
    const char *code = "(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))";
    
    // This should not throw an exception
    CljValue result = eval_string(code, st);
    
    // Function should be defined
    TEST_ASSERT_NOT_NULL(result);
    
    // Check if function is in namespace
    CljObject *fn_sym = intern_symbol_global("factorial");
    CljObject *fn_value = (CljObject*)ns_resolve(st, fn_sym);
    
    TEST_ASSERT_NOT_NULL(fn_value);
    TEST_ASSERT_TRUE(is_type(fn_value, CLJ_FUNC) || is_type(fn_value, CLJ_CLOSURE));
    
}

// Test: Check if ns_resolve finds function during definition
TEST(test_ns_resolve_during_definition) {
    
    // Start defining a function
    const char *defn_code = "(defn test-fn [x] x)";
    
    // Before evaluation, function should not be in namespace
    CljObject *fn_sym = intern_symbol_global("test-fn");
    CljObject *fn_value_before = (CljObject*)ns_resolve(st, fn_sym);
    TEST_ASSERT_NULL(fn_value_before);
    
    // Evaluate defn
    CljValue result = eval_string(defn_code, st);
    TEST_ASSERT_NOT_NULL(result);
    
    // After evaluation, function should be in namespace
    CljObject *fn_value_after = (CljObject*)ns_resolve(st, fn_sym);
    TEST_ASSERT_NOT_NULL(fn_value_after);
    TEST_ASSERT_TRUE(is_type(fn_value_after, CLJ_FUNC) || is_type(fn_value_after, CLJ_CLOSURE));
    
}

// Test: Check if symbol resolution works in eval_body_with_params
TEST(test_symbol_resolution_in_eval_body_with_params) {
    
    // Define a function
    eval_string("(defn test-fn [x] x)", st);
    
    // Get the function from namespace
    CljObject *fn_sym = intern_symbol_global("test-fn");
    CljObject *fn_value = (CljObject*)ns_resolve(st, fn_sym);
    TEST_ASSERT_NOT_NULL(fn_value);
    
    // Try to resolve the symbol using ns_resolve with NULL st
    CljObject *resolved = (CljObject*)ns_resolve(NULL, fn_sym);
    
    // Should find the function (searches all namespaces)
    TEST_ASSERT_NOT_NULL(resolved);
    
}

// Test: Check if recursive call works (simplified)
TEST(test_recursive_call_simplified) {
    
    // Define a recursive function
    eval_string("(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))", st);
    
    // Call the function
    CljValue result = eval_string("(factorial 5)", st);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(120, as_fixnum(result));
    
}

// Test: Check what happens when eval_body_with_params is called with a symbol
// that should resolve to a function call result
TEST(test_eval_body_with_params_symbol_in_arithmetic) {
    
    // Define a recursive function
    eval_string("(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))", st);
    
    // Get the function symbol
    CljObject *fn_sym = intern_symbol_global("factorial");
    
    // Try to resolve it using ns_resolve with NULL st (like eval_body_with_params does)
    CljObject *resolved = (CljObject*)ns_resolve(NULL, fn_sym);
    
    // Should find the function
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(is_type(resolved, CLJ_FUNC) || is_type(resolved, CLJ_CLOSURE));
    
}

// Test: Check what happens when we evaluate (* n (factorial (- n 1)))
// where factorial is a symbol that should resolve to a function
TEST(test_arithmetic_with_recursive_symbol) {
    
    // Define a recursive function
    eval_string("(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))", st);
    
    // Try to evaluate just the arithmetic part: (* 5 (factorial 4))
    // This simulates what happens inside the recursive call
    CljValue result = eval_string("(* 5 (factorial 4))", st);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(120, as_fixnum(result)); // 5 * 24 = 120
    
}
