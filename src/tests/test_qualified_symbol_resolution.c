/*
 * Unity Tests for qualified symbol resolution in Tiny-CLJ
 *
 * Tests for resolving qualified symbols like clojure.core/reverse, clojure.string/blank?, etc.
 */

#include "tests_common.h"
#include "symbol.h"
#include "namespace.h"
#include "function_call.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// HELPER: Load clojure.string namespace
// ============================================================================

static void load_clojure_string_namespace(void) {
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// ============================================================================
// TESTS FOR QUALIFIED SYMBOL PARSING
// ============================================================================

// Test: Verify that qualified symbols are parsed correctly with ns field set
TEST(test_qualified_symbol_parsing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Parse a qualified symbol
    CljObject *parsed = eval_string("'clojure.core/reverse", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);

    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NOT_NULL(sym->ns_name); // ns field should be set
    TEST_ASSERT_NOT_NULL(sym->cname); // name field should be set
    TEST_ASSERT_EQUAL_STRING("reverse", sym->cname);
    TEST_ASSERT_EQUAL_STRING("clojure.core", sym->ns_name->cname);
}

// Test: Verify that unqualified symbols have ns field NULL
TEST(test_unqualified_symbol_parsing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Parse an unqualified symbol
    CljObject *parsed = eval_string("'reverse", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_TRUE(TAG(parsed) == CLJ_SYMBOL);

    CljSymbol *sym = as_symbol(parsed);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_NULL(sym->ns_name); // ns field should be NULL for unqualified symbols
    TEST_ASSERT_NOT_NULL(sym->cname);
    TEST_ASSERT_EQUAL_STRING("reverse", sym->cname);
}

// ============================================================================
// TESTS FOR clojure.core QUALIFIED SYMBOLS
// ============================================================================

// Test: Resolve clojure.core/reverse
TEST(test_resolve_clojure_core_reverse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test direct resolution via eval_symbol
    CljSymbol *reverse_sym = intern_symbol(SYM_CLOJURE_CORE, "reverse");
    TEST_ASSERT_NOT_NULL(reverse_sym);
    TEST_ASSERT_NOT_NULL(reverse_sym->ns_name);

    ID resolved = eval_symbol(reverse_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.core/reverse in expression
TEST(test_eval_clojure_core_reverse_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (clojure.core/reverse '(1 2 3)) => '(3 2 1)
    CljObject *result = eval_string("(clojure.core/reverse '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_LIST);
}

// Test: Resolve clojure.core/reverse in let binding
TEST(test_resolve_clojure_core_reverse_in_let) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (let [rev clojure.core/reverse] (rev '(1 2 3)))
    CljObject *result = eval_string("(let [rev clojure.core/reverse] (rev '(1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_LIST);
}

// Test: Resolve clojure.core/reverse in function
TEST(test_resolve_clojure_core_reverse_in_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (fn [x] (clojure.core/reverse x))
    CljObject *fn_result = eval_string("(fn [x] (clojure.core/reverse x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_result);
    TEST_ASSERT_TRUE(TAG(fn_result) == CLJ_CLOSURE);

    // Call the function
    CljObject *call_result = eval_string("((fn [x] (clojure.core/reverse x)) '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(TAG(call_result) == CLJ_LIST);
}

// ============================================================================
// TESTS FOR clojure.string QUALIFIED SYMBOLS
// ============================================================================

// Test: Resolve clojure.string/blank?
TEST(test_resolve_clojure_string_blank) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test direct resolution via eval_symbol
    CljSymbol *blank_sym = intern_symbol(SYM_CLOJURE_STRING, "blank?");
    TEST_ASSERT_NOT_NULL(blank_sym);
    TEST_ASSERT_NOT_NULL(blank_sym->ns_name);

    ID resolved = eval_symbol(blank_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.string/blank? in expression
TEST(test_eval_clojure_string_blank_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: (clojure.string/blank? "") => true
    CljObject *result = eval_string("(clojure.string/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

// Test: Resolve clojure.string/join
TEST(test_resolve_clojure_string_join) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test direct resolution via eval_symbol
    CljSymbol *join_sym = intern_symbol(SYM_CLOJURE_STRING, "join");
    TEST_ASSERT_NOT_NULL(join_sym);
    TEST_ASSERT_NOT_NULL(join_sym->ns_name);

    ID resolved = eval_symbol(join_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.string/join in expression
TEST(test_eval_clojure_string_join_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: (clojure.string/join "," '("a" "b" "c")) => "a,b,c"
    CljObject *result = eval_string("(clojure.string/join \",\" '(\"a\" \"b\" \"c\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
}

// Test: Resolve clojure.string/reverse
TEST(test_resolve_clojure_string_reverse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test direct resolution via eval_symbol
    CljSymbol *reverse_sym = intern_symbol(SYM_CLOJURE_STRING, "reverse");
    TEST_ASSERT_NOT_NULL(reverse_sym);
    TEST_ASSERT_NOT_NULL(reverse_sym->ns_name);

    ID resolved = eval_symbol(reverse_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_TRUE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE);
}

// Test: Evaluate clojure.string/reverse in expression
TEST(test_eval_clojure_string_reverse_expression) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: (clojure.string/reverse "hello") => "olleh"
    CljObject *result = eval_string("(clojure.string/reverse \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
}

// ============================================================================
// TESTS FOR QUALIFIED SYMBOLS IN LOCAL FUNCTIONS
// ============================================================================

// Test: Resolve clojure.core/reverse in local function (fn)
TEST(test_resolve_clojure_core_reverse_in_local_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: Define a function that uses clojure.core/reverse
    CljObject *fn_result = eval_string("(fn [x] (clojure.core/reverse x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_result);
    TEST_ASSERT_TRUE(TAG(fn_result) == CLJ_CLOSURE);

    // Call the function
    CljObject *call_result = eval_string("((fn [x] (clojure.core/reverse x)) '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(TAG(call_result) == CLJ_LIST);
}

// Test: Resolve clojure.core/reverse in let binding with function
TEST(test_resolve_clojure_core_reverse_in_let_with_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.core
    load_clojure_core(g_test_eval_state);

    // Test: (let [step (fn [x] (clojure.core/reverse x))] (step '(1 2 3)))
    CljObject *result = eval_string("(let [step (fn [x] (clojure.core/reverse x))] (step '(1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_LIST);
}

// Test: Resolve clojure.string/join in local function
TEST(test_resolve_clojure_string_join_in_local_fn) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Test: Define a function that uses clojure.string/join
    CljObject *fn_result = eval_string("(fn [coll] (clojure.string/join \",\" coll))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn_result);
    TEST_ASSERT_TRUE(TAG(fn_result) == CLJ_CLOSURE);

    // Call the function
    CljObject *call_result = eval_string("((fn [coll] (clojure.string/join \",\" coll)) '(\"a\" \"b\" \"c\"))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(call_result);
    TEST_ASSERT_TRUE(TAG(call_result) == CLJ_STRING);
}

// ============================================================================
// TESTS FOR NAMESPACE ALIAS RESOLUTION
// ============================================================================

// Test: Resolve qualified symbol with namespace alias
TEST(test_resolve_qualified_symbol_with_alias) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Load clojure.string namespace
    load_clojure_string_namespace();

    // Create a namespace with alias
    eval_string("(ns test-ns (:require [clojure.string :as str]))", g_test_eval_state);

    // Test: str/blank? should resolve to clojure.string/blank?
    CljObject *result = eval_string("(str/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

// ============================================================================
// TESTS FOR ERROR CASES
// ============================================================================

// Test: Non-existent qualified symbol should throw exception
TEST(test_nonexistent_qualified_symbol_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: clojure.core/nonexistent should throw exception
    TRY {
        (void)eval_string("clojure.core/nonexistent", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown exception for nonexistent symbol");
    } CATCH(ex) {
        // Expected exception
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

// Test: Non-existent namespace should throw exception
TEST(test_nonexistent_namespace_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Test: nonexistent.ns/symbol should throw exception
    TRY {
        (void)eval_string("nonexistent.ns/symbol", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown exception for nonexistent namespace");
    } CATCH(ex) {
        // Expected exception
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

