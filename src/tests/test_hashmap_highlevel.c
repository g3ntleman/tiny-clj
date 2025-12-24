/*
 * High-Level HashMap Tests using Clojure Source Code
 * 
 * NOTE: These tests can only INDIRECTLY verify HashMap functionality via symbol table.
 * They cannot prove that HashMap is used (could be CljMap) or that specific HashMap
 * features (Linear Probing, Rehashing, COW, Tombstones) work correctly.
 * 
 * These tests are only useful AFTER HashMap is integrated into symbol table to verify
 * that the symbol table still works correctly. The real HashMap tests are in
 * subjective-c/tests/test_hashmap.c which test the HashMap API directly.
 * 
 * Once HashMap is integrated, these tests can serve as regression tests to ensure
 * that symbol table operations still work correctly with HashMap backend.
 */

#include "tests_common.h"
#include "../symbol.h"
#include "../namespace.h"

// ============================================================================
// INDIRECT REGRESSION TESTS (Symbol Table via Clojure)
// ============================================================================
// These tests verify that symbol table operations work correctly.
// They do NOT prove HashMap functionality - they only verify that the
// symbol table (regardless of implementation) works as expected.

// Test: Many symbol definitions and lookups
// This would fail if symbol table couldn't handle many entries,
// but doesn't prove HashMap is used or works correctly.
TEST(test_symbol_table_many_symbols) {
    // Create many symbols - would fail if symbol table had issues with many entries
    CljObject *result = eval_string(
        "(do "
        "  (def a 1) (def b 2) (def c 3) (def d 4) (def e 5) "
        "  (def f 6) (def g 7) (def h 8) (def i 9) (def j 10) "
        "  (+ a b c d e f g h i j))",
        g_test_eval_state
    );
    assert_fixnum(result, 55);
}

// Test: Symbol redefinition
// This would fail if symbol table couldn't update entries,
// but doesn't prove HashMap COW or update mechanisms.
TEST(test_symbol_table_redefinition) {
    // Define and redefine in a single expression - verifies def can update values
    CljObject *result = eval_string(
        "(do "
        "  (def test-var 42) "
        "  (def test-var 100) "
        "  test-var)",
        g_test_eval_state
    );
    assert_fixnum(result, 100);
}

// Test: Qualified symbol resolution
// This would fail if symbol table couldn't handle qualified lookups,
// but doesn't prove HashMap string key lookup.
TEST(test_symbol_table_qualified_symbols) {
    // Use qualified symbols - would fail if symbol table couldn't resolve them
    CljObject *result = eval_string(
        "(do "
        "  (require 'clojure.string) "
        "  (clojure.string/upper-case \"hello\"))",
        g_test_eval_state
    );
    assert_string(result, "HELLO");
}

// Helper: Load clojure.string namespace (like other alias tests)
static void load_clojure_string_namespace(void) {
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// Test: Namespace aliases
// This test verifies that aliases are set correctly in the symbol table.
// Note: Function calls via aliases (str/upper-case) may not work in all contexts,
// but the alias itself should be stored correctly.
TEST(test_symbol_table_namespace_aliases) {
    // Load namespace first (like other alias tests)
    load_clojure_string_namespace();
    
    // Set alias
    CljObject *req_result = eval_string("(require '[clojure.string :as str])", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify alias was stored in symbol table
    CljSymbol *str_alias = intern_symbol_global("str");
    TEST_ASSERT_NOT_NULL(str_alias);
    
    CljObject *ns_name = ns_get_alias(g_test_eval_state->current_ns, (CljObject *)str_alias);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_name, "Alias 'str' should be set in symbol table");
    
    if (ns_name && TAG(ns_name) == CLJ_SYMBOL) {
        CljSymbol *ns_sym = as_symbol(ns_name);
        TEST_ASSERT_NOT_NULL(ns_sym);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("clojure.string", ns_sym->cname,
            "Alias should resolve to clojure.string");
    }
}

