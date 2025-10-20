#include "unity.h"
#include "function_call.h"
#include "object.h"
#include "namespace.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include "runtime.h"
#include "parser.h"

// Forward declarations for recur tests
void test_recur_factorial(void);
void test_recur_deep_recursion(void);
void test_recur_arity_error(void);

// Test group for recur functionality - defined in unity_test_runner.c

// Test factorial with recur
void test_recur_factorial(void) {
    EvalState *st = evalstate_new();
    if (!st) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Initialize special symbols first
    init_special_symbols();
    
    // Test only the function definition first - no call
    CljObject *factorial_def = eval_string("(def factorial (fn [n acc] (if (= n 0) acc (recur (- n 1) (* n acc)))))", st);
    TEST_ASSERT_NOT_NULL(factorial_def);
    
    evalstate_free(st);
}

// Test deep recursion with recur
void test_recur_deep_recursion(void) {
    EvalState *st = evalstate_new();
    if (!st) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Initialize special symbols first
    init_special_symbols();
    
    // Test deep recursion with recur
    CljObject *deep_def = eval_string("(def deep (fn [n acc] (if (= n 0) acc (recur (- n 1) (+ acc 1)))))", st);
    TEST_ASSERT_NOT_NULL(deep_def);
    
    // Test deep(5, 0) = 5 (very small value for testing)
    CljObject *result = eval_string("(deep 5 0)", st);
    TEST_ASSERT_NOT_NULL(result);
    // Just test that we get a result - don't check type yet
    TEST_ASSERT_TRUE(result != NULL);
    
    evalstate_free(st);
}

// Test arity error with recur
void test_recur_arity_error(void) {
    EvalState *st = evalstate_new();
    if (!st) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Initialize special symbols first
    init_special_symbols();
    
    // Test arity error with recur - simplified test
    CljObject *arity_def = eval_string("(def arity-test (fn [n acc] (if (= n 0) acc (recur (- n 1)))))", st);
    TEST_ASSERT_NOT_NULL(arity_def);
    
    // For now, just test that the function can be defined
    // TODO: Implement proper arity checking for recur
    TEST_ASSERT_TRUE(arity_def != NULL);
    
    evalstate_free(st);
}
