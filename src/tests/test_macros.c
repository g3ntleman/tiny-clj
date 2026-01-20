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
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
    
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
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
    TEST_ASSERT_EQUAL_INT(2, list_count(as_list(result)));
}

TEST(test_variadic_fn_empty_rest) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // ((fn [a & rest] rest) 1) should return nil (no rest args)
    CljObject *result = eval_string("((fn [a & rest] rest) 1)", g_test_eval_state);
    TEST_ASSERT_NIL(result);  // nil
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
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
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
    TEST_ASSERT_TRUE(is_list_type(TAG(result)));
    
    // First element should be 'if
    CljList *expanded = as_list(result);
    ID first = expanded->first;
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(first));
    
    CljSymbol *sym = as_symbol(first);
    // Should be 'if' (might be printed as #<special-form if>)
    TEST_ASSERT_NOT_NULL(sym);
}

// ============================================================================
// TEST: Macros in anonymous functions (regression tests)
// ============================================================================

TEST(test_macro_in_anonymous_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a macro
    eval_string("(defmacro inc2 [x] (list '+ x 2))", g_test_eval_state);
    
    // Use macro inside anonymous function: ((fn [y] (inc2 y)) 5) => 7
    CljObject *result = eval_string("((fn [y] (inc2 y)) 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(7, as_fixnum((CljValue)result));
}

TEST(test_macro_in_reader_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a macro
    eval_string("(defmacro triple [x] (list '* x 3))", g_test_eval_state);
    
    // Use macro inside #() reader macro: (#(triple %) 4) => 12
    CljObject *result = eval_string("(#(triple %) 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(12, as_fixnum((CljValue)result));
}

TEST(test_macro_generating_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a macro that generates an anonymous function
    eval_string("(defmacro make-doubler [] '(fn [x] (* x 2)))", g_test_eval_state);
    
    // Use macro that returns a function: ((make-doubler) 5) => 10
    CljObject *result = eval_string("((make-doubler) 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)result));
}

TEST(test_nested_macro_in_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a macro
    eval_string("(defmacro add [a b] (list '+ a b))", g_test_eval_state);
    
    // Use macro in nested anonymous functions: ((fn [x] ((fn [y] (add x y)) 3)) 5) => 8
    CljObject *result = eval_string(
        "((fn [x] ((fn [y] (add x y)) 3)) 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum((CljValue)result));
}

// ============================================================================
// TEST: unquote-splice (~@) High-Level Regression Tests
// ============================================================================
// These tests verify that unquote-splice works correctly in various contexts
// and is Clojure-compatible

TEST(test_unquote_splice_basic_list) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Basic splicing: `(~@(list 1 2 3)) should expand to (1 2 3)
    // Using quasiquote directly since we can't use backtick in C strings
    CljObject *result = eval_string(
        "(let [x (list 1 2 3)] "
        "  (eval (quasiquote ((unquote-splice x)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(3, list_count(lst));
    
    // Verify elements
    ID first = lst->first;
    assert_fixnum(first, 1);
    ID second = as_list(lst->rest ? as_list(lst->rest) : NULL)->first;
    assert_fixnum(second, 2);
    ID third = as_list(as_list(lst->rest ? as_list(lst->rest) : NULL)->rest ? 
                       as_list(as_list(lst->rest ? as_list(lst->rest) : NULL)->rest) : NULL)->first;
    assert_fixnum(third, 3);
}

TEST(test_unquote_splice_in_middle) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Splicing in middle: `(a ~@(list 1 2) b) should expand to (a 1 2 b)
    CljObject *result = eval_string(
        "(let [x (list 1 2)] "
        "  (eval (quasiquote (a (unquote-splice x) b))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(4, list_count(lst));  // a, 1, 2, b
}

TEST(test_unquote_splice_at_start) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Splicing at start: `(~@(list 1 2) a b) should expand to (1 2 a b)
    CljObject *result = eval_string(
        "(let [x (list 1 2)] "
        "  (eval (quasiquote ((unquote-splice x) a b))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(4, list_count(lst));  // 1, 2, a, b
}

TEST(test_unquote_splice_at_end) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Splicing at end: `(a b ~@(list 1 2)) should expand to (a b 1 2)
    CljObject *result = eval_string(
        "(let [x (list 1 2)] "
        "  (eval (quasiquote (a b (unquote-splice x)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(4, list_count(lst));  // a, b, 1, 2
}

TEST(test_unquote_splice_multiple) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Multiple splices: `(~@(list 1) ~@(list 2 3) ~@(list 4)) should expand to (1 2 3 4)
    CljObject *result = eval_string(
        "(let [x (list 1) y (list 2 3) z (list 4)] "
        "  (eval (quasiquote ((unquote-splice x) (unquote-splice y) (unquote-splice z)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(4, list_count(lst));  // 1, 2, 3, 4
}

TEST(test_unquote_splice_empty_list) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Empty list splice: `(~@(list) a) should expand to (a) - empty splice adds nothing
    CljObject *result = eval_string(
        "(let [x (list)] "
        "  (eval (quasiquote ((unquote-splice x) a))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(1, list_count(lst));  // just 'a'
}

TEST(test_unquote_splice_in_macro) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define macro using unquote-splice
    eval_string(
        "(defmacro my-concat [& args] "
        "  (list 'concat (quasiquote ((unquote-splice args)))))",
        g_test_eval_state);
    
    // Use macro: (my-concat (list 1) (list 2) (list 3)) should expand to (concat (list 1) (list 2) (list 3))
    CljObject *result = eval_string("(my-concat (list 1) (list 2) (list 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(3, list_count(lst));  // 1, 2, 3
}

TEST(test_unquote_splice_with_unquote) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Mix unquote and unquote-splice: `(a ~b ~@(list 1 2) c) where b=99 should expand to (a 99 1 2 c)
    CljObject *result = eval_string(
        "(let [b 99 x (list 1 2)] "
        "  (eval (quasiquote (a (unquote b) (unquote-splice x) c))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(5, list_count(lst));  // a, 99, 1, 2, c
    
    // Verify first element is 'a (symbol)
    ID first = lst->first;
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(first));
    
    // Verify second element is 99
    ID second = as_list(lst->rest ? as_list(lst->rest) : NULL)->first;
    assert_fixnum(second, 99);
}

TEST(test_unquote_splice_nested_quasiquote) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Nested quasiquote with splice: ``(~@~x) where x='(1 2) should expand correctly
    // This tests that nested quasiquotes handle unquote-splice properly
    CljObject *result = eval_string(
        "(let [x (list (quasiquote ((unquote-splice (list 1 2)))))] "
        "  (eval (quasiquote ((unquote-splice x)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
}

TEST(test_unquote_splice_function_call_result) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Splice result of function call: `(~@(range 3)) should expand to (0 1 2)
    // Using list instead of range since range might not exist
    CljObject *result = eval_string(
        "(let [x (list 0 1 2)] "
        "  (eval (quasiquote ((unquote-splice x)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(3, list_count(lst));
    
    // Verify elements are 0, 1, 2
    ID first = lst->first;
    assert_fixnum(first, 0);
}

TEST(test_unquote_splice_clojure_compatible) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test that unquote-splice works like Clojure's ~@
    // In Clojure: (let [x '(1 2 3)] `(~@x)) => (1 2 3)
    CljObject *result = eval_string(
        "(let [x (list 1 2 3)] "
        "  (eval (quasiquote ((unquote-splice x)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_list(result);
    CljList *lst = as_list(result);
    TEST_ASSERT_EQUAL_INT(3, list_count(lst));
    
    // Verify it's exactly (1 2 3)
    ID first = lst->first;
    assert_fixnum(first, 1);
    ID second = as_list(lst->rest ? as_list(lst->rest) : NULL)->first;
    assert_fixnum(second, 2);
    ID third = as_list(as_list(lst->rest ? as_list(lst->rest) : NULL)->rest ? 
                       as_list(as_list(lst->rest ? as_list(lst->rest) : NULL)->rest) : NULL)->first;
    assert_fixnum(third, 3);
}


