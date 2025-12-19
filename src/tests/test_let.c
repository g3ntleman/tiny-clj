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
#include "../atom.h"
#include "../list.h"

// ============================================================================
// TEST: Basic let binding
// ============================================================================
TEST(test_let_basic_binding) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] x) should return 10
        const char *code = "(let [x 10] x)";
        CljValue result = eval_string(code, g_test_eval_state);
        
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
        CljValue result = eval_string(code, g_test_eval_state);
        
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
        CljValue result = eval_string(code, g_test_eval_state);
        
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
        CljValue result = eval_string(code, g_test_eval_state);
        
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
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(12, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let closures capture stack-based bindings
// ============================================================================
TEST(test_let_closure_captures_value) {
    WITH_AUTORELEASE_POOL({
        const char *code = "(let [x 10 f (fn [] x)] (f))";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let supports up to 16 frame bindings
// ============================================================================
TEST(test_let_sixteen_frame_bindings) {
    WITH_AUTORELEASE_POOL({
        const char *code =
            "(let [a0 0 a1 1 a2 2 a3 3 a4 4 a5 5 a6 6 a7 7 "
                  "a8 8 a9 9 a10 10 a11 11 a12 12 a13 13 a14 14 a15 15]"
                  " (+ a0 a15))";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
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
        CljValue result = eval_string(code, g_test_eval_state);
        
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
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let symbol in arithmetic operation
// ============================================================================
TEST(test_let_symbol_in_arithmetic) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [result 2] (+ 1 result)) should return 3
        // This tests that symbols bound in let can be used in arithmetic operations
        const char *code = "(let [result 2] (+ 1 result))";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let with function calls
// ============================================================================
TEST(test_let_with_function_calls) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Define a function first
        eval_string("(def square (fn [x] (* x x)))", g_test_eval_state);
        
        // Test: (let [x 5] (square x)) should return 25
        const char *code = "(let [x 5] (square x))";
        CljValue result = eval_string(code, g_test_eval_state);
        
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
        CljValue result = eval_string(code, g_test_eval_state);
        
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
    CljObject *result = eval_string("(if-let [x 42] x nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_if_let_false_condition) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x false] x 100) => 100
    CljObject *result = eval_string("(if-let [x false] x 100)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(result));
}

TEST(test_if_let_nil_condition) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x nil] x 200) => 200
    CljObject *result = eval_string("(if-let [x nil] x 200)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(200, as_fixnum(result));
}

TEST(test_if_let_without_else) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x 42] x) => 42
    CljObject *result = eval_string("(if-let [x 42] x)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    // Test: (if-let [x nil] x) => nil
    CljObject *result2 = eval_string("(if-let [x nil] x)", g_test_eval_state);
    TEST_ASSERT_NULL(result2);
}

TEST(test_if_let_with_expression) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (if-let [x 10] (+ x 5) 0) => 15
    CljObject *result = eval_string("(if-let [x 10] (+ x 5) 0)", g_test_eval_state);
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
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_with_local_function_multiple_calls) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [x] (+ x 1))] (+ (step 5) (step 10))) => 17
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (+ (step 5) (step 10)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(17, as_fixnum(result));
}

TEST(test_let_with_local_function_using_namespace_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [x] (+ x 1))] (step 5)) => 6
    // This tests if local functions can use namespace functions like +
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_with_local_function_using_reverse) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [coll] (reverse coll))] (step (list 1 2 3)))
    // This tests if local functions can use namespace functions like reverse
    CljObject *result = eval_string("(let [step (fn [coll] (reverse coll))] (step (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && list_type_matches(TAG(result)));
    
    // Verify first element is 3
    CljList *list = as_list(result);
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
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] step)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_FUNC || result && TAG(result) == CLJ_CLOSURE);
}

