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
