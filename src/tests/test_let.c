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
TEST(test_let_basic_binding) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] x) should return 10
        const char *code = "(let [x 10] x)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Multiple bindings
// ============================================================================
TEST(test_let_multiple_bindings) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10 y 20] (+ x y)) should return 30
        const char *code = "(let [x 10 y 20] (+ x y))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Sequential bindings (later bindings can use earlier ones)
// ============================================================================
TEST(test_let_sequential_bindings) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10 y (+ x 5)] y) should return 15
        const char *code = "(let [x 10 y (+ x 5)] y)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let with expression body
// ============================================================================
TEST(test_let_expression_body) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 5 y 3] (* x y)) should return 15
        const char *code = "(let [x 5 y 3] (* x y))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let with multiple body expressions (implicit do)
// ============================================================================
TEST(test_let_multiple_body_expressions) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] (+ x 1) (+ x 2)) should return 12 (last expression)
        const char *code = "(let [x 10] (+ x 1) (+ x 2))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(12, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Nested let
// ============================================================================
TEST(test_let_nested) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] (let [y 20] (+ x y))) should return 30
        const char *code = "(let [x 10] (let [y 20] (+ x y)))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let shadowing outer binding
// ============================================================================
TEST(test_let_shadowing) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] (let [x 20] x)) should return 20 (inner shadows outer)
        const char *code = "(let [x 10] (let [x 20] x))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let with function calls
// ============================================================================
TEST(test_let_with_function_calls) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Define a function first
        eval_string("(def square (fn [x] (* x x)))", st);
        
        // Test: (let [x 5] (square x)) should return 25
        const char *code = "(let [x 5] (square x))";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let with empty bindings
// ============================================================================
TEST(test_let_empty_bindings) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [] 42) should return 42
        const char *code = "(let [] 42)";
        CljValue result = eval_string(code, st);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    });
}

// ============================================================================
// Tests for if-let macro
// ============================================================================

TEST(test_if_let_basic) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x 42] x nil) => 42
    CljObject *result = eval_string("(if-let [x 42] x nil)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_if_let_false_condition) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x false] x 100) => 100
    CljObject *result = eval_string("(if-let [x false] x 100)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(result));
}

TEST(test_if_let_nil_condition) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x nil] x 200) => 200
    CljObject *result = eval_string("(if-let [x nil] x 200)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(200, as_fixnum(result));
}

TEST(test_if_let_without_else) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x 42] x) => 42
    CljObject *result = eval_string("(if-let [x 42] x)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    // Test: (if-let [x nil] x) => nil
    CljObject *result2 = eval_string("(if-let [x nil] x)", st);
    TEST_ASSERT_NULL(result2);
}

TEST(test_if_let_with_expression) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x 10] (+ x 5) 0) => 15
    CljObject *result = eval_string("(if-let [x 10] (+ x 5) 0)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
}

// ============================================================================
// Tests for let with local functions (like step in filter)
// ============================================================================

TEST(test_let_with_local_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [x] (+ x 1))] (step 5)) => 6
    // This tests if local functions defined in let can be called
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_with_local_function_multiple_calls) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [x] (+ x 1))] (+ (step 5) (step 10))) => 17
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (+ (step 5) (step 10)))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(17, as_fixnum(result));
}

TEST(test_let_with_local_function_using_namespace_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [x] (+ x 1))] (step 5)) => 6
    // This tests if local functions can use namespace functions like +
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_with_local_function_using_reverse) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [coll] (reverse coll))] (step (list 1 2 3)))
    // This tests if local functions can use namespace functions like reverse
    CljObject *result = eval_string("(let [step (fn [coll] (reverse coll))] (step (list 1 2 3)))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_LIST));
    
    // Verify first element is 3
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

// Test that verifies step is bound in let_env by checking if it can be resolved
TEST(test_let_verify_step_binding) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Parse the let expression manually to inspect the binding
    // (let [step (fn [x] (+ x 1))] step)
    // This should return the function itself, proving step is bound
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] step)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_FUNC) || is_type(result, CLJ_CLOSURE));
}

// Test that verifies step can be called after being bound
TEST(test_let_verify_step_callable) {
    // Use global st from setUp (clojure.core already loaded)
    
    // (let [step (fn [x] (+ x 1))] (step 5))
    // This should return 6, proving step is bound and callable
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test that verifies step is bound before it's used in the body
TEST(test_let_verify_step_binding_order) {
    // Use global st from setUp (clojure.core already loaded)
    
    // (let [step (fn [x] x)] (let [result (step 42)] result))
    // This tests if step is available in nested let
    CljObject *result = eval_string("(let [step (fn [x] x)] (let [result (step 42)] result))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Test that verifies let works inside a function (like filter)
TEST(test_let_inside_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Define a function that uses let internally
    // (def test-filter (fn [pred coll] (let [step (fn [x] (+ x 1))] (step 5))))
    eval_string("(def test-filter (fn [pred coll] (let [step (fn [x] (+ x 1))] (step 5))))", st);
    
    // Call the function
    CljObject *result = eval_string("(test-filter even? [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test that verifies filter-like pattern works
TEST(test_let_filter_pattern) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test pattern similar to filter: (fn [pred coll] (let [step (fn [x] x)] (step pred coll (list))))
    // Simplified: (fn [pred coll] (let [step (fn [x] x)] (step 42)))
    eval_string("(def test-filter-pattern (fn [pred coll] (let [step (fn [x] x)] (step 42))))", st);
    
    // Call the function
    CljObject *result = eval_string("(test-filter-pattern even? [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Test that verifies filter function itself can be called
TEST(test_let_filter_function_call) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test if filter function exists and can be called
    CljObject *filter_fn = eval_string("filter", st);
    TEST_ASSERT_NOT_NULL(filter_fn);
    TEST_ASSERT_TRUE(is_type(filter_fn, CLJ_FUNC) || is_type(filter_fn, CLJ_CLOSURE));
    
    // Test a simple filter call: (filter (fn [x] true) [1 2 3])
    // This should return [1 2 3] if filter works
    CljObject *result = eval_string("(filter (fn [x] true) [1 2 3])", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_LIST));
}
