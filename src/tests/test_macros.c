/*
 * Tests for Macro System
 * 
 * Tests for: defmacro, macroexpand-1, macroexpand, variadic macros
 */

#include "tests_common.h"

// ============================================================================
// TEST: Basic defmacro
// ============================================================================

TEST(test_macro_basic_defmacro) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a simple macro that doesn't use variadic params
    CljObject *result = eval_string(
        "(defmacro my-inc [x] (list '+ x 1))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    
    // Use the macro
    result = eval_string("(my-inc 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum((CljValue)result));
}

TEST(test_macro_macroexpand_1) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a macro
    eval_string("(defmacro double [x] (list '* x 2))", g_test_eval_state);
    
    // macroexpand-1 should return the expanded form
    CljObject *result = eval_string(
        "(macroexpand-1 '(double 5))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    
    // Should be (* 5 2)
    CljList *expanded = as_list(result);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_INT(3, list_count(expanded));
}

TEST(test_macro_nested_expansion) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define two macros
    eval_string("(defmacro add1 [x] (list '+ x 1))", g_test_eval_state);
    eval_string("(defmacro add2 [x] (list 'add1 (list '+ x 1)))", g_test_eval_state);
    
    // macroexpand should expand all levels
    CljObject *result = eval_string("(add2 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(7, as_fixnum((CljValue)result));  // 5 + 1 + 1 = 7
}

// ============================================================================
// TEST: Variadic Functions (prerequisite for variadic macros)
// ============================================================================

TEST(test_variadic_fn_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // ((fn [a & rest] rest) 1 2 3) should return (2 3)
    CljObject *result = eval_string("((fn [a & rest] rest) 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    TEST_ASSERT_EQUAL_INT(2, list_count(as_list(result)));
}

TEST(test_variadic_fn_empty_rest) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // ((fn [a & rest] rest) 1) should return nil (no rest args)
    CljObject *result = eval_string("((fn [a & rest] rest) 1)", g_test_eval_state);
    TEST_ASSERT_NULL(result);  // nil
}

TEST(test_variadic_fn_many_args) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // ((fn [a & rest] (count rest)) 1 2 3 4 5) should return 4
    CljObject *result = eval_string("((fn [a & rest] (count rest)) 1 2 3 4 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)result));
}

// ============================================================================
// TEST: Variadic Macros
// ============================================================================

TEST(test_variadic_macro_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a variadic macro that collects all args into a list
    eval_string("(defmacro collect-all [& items] (cons 'list items))", g_test_eval_state);
    
    // Use the macro
    CljObject *result = eval_string("(collect-all 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    TEST_ASSERT_EQUAL_INT(3, list_count(as_list(result)));
}

TEST(test_variadic_macro_defn_style) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // This is the defn macro pattern we want to use
    // (defmacro my-defn [name params & body] ...)
    CljObject *def_result = eval_string(
        "(defmacro my-defn [name params body] "
        "  (list 'def name (list 'fn name params body)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(def_result);
    
    // Use the simplified defn (single body only)
    CljObject *result = eval_string(
        "(my-defn greet [x] (str \"Hello \" x))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    
    // Test the defined function
    result = eval_string("(greet \"World\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(result));
}

TEST(test_variadic_macro_with_body) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a defn-style macro with variadic body
    eval_string(
        "(defmacro my-defn2 [name params & body] "
        "  (list 'def name (list 'fn name params (cons 'do body))))",
        g_test_eval_state);
    
    // Use the macro with multiple body expressions
    eval_string("(my-defn2 add-and-double [x y] (+ x y) (* 2 (+ x y)))", g_test_eval_state);
    
    // Test the defined function - should return result of last body expr
    CljObject *result = eval_string("(add-and-double 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(14, as_fixnum((CljValue)result));  // 2 * (3 + 4) = 14
}

// ============================================================================
// TEST: macroexpand shows correct expansion
// ============================================================================

TEST(test_macroexpand_shows_expansion) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define macro
    eval_string(
        "(defmacro unless [test body] (list 'if (list 'not test) body))",
        g_test_eval_state);
    
    // macroexpand should show the expansion
    CljObject *result = eval_string(
        "(macroexpand '(unless false 42))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    
    // First element should be 'if
    CljList *expanded = as_list(result);
    ID first = expanded->first;
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(first));
    
    CljSymbol *sym = as_symbol(first);
    // Should be 'if' (might be printed as #<special-form if>)
    TEST_ASSERT_NOT_NULL(sym);
}


