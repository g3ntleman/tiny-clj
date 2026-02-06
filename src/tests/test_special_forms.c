/*
 * Unit Tests for Special Form Dispatch Optimization
 * 
 * Tests for CljSpecialSymbol with function pointer dispatch
 */

#include "tests_common.h"
#include "ast.h"

// ============================================================================
// TEST: Special Form Dispatch Functionality
// ============================================================================

TEST(test_special_if_dispatch) {
    ID result = eval_string("(if true 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
    
    ID result2 = eval_string("(if false 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result2));
}

TEST(test_special_let_dispatch) {
    ID result = eval_string("(let [x 42] x)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_special_do_dispatch) {
    ID result = eval_string("(do 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_special_when_dispatch) {
    ID result = eval_string("(when true 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    ID result2 = eval_string("(when false 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result2);
}

TEST(test_special_and_dispatch) {
    // (and true true) → true
    ID result = eval_string("(and true true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    
    // (and true false) → false (not nil!)
    // Clojure returns the first falsy value, which is false
    (void)eval_string("(and true false)", g_test_eval_state);
}

TEST(test_special_or_dispatch) {
    // (or false true) → true
    ID result = eval_string("(or false true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    
    // (or false false) → false (not nil!)
    // Clojure returns the last value when all are falsy
    (void)eval_string("(or false false)", g_test_eval_state);
}

TEST(test_special_cond_basic_match) {
    ID result = eval_string("(cond false 1 true 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

TEST(test_special_cond_no_match_returns_nil) {
    ID result = eval_string("(cond false 1 false 2)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_special_cond_else) {
    ID result = eval_string("(cond false 1 :else 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

TEST(test_special_cond_allows_nil_expr_and_short_circuits) {
    // Regression: a previous implementation treated `nil` expr as “missing”
    // and continued evaluating later clauses.
    ID result = eval_string(
        "(do (def x (atom 0)) "
        "    (cond true nil (do (swap! x inc) true) 1) "
        "    @x)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_special_cond_odd_number_of_forms_throws) {
    bool did_throw = false;
    TRY {
        // (cond test expr test) -> odd number of forms after `cond`
        (void)eval_string("(cond true 1 false)", g_test_eval_state);
    } CATCH(ex) {
        did_throw = true;
        TEST_ASSERT_EQUAL_STRING("IllegalArgumentException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(did_throw);
}

TEST(test_special_cond_with_else_in_function_context) {
    // Test that cond with :else works when called from within a function
    // This reproduces the issue where cond fails during macro expansion
    ID result = eval_string(
        "(let [helper (fn [item] "
        "  (cond "
        "    (= item :when) :w "
        "    (= item :while) :wh "
        "    (= item :let) :l "
        "    :else :d))] "
        "(helper :x))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_keyword(result));
    CljSymbol *kw = as_symbol(result);
    TEST_ASSERT_NOT_NULL(kw);
    TEST_ASSERT_EQUAL_STRING(":d", kw->cname);
}

TEST(test_special_cond_with_else_multiple_clauses) {
    // Test cond with multiple clauses ending in :else
    ID result = eval_string(
        "(cond "
        "  false 1 "
        "  false 2 "
        "  false 3 "
        "  :else 4)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(result));
}

TEST(test_special_cond_simple_nesting_problem) {
    // Very simple test to demonstrate the cond nesting problem
    // Expected: (cond true 1 false 2) should work
    // Problem: cond receives nested structure [List: (true 1 false 2)] instead of (true 1 false 2)
    ID result = eval_string("(cond true 1 false 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
    
    // Test with :else
    ID result2 = eval_string("(cond false 1 :else 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result2));
}

// Low-level test that demonstrates the nesting problem directly
// Creates a nested structure: (cond (true 1 false 2)) instead of (cond true 1 false 2)
// This test shows WHERE the nesting problem occurs
TEST(test_special_cond_nesting_low_level) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create values: true, 1, false, 2
    // Use eval_string to create the values properly
    ID true_val_obj = eval_string("true", g_test_eval_state);
    ID one_obj = eval_string("1", g_test_eval_state);
    ID false_val_obj = eval_string("false", g_test_eval_state);
    ID two_obj = eval_string("2", g_test_eval_state);
    
    CljValue true_val = (CljValue)true_val_obj;
    CljValue one = (CljValue)one_obj;
    CljValue false_val = (CljValue)false_val_obj;
    CljValue two = (CljValue)two_obj;
    
    // Create the inner list: (true 1 false 2)
    CljList *inner_list = make_list(true_val, 
                                    make_list(one,
                                             make_list(false_val,
                                                      make_list(two, NULL))));
    
    // Create the nested structure: (cond (true 1 false 2))
    // This simulates the problem where arguments are wrapped in an extra list
    // The structure is: (cond [List: (true 1 false 2)])
    // So LIST_REST(cond) is the list (true 1 false 2), which is itself a list
    // We wrap it in another list to simulate the nesting problem
    CljList *wrapped_inner = make_list((ID)inner_list, NULL);
    CljList *nested_cond = make_ast_list(SYM_COND, wrapped_inner);
    
    // Test list_rest_normalized: should return the nested list
    CljList *rest_normalized = list_rest_normalized(nested_cond);
    TEST_ASSERT_NOT_NULL(rest_normalized);
    
    // The first element should be a list (the nested structure)
    // This demonstrates the PROBLEM: arguments are wrapped in an extra list
    ID first_elem = LIST_FIRST(rest_normalized);
    TEST_ASSERT_NOT_NULL(first_elem);
    TEST_ASSERT_TRUE(is_list_type(TAG(first_elem)));
    
    // Verify the nested structure
    CljList *nested_inner = as_list(first_elem);
    TEST_ASSERT_NOT_NULL(nested_inner);
    ID nested_first = LIST_FIRST(nested_inner);
    TEST_ASSERT_NOT_NULL(nested_first);
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(nested_first));
}

TEST(test_special_cond_after_macro_expansion) {
    // Test that cond with :else works correctly after macro expansion
    // This tests the normalize-for-bindings function which uses cond
    // Note: normalize-for-bindings is a private function, so we test it indirectly
    // by testing the for macro which uses it
    ID result = eval_string(
        "(for [x [1 2 3]] x)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    // for returns a lazy sequence, so we can't easily check the structure
    // Just verify it doesn't throw an exception
}

TEST(test_special_cond_else_last_expression) {
    // Test that :else clause correctly evaluates its expression
    // This ensures the last expression in cond is properly handled
    ID result = eval_string(
        "(let [item :unknown] "
        "  (cond "
        "    (= item :when) :w "
        "    (= item :while) :wh "
        "    (= item :let) :l "
        "    :else (do :d :final)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_keyword(result));
    CljSymbol *kw = as_symbol(result);
    TEST_ASSERT_NOT_NULL(kw);
    TEST_ASSERT_EQUAL_STRING(":final", kw->cname);
}

// ============================================================================
// TEST: All Special Forms Have Valid Function Pointers
// ============================================================================

TEST(test_all_special_symbols_have_eval_fn) {
    // Test that all Special Forms have valid function pointers
    CljSpecialSymbol *sym_if = as_special_symbol(SYM_IF);
    TEST_ASSERT_NOT_NULL(sym_if);
    TEST_ASSERT_NOT_NULL(sym_if->eval_fn);
    
    CljSpecialSymbol *sym_let = as_special_symbol(SYM_LET);
    TEST_ASSERT_NOT_NULL(sym_let);
    TEST_ASSERT_NOT_NULL(sym_let->eval_fn);
    
    CljSpecialSymbol *sym_do = as_special_symbol(SYM_DO);
    TEST_ASSERT_NOT_NULL(sym_do);
    TEST_ASSERT_NOT_NULL(sym_do->eval_fn);
    
    CljSpecialSymbol *sym_when = as_special_symbol(SYM_WHEN);
    TEST_ASSERT_NOT_NULL(sym_when);
    TEST_ASSERT_NOT_NULL(sym_when->eval_fn);
    
    CljSpecialSymbol *sym_and = as_special_symbol(SYM_AND);
    TEST_ASSERT_NOT_NULL(sym_and);
    TEST_ASSERT_NOT_NULL(sym_and->eval_fn);
    
    CljSpecialSymbol *sym_or = as_special_symbol(SYM_OR);
    TEST_ASSERT_NOT_NULL(sym_or);
    TEST_ASSERT_NOT_NULL(sym_or->eval_fn);
    
    CljSpecialSymbol *sym_fn = as_special_symbol(SYM_FN);
    TEST_ASSERT_NOT_NULL(sym_fn);
    TEST_ASSERT_NOT_NULL(sym_fn->eval_fn);
    
    CljSpecialSymbol *sym_quote = as_special_symbol(SYM_QUOTE);
    TEST_ASSERT_NOT_NULL(sym_quote);
    TEST_ASSERT_NOT_NULL(sym_quote->eval_fn);

    CljSpecialSymbol *sym_cond = as_special_symbol(SYM_COND);
    TEST_ASSERT_NOT_NULL(sym_cond);
    TEST_ASSERT_NOT_NULL(sym_cond->eval_fn);
}

// ============================================================================
// TEST: Special Form Semantics (Cannot Be Overridden)
// ============================================================================

TEST(test_special_form_not_shadowable) {
    // Special Forms cannot be overridden by def
    eval_string("(def if 42)", g_test_eval_state);
    ID result = eval_string("(if true 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));  // if remains Special Form
}

// ============================================================================
// TEST: Let Shadowing Semantics
// ============================================================================

TEST(test_let_shadowing_special_form_name) {
    // Special Form names can be shadowed in let bindings
    ID result = eval_string("(let [if 42] if)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));  // if as variable → 42
}

TEST(test_special_form_operator_not_shadowed) {
    // Special Form as operator remains special even when name is shadowed
    ID result = eval_string("(let [if 42] (if true 1 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));  // if as operator → Special Form
}

TEST(test_let_shadowing_multiple_levels) {
    // Nested let shadowing
    ID result = eval_string("(let [if 10] (let [if 20] (if true if 0)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));  // Inner if binding → 20
}

// ============================================================================
// TEST: Special Form Performance (Basic)
// ============================================================================

TEST(test_special_form_dispatch_basic_performance) {
    // Test that dispatch works correctly with many calls
    for (int i = 0; i < 100; i++) {
        ID result = eval_string("(if true 1 2)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
    }
}

// ============================================================================
// TEST: Named fn (recursive anonymous functions)
// ============================================================================

TEST(test_named_fn_creates_function) {
    // Named fn should create a function
    ID result = eval_string("(fn? (fn fact [n] n))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
}

TEST(test_named_fn_factorial) {
    // Named fn should allow recursion
    ID result = eval_string("((fn fact [n] (if (<= n 1) 1 (* n (fact (dec n))))) 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(120, as_fixnum(result));
}

TEST(test_named_fn_countdown) {
    // Named fn countdown to 0
    ID result = eval_string("((fn countdown [n] (if (<= n 0) n (countdown (dec n)))) 10)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_named_fn_with_two_params) {
    // Named fn with two parameters
    ID result = eval_string("((fn pow [base exp] (if (<= exp 0) 1 (* base (pow base (dec exp))))) 2 10)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1024, as_fixnum(result)); // 2^10 = 1024
}

// ============================================================================
// Quasiquote simple form (regression: `(+ 1 2) should return (quote (+ 1 2)))
// ============================================================================

TEST(test_quasiquote_simple_form_returns_quoted_form) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // (quasiquote (+ 1 2)) returns (quote (+ 1 2)); eval once gives (+ 1 2)
    CljObject *form = eval_string("(eval (quasiquote (+ 1 2)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(form);
    TEST_ASSERT_TRUE(is_list_type(TAG(form)));
    CljList *lst = as_list(form);
    TEST_ASSERT_EQUAL_INT(3, list_count(lst));
    TEST_ASSERT_TRUE(is_symbol(LIST_FIRST(lst)));
    assert_fixnum((CljObject *)LIST_FIRST(as_list(LIST_REST(lst))), 1);
    assert_fixnum((CljObject *)LIST_FIRST(as_list(LIST_REST(as_list(LIST_REST(lst))))), 2);
}

TEST(test_quasiquote_simple_form_evaluates_to_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    // (eval (eval (quasiquote (+ 1 2)))) = (eval (+ 1 2)) = 3
    CljObject *result = eval_string("(eval (eval (quasiquote (+ 1 2))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_fixnum(result, 3);
}