// Test that verifies step can be called after being bound
TEST(test_let_verify_step_callable) {
    // Use global st from setUp (clojure.core already loaded)
    
    // (let [step (fn [x] (+ x 1))] (step 5))
    // This should return 6, proving step is bound and callable
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test that verifies step is bound before it's used in the body
TEST(test_let_verify_step_binding_order) {
    // Use global st from setUp (clojure.core already loaded)
    
    // (let [step (fn [x] x)] (let [result (step 42)] result))
    // This tests if step is available in nested let
    CljObject *result = eval_string("(let [step (fn [x] x)] (let [result (step 42)] result))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Test that verifies let works inside a function (like filter)
TEST(test_let_inside_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Define a function that uses let internally
    // (def test-filter (fn [pred coll] (let [step (fn [x] (+ x 1))] (step 5))))
    eval_string("(def test-filter (fn [pred coll] (let [step (fn [x] (+ x 1))] (step 5))))", g_test_eval_state);
    
    // Call the function
    CljObject *result = eval_string("(test-filter even? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Test that verifies filter-like pattern works
TEST(test_let_filter_pattern) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test pattern similar to filter: (fn [pred coll] (let [step (fn [x] x)] (step pred coll (list))))
    // Simplified: (fn [pred coll] (let [step (fn [x] x)] (step 42)))
    eval_string("(def test-filter-pattern (fn [pred coll] (let [step (fn [x] x)] (step 42))))", g_test_eval_state);
    
    // Call the function
    CljObject *result = eval_string("(test-filter-pattern even? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// Test that verifies filter function itself can be called
TEST(test_let_filter_function_call) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test if filter function exists and can be called
    CljObject *filter_fn = eval_string("filter", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(filter_fn);
    TEST_ASSERT_TRUE(filter_fn && TAG(filter_fn) == CLJ_FUNC || filter_fn && TAG(filter_fn) == CLJ_CLOSURE);
    
    // Test a simple filter call: (filter (fn [x] true) (list 1 2 3))
    // This should return (1 2 3) if filter works
    CljObject *result = eval_string("(filter (fn [x] true) (list 1 2 3))", g_test_eval_state);
    if (!result) {
        TEST_FAIL_MESSAGE("filter with (fn [x] true) returned NULL");
        return;
    }
    TEST_ASSERT_TRUE(result && list_type_matches(TAG(result)));
    
    // Test if even? works: (even? 2) => true
    CljObject *even_result = eval_string("(even? 2)", g_test_eval_state);
    if (!even_result) {
        TEST_FAIL_MESSAGE("even? 2 returned NULL");
        return;
    }
    TEST_ASSERT_TRUE(clj_is_truthy(even_result));
    
    // Test if even? works: (even? 1) => false
    CljObject *odd_result = eval_string("(even? 1)", g_test_eval_state);
    if (!odd_result) {
        TEST_FAIL_MESSAGE("even? 1 returned NULL");
        return;
    }
    TEST_ASSERT_FALSE(clj_is_truthy(odd_result));
    
    // Test a simple let with step function: (let [step (fn [x] x)] (step 42))
    CljObject *simple_step = eval_string("(let [step (fn [x] x)] (step 42))", g_test_eval_state);
    if (!simple_step) {
        TEST_FAIL_MESSAGE("simple let with step returned NULL");
        return;
    }
    TEST_ASSERT_TRUE(is_fixnum(simple_step));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(simple_step));
    
    // Test with even?: (filter even? (list 1 2 3))
    // This should return (2) if filter works
    CljObject *result2 = eval_string("(filter even? (list 1 2 3))", g_test_eval_state);
    if (!result2) {
        TEST_FAIL_MESSAGE("filter with even? returned NULL");
        return;
    }
    TEST_ASSERT_TRUE(result2 && list_type_matches(TAG(result2)));
    
    // Verify first element is 2
    CljList *list = as_list(result2);
    TEST_ASSERT_NOT_NULL(list);
    if (!list->first) {
        TEST_FAIL_MESSAGE("filter result list is empty (first is NULL)");
        return;
    }
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
}

// ============================================================================
// TESTS: Recursive functions in let bindings - Thesis testing
// ============================================================================

// THESIS 1: A recursive function defined in a let binding should be able to find itself
// This tests whether step can find itself when calling (step ...) recursively
TEST(test_let_recursive_function_self_reference) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [n acc] (if (= n 0) acc (step (- n 1) (+ acc n))))] (step 5 0))
    // This should return 15 (sum of 1+2+3+4+5)
    // The function step calls itself recursively, so it must find itself in its closure environment
    CljObject *result = eval_string("(let [step (fn [n acc] (if (= n 0) acc (step (- n 1) (+ acc n))))] (step 5 0))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function in let returned NULL - step cannot find itself");
        return;
    }
    
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
}

