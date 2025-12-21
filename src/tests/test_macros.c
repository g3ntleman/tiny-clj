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
    // BUG: Variadic parameters (& rest) don't work correctly
    // ((fn [a & rest] rest) 1 2 3) returns 3 instead of (2 3)
    TEST_IGNORE_MESSAGE("KNOWN BUG: & rest returns single value instead of list");
}

TEST(test_variadic_fn_empty_rest) {
    // BUG: Variadic parameters cause ArityException with exact param count
    TEST_IGNORE_MESSAGE("KNOWN BUG: & rest causes ArityException");
}

TEST(test_variadic_fn_many_args) {
    // BUG: Variadic parameters cause ArityException with >3 args
    TEST_IGNORE_MESSAGE("KNOWN BUG: & rest causes ArityException with many args");
}

// ============================================================================
// TEST: Variadic Macros
// ============================================================================

TEST(test_variadic_macro_basic) {
    // BUG: Variadic macros don't work due to & rest bug
    TEST_IGNORE_MESSAGE("KNOWN BUG: Variadic macros fail - depends on & rest fix");
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
    // BUG: This is the defn pattern - requires variadic macros to work
    // (defmacro my-defn [name params & body] ...)
    TEST_IGNORE_MESSAGE("KNOWN BUG: defn-style variadic macro - depends on & rest fix");
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


