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
#include <sys/time.h>

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

// ============================================================================
// TEST: defn symbol resolution in REPL context (reproduces current bug)
// ============================================================================
TEST(test_defn_symbol_resolution_in_repl_context) {
    EvalState *st = evalstate_new();
    
    // Simuliere REPL-Kontext: evaluiere defn wie im REPL
    // Das sollte aktuell fehlschlagen mit "Unable to resolve symbol: defn"
    const char *code = "(defn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))";
    CljValue result = eval_string(code, st);
    
    // Test sollte zeigen, dass defn funktioniert
    TEST_ASSERT_NOT_NULL(result);
    
    // Funktion sollte aufrufbar sein
    CljValue call = eval_string("(fib 5)", st);
    TEST_ASSERT_NOT_NULL(call);
    TEST_ASSERT_TRUE(is_fixnum(call));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(call));
    
    evalstate_free(st);
}

// ============================================================================
// TEST: Parameter lookup optimization
// ============================================================================
TEST(test_parameter_lookup_optimization) {
    EvalState *st = evalstate_new();
    
    // Define a function with 3 parameters to test lookup performance
    eval_string("(defn test-lookup [a b c] (+ a (+ b c)))", st);
    
    // Measure time for 1000 parameter lookups
    // This test establishes baseline for parameter lookup performance
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Call function 1000 times - each call does parameter lookups
    for (int i = 0; i < 1000; i++) {
        char code[64];
        snprintf(code, sizeof(code), "(test-lookup %d %d %d)", i, i+1, i+2);
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(i + (i+1) + (i+2), as_fixnum(result));
    }
    
    gettimeofday(&end, NULL);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_usec - start.tv_usec) / 1000.0;
    
    printf("Baseline: 1000 function calls with parameter lookups took %.2f ms\n", elapsed_ms);
    
    evalstate_free(st);
}