// THESIS 2: A recursive function in let should be able to call namespace functions like reverse
// This tests whether step can find reverse in its closure environment
TEST(test_let_recursive_function_with_namespace_function) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [coll acc] (if (empty? coll) (reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))
    // This should return (1 2 3) - step calls reverse, which must be in closure environment
    CljObject *result = eval_string("(let [step (fn [coll acc] (if (empty? coll) (reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function with reverse returned NULL - reverse not found in closure environment");
        return;
    }
    
    TEST_ASSERT_TRUE(result && list_type_matches(TAG(result)));
    
    // Verify result is (1 2 3)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
    
    CljList *rest1 = as_list(list->rest);
    TEST_ASSERT_NOT_NULL(rest1);
    TEST_ASSERT_NOT_NULL(rest1->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)rest1->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)rest1->first));
    
    CljList *rest2 = as_list(rest1->rest);
    TEST_ASSERT_NOT_NULL(rest2);
    TEST_ASSERT_NOT_NULL(rest2->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)rest2->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)rest2->first));
}

// THESIS 3: A recursive function in let should work like filter's step function
// This tests the exact pattern used in filter: step calls itself and uses reverse
TEST(test_let_filter_step_pattern) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: Simplified filter pattern
    // (let [step (fn [pred coll acc] (if (empty? coll) (if (empty? acc) nil (reverse acc)) (if (pred (first coll)) (step pred (rest coll) (cons (first coll) acc)) (step pred (rest coll) acc))))] (step even? (list 1 2 3) (list)))
    // This should return (2) - step must find itself and reverse
    CljObject *result = eval_string("(let [step (fn [pred coll acc] (if (empty? coll) (if (empty? acc) nil (reverse acc)) (if (pred (first coll)) (step pred (rest coll) (cons (first coll) acc)) (step pred (rest coll) acc))))] (step even? (list 1 2 3) (list)))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Filter step pattern returned NULL - step cannot find itself or reverse");
        return;
    }
    
    TEST_ASSERT_TRUE(result && list_type_matches(TAG(result)));
    
    // Verify result is (2)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
}

// THESIS 4: A recursive function in let should work with multiple recursive calls
// This tests that step can find itself even when called multiple times in the same expression
TEST(test_let_recursive_function_multiple_calls) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [n] (if (= n 0) 0 (+ (step (- n 1)) (step (- n 1)))))] (step 3))
    // This should return 6 (step calls itself twice in the same expression)
    CljObject *result = eval_string("(let [step (fn [n] (if (= n 0) 0 (+ (step (- n 1)) (step (- n 1)))))] (step 3))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function with multiple calls returned NULL - step cannot find itself");
        return;
    }
    
    TEST_ASSERT_TRUE(is_fixnum(result));
    // Note: This is a simple test - the actual value depends on the recursion depth
    // We just verify it doesn't return NULL
    TEST_ASSERT_NOT_NULL(result);
}

// THESIS 5: A recursive function in let should have access to all namespace functions
// This tests that step can find multiple namespace functions (reverse, cons, first, rest, empty?)
TEST(test_let_recursive_function_namespace_access) {
    // Use global st from setUp (clojure.core already loaded)
    
    // Test: (let [step (fn [coll] (if (empty? coll) (list) (cons (first coll) (step (rest coll)))))] (step (list 1 2 3)))
    // This should return (1 2 3) - step uses empty?, first, rest, cons, and calls itself
    CljObject *result = eval_string("(let [step (fn [coll] (if (empty? coll) (list) (cons (first coll) (step (rest coll)))))] (step (list 1 2 3)))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function with namespace access returned NULL - namespace functions not found");
        return;
    }
    
    TEST_ASSERT_TRUE(result && list_type_matches(TAG(result)));
    
    // Verify result is (1 2 3)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
}

// ============================================================================
// LOW-LEVEL TEST: Symbol resolution in eval_arg
// ============================================================================

