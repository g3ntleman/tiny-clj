/*
 * Unity Tests for (let) bindings in Tiny-CLJ
 * 
 * Test-First: These tests are written before implementing let functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../namespace.h"
#include "../symbol.h"

// ============================================================================
// TEST: Basic let binding
// ============================================================================
void test_let_basic_binding(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 10] x) should return 10
        const char *code = "(let [x 10] x)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Multiple bindings
// ============================================================================
void test_let_multiple_bindings(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 10 y 20] (+ x y)) should return 30
        const char *code = "(let [x 10 y 20] (+ x y))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Sequential bindings (later bindings can use earlier ones)
// ============================================================================
void test_let_sequential_bindings(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 10 y (+ x 5)] y) should return 15
        const char *code = "(let [x 10 y (+ x 5)] y)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Let with expression body
// ============================================================================
void test_let_expression_body(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 5 y 3] (* x y)) should return 15
        const char *code = "(let [x 5 y 3] (* x y))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Let with multiple body expressions (implicit do)
// ============================================================================
void test_let_multiple_body_expressions(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 10] (+ x 1) (+ x 2)) should return 12 (last expression)
        const char *code = "(let [x 10] (+ x 1) (+ x 2))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(12, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Nested let
// ============================================================================
void test_let_nested(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 10] (let [y 20] (+ x y))) should return 30
        const char *code = "(let [x 10] (let [y 20] (+ x y)))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Let shadowing outer binding
// ============================================================================
void test_let_shadowing(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [x 10] (let [x 20] x)) should return 20 (inner shadows outer)
        const char *code = "(let [x 10] (let [x 20] x))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Let with function calls
// ============================================================================
void test_let_with_function_calls(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Define a function first
        eval_string("(def square (fn [x] (* x x)))", st);
        
        // Test: (let [x 5] (square x)) should return 25
        const char *code = "(let [x 5] (square x))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST: Let with empty bindings
// ============================================================================
void test_let_empty_bindings(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        
        // Test: (let [] 42) should return 42
        const char *code = "(let [] 42)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
        
        evalstate_free(st);
    });
}
