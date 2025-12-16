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

// Test: Namespace aliases
// DISABLED: require :as alias is not fully implemented yet
// TEST(test_symbol_table_namespace_aliases) {
//     CljObject *result = eval_string(
//         "(do "
//         "  (require '[clojure.string :as str]) "
//         "  (str/upper-case \"test\"))",
//         g_test_eval_state
//     );
//     assert_string(result, "TEST");
// }