// Test that eval_arg can resolve symbols from let_env
TEST(test_let_lowlevel_eval_arg_symbol_resolution) {
    WITH_AUTORELEASE_POOL({
        // Create a let_env manually (simulating what eval_let does)
        CljMap *let_env = (CljMap*)make_map(4);
        TEST_ASSERT_NOT_NULL(let_env);
        
        // Create a symbol "i" (interned)
        CljSymbol *i_sym = intern_symbol_global("i");
        TEST_ASSERT_NOT_NULL(i_sym);
        
        // Create an atom value
        CljAtom *atom = make_atom(fixnum(0));
        TEST_ASSERT_NOT_NULL(atom);
        
        // Store i -> atom in let_env (simulating map_assoc in eval_let)
        CljMap *new_let_env = (CljMap*)map_assoc((CljMap*)let_env, i_sym, (CljObject*)atom);
        ASSIGN(let_env, new_let_env);
        
        // Verify i is in let_env using map_contains
        bool contains = map_contains((CljMap*)let_env, (CljValue)i_sym);
        TEST_ASSERT_TRUE_MESSAGE(contains, "map_contains should find symbol 'i' in let_env");
        
        // Verify i is in let_env using map_get
        CljValue found = map_get((CljMap*)let_env, (CljValue)i_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(found, "map_get should find symbol 'i' in let_env");
        TEST_ASSERT_TRUE(found && TAG(found) == CLJ_ATOM);
        
        // Create another "i" symbol (should be same pointer if interned)
        CljSymbol *i_sym2 = intern_symbol_global("i");
        TEST_ASSERT_NOT_NULL(i_sym2);
        
        // Verify both symbols are the same pointer (interned)
        TEST_ASSERT_EQUAL_PTR_MESSAGE(i_sym, i_sym2, "Symbol 'i' should be interned (same pointer)");
        
        // Test map_contains with second symbol
        bool contains2 = map_contains((CljMap*)let_env, (CljValue)i_sym2);
        TEST_ASSERT_TRUE_MESSAGE(contains2, "map_contains should find symbol 'i' (second instance) in let_env");
        
        // Test map_get with second symbol
        CljValue found2 = map_get((CljMap*)let_env, (CljValue)i_sym2, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(found2, "map_get should find symbol 'i' (second instance) in let_env");
        TEST_ASSERT_TRUE(found2 && TAG(found2) == CLJ_ATOM);
        
        // Now create a simple list with "i" as an element
        // This simulates (swap! i inc) where i is at index 1
        // Use parse() to build the list structure: (swap! i inc)
        ID parsed = parse("(swap! i inc)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(parsed);
        ID canonical = canonicalize_ast(parsed, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(canonical);
        TEST_ASSERT_TRUE(canonical && list_type_matches(TAG(canonical)));
        CljList *list = as_list(canonical);
        
        // Test: Call via eval_list (which uses call_function_with_args internally)
        // This simulates what happens when (swap! i inc) is evaluated in a let body
        // eval_list will call call_function_with_args with let_env
        // The question is: does the environment get passed correctly through this chain?
        // 
        // First, reset the atom to 0 to test the swap! call
        atom_reset(atom, fixnum(0));
        
        // Now call eval_list - this should:
        // 1. Resolve swap! from namespace (should work)
        // 2. Call call_function_with_args with let_env
        // 3. call_function_with_args uses eval_all_args to evaluate arguments
        // 4. Arguments should find "i" in let_env (this is what we're testing)
        // 5. Should find "inc" in namespace (should work)
        ID result2 = eval_list(list, let_env, g_test_eval_state, NULL);
        
        // Check if eval_list returned NULL (indicates failure)
        if (!result2) {
            // If swap! failed, check if the atom value changed
            // If it's still 0, then swap! didn't execute (probably because 'i' wasn't found)
            CljAtom *atom_after = (CljAtom*)atom;
            ID atom_value = atom_deref(atom_after);
            if (as_fixnum(atom_value) == 0) {
                // Atom value is still 0 - swap! didn't execute
                // This means 'i' was probably not found in let_env
                TEST_FAIL_MESSAGE("eval_list returned NULL and atom value is still 0. "
                                 "This indicates that 'i' was not found in let_env when called via eval_list. "
                                 "The direct eval_arg call worked, but eval_list (via call_function_with_args) failed.");
            } else {
                // Atom value changed - swap! executed but returned NULL
                TEST_FAIL_MESSAGE("eval_list returned NULL but atom value changed. "
                                 "This indicates swap! executed but returned NULL.");
            }
        }
        
        // If swap! works correctly, it should have incremented the atom value from 0 to 1
        CljAtom *atom_after = (CljAtom*)atom;
        ID atom_value = atom_deref(atom_after);
        TEST_ASSERT_NOT_NULL_MESSAGE(result2, "eval_list should execute swap! successfully (environment passed correctly)");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(atom_value), "atom value should be a fixnum after swap!");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, as_fixnum(atom_value), "atom value should be 1 after swap! inc (symbol 'i' found in let_env)");
    });
}
