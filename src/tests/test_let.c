/*
 * Unity Tests for (let) bindings in Tiny-CLJ
 * 
 * Test-First: These tests are written before implementing let functionality
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "memory.h"
#include "../namespace.h"
#include "../symbol.h"
#include "../atom.h"
#include "../list.h"
#include "../to_string.h"  // for pr_str
#include "strings.h"    // for string_data
#include "../function.h"   // for as_function, CljFunction
#include <string.h>        // for strstr

// ============================================================================
// TEST: Basic let binding
// ============================================================================
TEST(test_let_basic_binding) {
    // Test: (let [x 10] x) should return 10
    const char *code = "(let [x 10] x)";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
}

// ============================================================================
// TEST: Multiple bindings
// ============================================================================
TEST(test_let_multiple_bindings) {
    // Test: (let [x 10 y 20] (+ x y)) should return 30
    const char *code = "(let [x 10 y 20] (+ x y))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
}

// ============================================================================
// TEST: Sequential bindings (later bindings can use earlier ones)
// ============================================================================
TEST(test_let_sequential_bindings) {
    // Test: (let [x 10 y (+ x 5)] y) should return 15
    const char *code = "(let [x 10 y (+ x 5)] y)";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
}

// ============================================================================
// TEST: Let with expression body
// ============================================================================
TEST(test_let_expression_body) {
    // Test: (let [x 5 y 3] (* x y)) should return 15
    const char *code = "(let [x 5 y 3] (* x y))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
}

// ============================================================================
// TEST: Let with multiple body expressions (implicit do)
// ============================================================================
TEST(test_let_multiple_body_expressions) {
    // Test: (let [x 10] (+ x 1) (+ x 2)) should return 12 (last expression)
    const char *code = "(let [x 10] (+ x 1) (+ x 2))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(12, as_fixnum(result));
}

// ============================================================================
// TEST: Let closures capture stack-based bindings
// ============================================================================
TEST(test_let_closure_captures_value) {
    const char *code = "(let [x 10 f (fn [] x)] (f))";
    CljValue result = eval_string(code, g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
}

// ============================================================================
// TEST: Let supports up to 16 frame bindings
// ============================================================================
TEST(test_let_sixteen_frame_bindings) {
    const char *code =
        "(let [a0 0 a1 1 a2 2 a3 3 a4 4 a5 5 a6 6 a7 7 "
              "a8 8 a9 9 a10 10 a11 11 a12 12 a13 13 a14 14 a15 15]"
              " (+ a0 a15))";
    CljValue result = eval_string(code, g_test_eval_state);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
}

// ============================================================================
// TEST: Nested let
// ============================================================================
TEST(test_let_nested) {
    // Test: (let [x 10] (let [y 20] (+ x y))) should return 30
    const char *code = "(let [x 10] (let [y 20] (+ x y)))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
}

// ============================================================================
// TEST: Let shadowing outer binding
// ============================================================================
TEST(test_let_shadowing) {
    // Test: (let [x 10] (let [x 20] x)) should return 20 (inner shadows outer)
    const char *code = "(let [x 10] (let [x 20] x))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
}

// ============================================================================
// TEST: Closure capture must be stable under shadowing
// ============================================================================
TEST(test_let_shadowing_closure_capture) {
    // Test:
    // (let [x 1]
    //   (let [f (fn [] x)]
    //     (let [x 2]
    //       (f))))  => 1
    const char *code = "(let [x 1] (let [f (fn [] x)] (let [x 2] (f))))";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// ============================================================================
// TEST: Let symbol in arithmetic operation
// ============================================================================
TEST(test_let_symbol_in_arithmetic) {
    // Test: (let [result 2] (+ 1 result)) should return 3
    const char *code = "(let [result 2] (+ 1 result))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

// ============================================================================
// TEST: Let with function calls
// ============================================================================
TEST(test_let_with_function_calls) {
    // Define a function first
    eval_string("(def square (fn [x] (* x x)))", g_test_eval_state);
    
    // Test: (let [x 5] (square x)) should return 25
    const char *code = "(let [x 5] (square x))";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
}

// ============================================================================
// TEST: Let with empty bindings
// ============================================================================
TEST(test_let_empty_bindings) {
    // Test: (let [] 42) should return 42
    const char *code = "(let [] 42)";
    CljValue result = eval_string(code, g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

// ============================================================================
// Tests for if-let macro
// ============================================================================

TEST(test_if_let_basic) {
    // Test: (if-let [x 42] x nil) => 42
    CljObject *result = eval_string("(if-let [x 42] x nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_if_let_false_condition) {
    // Test: (if-let [x false] x 100) => 100
    CljObject *result = eval_string("(if-let [x false] x 100)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(result));
}

TEST(test_if_let_nil_condition) {
    // Test: (if-let [x nil] x 200) => 200
    CljObject *result = eval_string("(if-let [x nil] x 200)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(200, as_fixnum(result));
}

TEST(test_if_let_without_else) {
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
    // Test: (let [step (fn [x] (+ x 1))] (step 5)) => 6
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_with_local_function_multiple_calls) {
    // Test: (let [step (fn [x] (+ x 1))] (+ (step 5) (step 10))) => 17
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (+ (step 5) (step 10)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(17, as_fixnum(result));
}

TEST(test_let_with_local_function_using_namespace_function) {
    // Test: (let [step (fn [x] (+ x 1))] (step 5)) => 6
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_with_local_function_using_reverse) {
    // Test: (let [step (fn [coll] (reverse coll))] (step (list 1 2 3)))
    CljObject *result = eval_string("(let [step (fn [coll] (reverse coll))] (step (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify first element is 3
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

TEST(test_let_verify_step_binding) {
    // (let [step (fn [x] (+ x 1))] step) should return the function itself
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] step)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_FUNC || TAG(result) == CLJ_CLOSURE);
}

TEST(test_let_verify_step_callable) {
    // (let [step (fn [x] (+ x 1))] (step 5)) should return 6
    CljObject *result = eval_string("(let [step (fn [x] (+ x 1))] (step 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_verify_step_binding_order) {
    // (let [step (fn [x] x)] (let [result (step 42)] result)) tests nested let
    CljObject *result = eval_string("(let [step (fn [x] x)] (let [result (step 42)] result))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_let_inside_function) {
    // Define a function that uses let internally
    eval_string("(def test-filter (fn [pred coll] (let [step (fn [x] (+ x 1))] (step 5))))", g_test_eval_state);
    
    // Call the function
    CljObject *result = eval_string("(test-filter even? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

TEST(test_let_filter_pattern) {
    // Test pattern similar to filter
    eval_string("(def test-filter-pattern (fn [pred coll] (let [step (fn [x] x)] (step 42))))", g_test_eval_state);
    
    CljObject *result = eval_string("(test-filter-pattern even? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_let_filter_function_call) {
    // Test if filter function exists and can be called
    CljObject *filter_fn = eval_string("filter", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(filter_fn);
    TEST_ASSERT_TRUE(TAG(filter_fn) == CLJ_FUNC || TAG(filter_fn) == CLJ_CLOSURE);
    
    // Test a simple filter call
    CljObject *result = eval_string("(filter (fn [x] true) (list 1 2 3))", g_test_eval_state);
    if (!result) {
        TEST_FAIL_MESSAGE("filter with (fn [x] true) returned NULL");
        return;
    }
    CljObject *count_result = eval_string("(count (filter (fn [x] true) (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    TEST_ASSERT_TRUE(is_fixnum(count_result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(count_result));
    
    // Test if even? works
    CljObject *even_result = eval_string("(even? 2)", g_test_eval_state);
    if (!even_result) {
        TEST_FAIL_MESSAGE("even? 2 returned NULL");
        return;
    }
    TEST_ASSERT_TRUE(clj_is_truthy(even_result));
    
    CljObject *odd_result = eval_string("(even? 1)", g_test_eval_state);
    if (!odd_result) {
        TEST_FAIL_MESSAGE("even? 1 returned NULL");
        return;
    }
    TEST_ASSERT_FALSE(clj_is_truthy(odd_result));
    
    // Test with even?
    CljObject *result2 = eval_string("(filter even? (list 1 2 3))", g_test_eval_state);
    if (!result2) {
        TEST_FAIL_MESSAGE("filter with even? returned NULL");
        return;
    }
    CljObject *count_result2 = eval_string("(count (filter even? (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result2);
    TEST_ASSERT_TRUE(is_fixnum(count_result2));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(count_result2));
    TEST_ASSERT_TRUE(result2 && is_list_type(TAG(result2)));
    
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
// TESTS: Recursive functions in let bindings
// ============================================================================

TEST(test_let_recursive_function_self_reference) {
    // Test: recursive sum function
    CljObject *result = eval_string("(let [step (fn [n acc] (if (= n 0) acc (step (- n 1) (+ acc n))))] (step 5 0))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function in let returned NULL - step cannot find itself");
        return;
    }
    
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(15, as_fixnum(result));
}

TEST(test_let_recursive_function_with_namespace_function) {
    // Test: recursive function using reverse
    CljObject *result = eval_string("(let [step (fn [coll acc] (if (empty? coll) (reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function with reverse returned NULL");
        return;
    }
    
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
}

TEST(test_let_filter_step_pattern) {
    // Test: filter step pattern
    CljObject *result = eval_string("(let [step (fn [pred coll acc] (if (empty? coll) (if (empty? acc) nil (reverse acc)) (if (pred (first coll)) (step pred (rest coll) (cons (first coll) acc)) (step pred (rest coll) acc))))] (step even? (list 1 2 3) (list)))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Filter step pattern returned NULL");
        return;
    }
    
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)list->first));
}

TEST(test_let_recursive_function_multiple_calls) {
    // Test: recursive function with multiple self-calls
    CljObject *result = eval_string("(let [step (fn [n] (if (= n 0) 0 (+ (step (- n 1)) (step (- n 1)))))] (step 3))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function with multiple calls returned NULL");
        return;
    }
    
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_NOT_NULL(result);
}

TEST(test_let_recursive_function_namespace_access) {
    // Test: recursive function using multiple namespace functions
    CljObject *result = eval_string("(let [step (fn [coll] (if (empty? coll) (list) (cons (first coll) (step (rest coll)))))] (step (list 1 2 3)))", g_test_eval_state);
    
    if (!result) {
        TEST_FAIL_MESSAGE("Recursive function with namespace access returned NULL");
        return;
    }
    
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
}

// ============================================================================
// LOW-LEVEL TEST: Symbol resolution in eval_arg (needs manual memory management)
// ============================================================================

TEST(test_let_lowlevel_eval_arg_symbol_resolution) {
    WITH_AUTORELEASE_POOL({
        // Create a let_env manually (simulating what eval_let does)
        CljPersistentMap *let_env = (CljPersistentMap*)make_map(4);
        TEST_ASSERT_NOT_NULL(let_env);
        
        CljSymbol *i_sym = intern_symbol_global("i");
        TEST_ASSERT_NOT_NULL(i_sym);
        
        CljAtom *atom = make_atom(fixnum(0));
        TEST_ASSERT_NOT_NULL(atom);
        
        CljPersistentMap *new_let_env = (CljPersistentMap*)map_assoc((CljPersistentMap*)let_env, i_sym, (CljObject*)atom);
        ASSIGN(let_env, new_let_env);
        
        bool contains = map_contains((CljPersistentMap*)let_env, (CljValue)i_sym);
        TEST_ASSERT_TRUE_MESSAGE(contains, "map_contains should find symbol 'i' in let_env");
        
        CljValue found = map_get_sentinel((CljPersistentMap*)let_env, (CljValue)i_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(found, "map_get should find symbol 'i' in let_env");
        TEST_ASSERT_TRUE(TAG(found) == CLJ_ATOM);
        
        CljSymbol *i_sym2 = intern_symbol_global("i");
        TEST_ASSERT_EQUAL_PTR_MESSAGE(i_sym, i_sym2, "Symbol 'i' should be interned (same pointer)");
        
        ID parsed = parse("(swap! i inc)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(parsed);
        ID canonical = canonicalize_ast(parsed, g_test_eval_state);
        TEST_ASSERT_NOT_NULL(canonical);
        
        atom_reset(atom, fixnum(0));
        
        ID result2 = eval_body(canonical, let_env, g_test_eval_state, NULL);
        
        if (!result2) {
            CljAtom *atom_after = (CljAtom*)atom;
            ID atom_value = atom_deref(atom_after);
            if (as_fixnum(atom_value) == 0) {
                TEST_FAIL_MESSAGE("eval_body returned NULL and atom value is still 0.");
            } else {
                TEST_FAIL_MESSAGE("eval_body returned NULL but atom value changed.");
            }
        }
        
        CljAtom *atom_after = (CljAtom*)atom;
        ID atom_value = atom_deref(atom_after);
        TEST_ASSERT_NOT_NULL_MESSAGE(result2, "eval_body should execute swap! successfully");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(atom_value), "atom value should be a fixnum after swap!");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, as_fixnum(atom_value), "atom value should be 1 after swap! inc");
    });
}

// ============================================================================
// TEST: TCO transformation for let-bound tail-recursive functions
// ============================================================================

TEST(test_let_tco_recur_synthesized) {
    // Define a tail-recursive function and get the function object
    CljObject *fn_obj = eval_string(
        "(let [f (fn f [n acc] (if (= n 0) acc (f (- n 1) (+ acc n))))] f)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_obj, "let should return the function");
    TEST_ASSERT_TRUE_MESSAGE(TAG(fn_obj) == CLJ_CLOSURE, "should be a closure");
    
    // Get the function body and check it contains 'recur'
    CljFunction *func = as_function(fn_obj);
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "as_function should return the function");
    TEST_ASSERT_NOT_NULL_MESSAGE(func->body, "function should have a body");
    
    // Convert body to string to check for recur
    CljString *body_str = pr_str(func->body);
    TEST_ASSERT_NOT_NULL_MESSAGE(body_str, "pr_str should return a CljString");
    const char *source_str = string_data((CljObject*)body_str);
    TEST_ASSERT_NOT_NULL_MESSAGE(source_str, "string_data should return a C string");
    
    // Check that 'recur' appears in the source (TCO transformation happened)
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(source_str, "recur"),
        "TCO: tail-recursive call should be transformed to recur");
    
    // Check that the original function name does NOT appear in tail position
    TEST_ASSERT_NULL_MESSAGE(strstr(source_str, "(f (- n 1)"),
        "TCO: original recursive call (f ...) should be replaced by (recur ...)");
}

TEST(test_let_tco_deep_recursion) {
    // Without TCO, this would cause stack overflow
    CljObject *result = eval_string(
        "(let [sum (fn sum [n acc] (if (= n 0) acc (sum (- n 1) (+ acc n))))] (sum 10000 0))",
        g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "TCO should allow deep recursion without stack overflow");
    TEST_ASSERT_TRUE(is_fixnum(result));
    // Sum of 1 to 10000 = 10000 * 10001 / 2 = 50005000
    TEST_ASSERT_EQUAL_INT(50005000, as_fixnum(result));
}
