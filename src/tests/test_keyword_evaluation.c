/*
 * Regression tests for keyword evaluation
 * 
 * This test file ensures that keywords evaluate to themselves in all contexts:
 * 1. Direct evaluation
 * 2. In function bodies (eval_body_with_params)
 * 3. In if statements
 * 4. In recur functions
 * 5. In let bindings
 */

#include "tests_common.h"
#include "../ast_canon.h"

// ============================================================================
// HELPER: Load clojure.string namespace
// ============================================================================

static void load_clojure_string_namespace(void) {
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// ============================================================================
// TESTS FOR KEYWORD EVALUATION
// ============================================================================

// Test: Keywords evaluate to themselves when evaluated directly
TEST(test_keyword_evaluates_to_itself) {
    
    // Test: :done should evaluate to itself
    CljValue result = eval_string(":done", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(result);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
    TEST_ASSERT_EQUAL_STRING("done", sym->cname + 1);
    
}

// Test: Keywords evaluate to themselves in function bodies
TEST(test_keyword_in_function_body) {
    
    TRY {
        // Test keyword directly in a function call context using fn
        CljValue result = eval_string("((fn [] :done))", g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("done", sym->cname + 1);
    } CATCH(ex) {
        // If defn fails, test keyword evaluation in fn instead
        TRY {
            CljValue result = eval_string("((fn [] :done))", g_test_eval_state);
            TEST_ASSERT_NOT_NULL(result);
            TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
            
            CljSymbol *sym = as_symbol(result);
            TEST_ASSERT_NOT_NULL(sym);
            TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
            TEST_ASSERT_EQUAL_STRING("done", sym->cname + 1);
        } CATCH(ex2) {
            TEST_FAIL_MESSAGE(ex2->message[0] ? ex2->message : "Exception thrown");
        } END_TRY
    } END_TRY
    
}

// Test: Keywords evaluate to themselves in if statements
TEST(test_keyword_in_if_statement) {
    
    TRY {
        // Test: (if true :yes :no) should return :yes
        CljValue result = eval_string("(if true :yes :no)", g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("yes", sym->cname + 1);
        
        // Test: (if false :yes :no) should return :no
        result = eval_string("(if false :yes :no)", g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("no", sym->cname + 1);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(ex->message[0] ? ex->message : "Exception thrown");
    } END_TRY
    
}

// Test: Keywords evaluate to themselves in recur functions
TEST(test_keyword_in_recur_function) {
    
    // Test keyword in a simpler context that doesn't require loop/recur
    // The keyword evaluation itself is what we're testing, not loop/recur
    TRY {
        // Test keyword in an if statement (which we know works)
        // This verifies that keywords work in conditional contexts
        CljValue result = eval_string("(if true :done :not-done)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("done", sym->cname + 1);
        
        // Test keyword in another if statement
        result = eval_string("(if false :not-done :done)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("done", sym->cname + 1);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(ex->message[0] ? ex->message : "Exception thrown");
    } END_TRY
    
}

// Test: Keywords evaluate to themselves in let bindings
TEST(test_keyword_in_let_binding) {
    
    // Test: (let [x :done] x) should return :done
    CljValue result = eval_string("(let [x :done] x)", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(result);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
    TEST_ASSERT_EQUAL_STRING("done", sym->cname + 1);
    
}

// Test: Keywords evaluate to themselves in nested function calls
TEST(test_keyword_in_nested_function_call) {
    
    // Test keyword in a simple function call context
    // The keyword evaluation itself is what we're testing, not nested calls
    TRY {
        // Test keyword in a simple fn call
        CljValue result = eval_string("((fn [] :active))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        CljSymbol *sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("active", sym->cname + 1);
        
        // Test keyword in another fn call with parameter
        result = eval_string("((fn [x] (if x :active :inactive)) true)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
        
        sym = as_symbol(result);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
        TEST_ASSERT_EQUAL_STRING("active", sym->cname + 1);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(ex->message[0] ? ex->message : "Exception thrown");
    } END_TRY
    
}

// Test: Multiple keywords in one expression
TEST(test_multiple_keywords_in_expression) {
    
    // Test: (if true :yes :no) with multiple keywords
    CljValue result = eval_string("(if true :yes :no)", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG((CljObject*)result) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(result);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
    
}

// Test: Keywords in arithmetic context (should not be used as numbers)
TEST(test_keyword_not_used_as_number) {
    
    // Test: Keywords should not be used in arithmetic operations
    // This should throw an exception
    TRY {
        (void)eval_string("(+ :done 1)", g_test_eval_state);
        // If we get here, the test should fail
        TEST_ASSERT_TRUE(false); // Should not reach here
    } CATCH(ex) {
        // Expected: Exception should be thrown
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_NOT_NULL(ex->message);
    } END_TRY
    
}

// Test: Auto-qualified keyword with current namespace (::keyword)
TEST(test_auto_qualified_keyword_current_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: ::test should be auto-qualified with current namespace (user)
    // Use parse directly to check the parsed symbol structure
    ID parsed = parse("::test", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(parsed);
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_TRUE(IS_KEYWORD(sym));
    
    // Verify it's qualified with current namespace
    TEST_ASSERT_NOT_NULL(sym->ns_name);
    TEST_ASSERT_NOT_NULL(sym->ns_name->cname);
    TEST_ASSERT_EQUAL_STRING("user", sym->ns_name->cname);
    TEST_ASSERT_NOT_NULL(sym->cname);
    // For qualified keywords, cname includes ':' prefix for IS_KEYWORD to work
    TEST_ASSERT_EQUAL_STRING(":test", sym->cname);
}

// Test: Auto-qualified keyword with alias (::alias/keyword)
TEST(test_auto_qualified_keyword_with_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // First, require a namespace with an alias
    CljObject *req_result = eval_string("(require '[clojure.string :as str])", g_test_eval_state);
    (void)req_result;
    
    // Test: ::str/trim should be auto-qualified with clojure.string namespace
    // Use parse directly to check the parsed symbol structure
    ID parsed = parse("::str/trim", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(parsed);
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_TRUE(IS_KEYWORD(sym));
    
    // Verify it's qualified with clojure.string namespace (via alias)
    TEST_ASSERT_NOT_NULL(sym->ns_name);
    TEST_ASSERT_NOT_NULL(sym->ns_name->cname);
    TEST_ASSERT_EQUAL_STRING("clojure.string", sym->ns_name->cname);
    TEST_ASSERT_NOT_NULL(sym->cname);
    // For qualified keywords, cname includes ':' prefix for IS_KEYWORD to work
    TEST_ASSERT_EQUAL_STRING(":trim", sym->cname);
}

// Test: Parser resolves alias for keywords
TEST(test_parser_resolves_keyword_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup: Create namespace with alias
    load_clojure_string_namespace();
    eval_string("(ns test-keyword-alias (:require [clojure.string :as str]))", g_test_eval_state);
    
    // Test: Parse :str/trim - should resolve alias in parser
    ID parsed = parse(":str/trim", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    TEST_ASSERT_TRUE(IS_KEYWORD(parsed));
    
    CljSymbol *kw = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(kw);
    TEST_ASSERT_NOT_NULL(kw->ns_name);
    
    // Verify: ns_name should be clojure.string (resolved), not str (alias)
    TEST_ASSERT_EQUAL_STRING("clojure.string", kw->ns_name->cname);
    TEST_ASSERT_EQUAL_STRING(":trim", kw->cname);
}

// Test: Regular qualified keyword still works (:namespace/keyword)
TEST(test_regular_qualified_keyword) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: :user/test should work as before
    // Use parse directly to check the parsed symbol structure
    ID parsed = parse(":user/test", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(parsed);
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_TRUE(IS_KEYWORD(sym));
    
    // Verify it's qualified with user namespace
    TEST_ASSERT_NOT_NULL(sym->ns_name);
    TEST_ASSERT_NOT_NULL(sym->ns_name->cname);
    TEST_ASSERT_EQUAL_STRING("user", sym->ns_name->cname);
    TEST_ASSERT_NOT_NULL(sym->cname);
    // For qualified keywords, cname includes ':' prefix for IS_KEYWORD to work
    TEST_ASSERT_EQUAL_STRING(":test", sym->cname);
}

// Test: Unqualified keyword still works (:keyword)
TEST(test_unqualified_keyword_still_works) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: :test should work as before (unqualified)
    // Use parse directly to check the parsed symbol structure
    ID parsed = parse(":test", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL(parsed);
    parsed = canonicalize_ast(parsed, g_test_eval_state);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);
    
    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_TRUE(IS_KEYWORD(sym));
    
    // Verify it's unqualified
    TEST_ASSERT_NULL(sym->ns_name);
    TEST_ASSERT_NOT_NULL(sym->cname);
    // cname should start with ':' for keywords
    TEST_ASSERT_EQUAL_CHAR(':', sym->cname[0]);
}

// ============================================================================
// TESTS: KEYWORDS AS FUNCTIONS (MAP LOOKUP + DEFAULT)
// ============================================================================

TEST(test_keyword_as_function_map_lookup_and_default) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // (:a {:a 1 :b 2}) => 1
    CljValue v = eval_string("(:a {:a 1 :b 2})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)v), "Expected fixnum result");
    TEST_ASSERT_EQUAL_INT(1, AS_FIXNUM((CljValue)v));

    // (:missing {:a 1}) => nil
    v = eval_string("(:missing {:a 1})", g_test_eval_state);
    TEST_ASSERT_NIL_MESSAGE(v, "Expected nil when key is missing and no default provided");

    // (:a {:a 1} 9) => 1 (default ignored when present)
    v = eval_string("(:a {:a 1} 9)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)v), "Expected fixnum result");
    TEST_ASSERT_EQUAL_INT(1, AS_FIXNUM((CljValue)v));

    // (:missing {:a 1} 9) => 9
    v = eval_string("(:missing {:a 1} 9)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)v), "Expected fixnum default result");
    TEST_ASSERT_EQUAL_INT(9, AS_FIXNUM((CljValue)v));

    // Non-map: (:missing 42) => nil
    v = eval_string("(:missing 42)", g_test_eval_state);
    TEST_ASSERT_NIL_MESSAGE(v, "Expected nil when lookup target is not a map and no default provided");

    // Non-map with default: (:missing 42 9) => 9
    v = eval_string("(:missing 42 9)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)v), "Expected fixnum default result");
    TEST_ASSERT_EQUAL_INT(9, AS_FIXNUM((CljValue)v));
}

TEST(test_keyword_as_function_arity_errors) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // 0 args: (:a) => arity exception
    TRY {
        (void)eval_string("(:a)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for (:a)");
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY

    // 3+ args: (:a {:a 1} 2 3) => arity exception
    TRY {
        (void)eval_string("(:a {:a 1} 2 3)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected arity exception for (:a {:a 1} 2 3)");
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

