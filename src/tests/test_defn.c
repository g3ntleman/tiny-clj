/*
 * Unity Tests for (defn) function definition in Tiny-CLJ
 * 
 * Test-First: These tests are written before implementing defn functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../namespace.h"
#include "../symbol.h"

// ============================================================================
// TEST: Basic defn function definition
// ============================================================================
TEST(test_defn_basic_function) {
        EvalState *st = evalstate_new();
        
        // Test: (defn add [a b] (+ a b)) should define a function
        const char *code = "(defn add [a b] (+ a b))";
        CljValue result = eval_string(code, st);
        
        // defn should return the function name (symbol)
        TEST_ASSERT_NOT_NULL(result);
        
        // Test that the function can be called
        const char *call_code = "(add 3 4)";
        CljValue call_result = eval_string(call_code, st);
        
        TEST_ASSERT_NOT_NULL(call_result);
        TEST_ASSERT_TRUE(is_fixnum(call_result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(call_result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with single parameter
// ============================================================================
TEST(test_defn_single_parameter) {
        EvalState *st = evalstate_new();
        
        // Test: (defn square [x] (* x x))
        eval_string("(defn square [x] (* x x))", st);
        
        // Test function call
        const char *code = "(square 5)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with no parameters
// ============================================================================
TEST(test_defn_no_parameters) {
        EvalState *st = evalstate_new();
        
        // Test: (defn answer [] 42)
        eval_string("(defn answer [] 42)", st);
        
        // Test function call
        const char *code = "(answer)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with multiple body expressions
// ============================================================================
TEST(test_defn_multiple_body_expressions) {
        EvalState *st = evalstate_new();
        
        // Test: (defn test-fn [x] (+ x 1) (+ x 2))
        eval_string("(defn test-fn [x] (+ x 1) (+ x 2))", st);
        
        // Test function call - should return last expression
        const char *code = "(test-fn 5)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(7, as_fixnum(result));
        
        evalstate_free(st);
}

// ============================================================================
// TEST: defn with recursive function
// ============================================================================
TEST(test_defn_recursive_function) {
        EvalState *st = evalstate_new();
        
        // Test: (defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))
        eval_string("(defn factorial [n] (if (= n 0) 1 (* n (factorial (- n 1)))))", st);
        
        // Test function call
        const char *code = "(factorial 5)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(120, as_fixnum(result));
        
        evalstate_free(st);
}

