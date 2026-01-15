/*
 * Unit Tests for Special Form Dispatch Optimization
 * 
 * Tests for CljSpecialSymbol with function pointer dispatch
 */

#include "tests_common.h"

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

TEST(test_special_case_basic_match) {
    // (case expr 1 10 2 20 99) => 20
    ID result = eval_string("(case 2 1 10 2 20 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)result));
}

TEST(test_special_case_default_used) {
    // No match => default
    ID result = eval_string("(case 3 1 10 2 20 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum((CljValue)result));
}

TEST(test_special_case_no_default_throws) {
    bool did_throw = false;
    TRY {
        (void)eval_string("(case 3 1 10 2 20)", g_test_eval_state);
    } CATCH(ex) {
        did_throw = true;
        TEST_ASSERT_EQUAL_STRING("IllegalArgumentException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE(did_throw);
}

TEST(test_special_case_multi_keys) {
    // Support list-of-keys and vector-of-keys for one branch.
    ID r1 = eval_string("(case 2 (1 2) 10 3 20 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)r1));

    ID r2 = eval_string("(case 2 [1 2] 10 3 20 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)r2));
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

    CljSpecialSymbol *sym_case = as_special_symbol(SYM_CASE);
    TEST_ASSERT_NOT_NULL(sym_case);
    TEST_ASSERT_NOT_NULL(sym_case->eval_fn);
    
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

    eval_string("(def case 42)", g_test_eval_state);
    ID result2 = eval_string("(case 2 1 10 2 20 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result2));  // case remains Special Form
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
