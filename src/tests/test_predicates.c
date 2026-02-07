/*
 * Test Predicates for Tiny-CLJ
 * 
 * Tests for type predicate functions: number?, integer?, float?, string?,
 * keyword?, symbol?, fn?, char?, some?, true?, false?, boolean?, list?,
 * set?, coll?, seq?, seqable?, ifn?
 */

#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 300
#include "tests_common.h"

// ============================================================================
// number? - Returns true if x is a Number
// ============================================================================

TEST_SHARED(test_predicate_number_true_integer) {
    CljObject *result = eval_string("(number? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_number_true_float) {
    CljObject *result = eval_string("(number? 3.14)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_number_false_string) {
    CljObject *result = eval_string("(number? \"42\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// integer? - Returns true if n is an integer
// ============================================================================

TEST_SHARED(test_predicate_integer_true) {
    CljObject *result = eval_string("(integer? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_integer_false_float) {
    CljObject *result = eval_string("(integer? 3.14)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// float? - Returns true if n is a floating point number
// ============================================================================

TEST_SHARED(test_predicate_float_true) {
    CljObject *result = eval_string("(float? 3.14)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_float_false_integer) {
    CljObject *result = eval_string("(float? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// string? - Returns true if x is a String
// ============================================================================

TEST_SHARED(test_predicate_string_true) {
    CljObject *result = eval_string("(string? \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_string_false_number) {
    CljObject *result = eval_string("(string? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// keyword? - Returns true if x is a Keyword
// ============================================================================

TEST_SHARED(test_predicate_keyword_true) {
    CljObject *result = eval_string("(keyword? :foo)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_keyword_false_symbol) {
    CljObject *result = eval_string("(keyword? 'foo)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// symbol? - Returns true if x is a Symbol (not keyword)
// ============================================================================

TEST_SHARED(test_predicate_symbol_true) {
    CljObject *result = eval_string("(symbol? 'foo)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_symbol_false_keyword) {
    CljObject *result = eval_string("(symbol? :foo)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// fn? - Returns true if x implements IFn (function or closure)
// ============================================================================

TEST_SHARED(test_predicate_fn_true_builtin) {
    CljObject *result = eval_string("(fn? inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_fn_true_lambda) {
    CljObject *result = eval_string("(fn? (fn [x] x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_fn_false_number) {
    CljObject *result = eval_string("(fn? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// char? - Returns true if x is a Character
// ============================================================================

TEST_SHARED(test_predicate_char_true) {
    // Use \newline as it's a named character that's supported
    CljObject *result = eval_string("(char? \\newline)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_char_false_string) {
    CljObject *result = eval_string("(char? \"a\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// some? - Returns true if x is not nil
// ============================================================================

TEST_SHARED(test_predicate_some_true) {
    CljObject *result = eval_string("(some? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_some_false) {
    CljObject *result = eval_string("(some? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// true? - Returns true if x is the value true
// ============================================================================

TEST_SHARED(test_predicate_true_true) {
    CljObject *result = eval_string("(true? true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_true_false) {
    CljObject *result = eval_string("(true? false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

TEST_SHARED(test_predicate_true_false_number) {
    // 1 is truthy but not true
    CljObject *result = eval_string("(true? 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// false? - Returns true if x is the value false
// ============================================================================

TEST_SHARED(test_predicate_false_true) {
    CljObject *result = eval_string("(false? false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_false_false) {
    CljObject *result = eval_string("(false? true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

TEST_SHARED(test_predicate_false_false_nil) {
    // nil is falsy but not false
    CljObject *result = eval_string("(false? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// boolean? - Returns true if x is a Boolean (true or false)
// ============================================================================

TEST_SHARED(test_predicate_boolean_true_true) {
    CljObject *result = eval_string("(boolean? true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_boolean_true_false) {
    CljObject *result = eval_string("(boolean? false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_boolean_false_nil) {
    CljObject *result = eval_string("(boolean? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// list? - Returns true if x is a list
// ============================================================================

TEST_SHARED(test_predicate_list_true) {
    CljObject *result = eval_string("(list? '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_list_false_vector) {
    CljObject *result = eval_string("(list? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// Quoted list type - Verify that quoted lists have type "list"
// ============================================================================

TEST_SHARED(test_quoted_list_type_is_list) {
    // This test verifies the canonicalize fix: quoted lists should be CljList, not ASTNode
    // Use list? predicate to verify - it compares (type x) with 'clojure.lang/PersistentList
    CljObject *result = eval_string("(list? '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_quoted_nested_list_type) {
    // Nested quoted lists should also be CljList
    CljObject *result = eval_string("(list? (first '((1 2) 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_quoted_symbol_type) {
    // Symbols in quoted lists should still be symbols (not strings)
    CljObject *result = eval_string("(symbol? (first '(foo bar)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

// ============================================================================
// set? - Returns true if x is a set
// ============================================================================

TEST_SHARED(test_predicate_set_true_literal) {
    CljObject *result = eval_string("(set? #{1 2 3})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_set_true_hash_set) {
    CljObject *result = eval_string("(set? (hash-set 1 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_set_false_map) {
    CljObject *result = eval_string("(set? {:a 1})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

TEST_SHARED(test_predicate_set_false_vector) {
    CljObject *result = eval_string("(set? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// coll? - Returns true if x is a collection (list, vector, or map)
// ============================================================================

TEST_SHARED(test_predicate_coll_true_list) {
    CljObject *result = eval_string("(coll? '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_coll_true_vector) {
    CljObject *result = eval_string("(coll? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_coll_true_map) {
    CljObject *result = eval_string("(coll? {:a 1})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_coll_true_set) {
    CljObject *result = eval_string("(coll? #{1 2 3})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_coll_false_number) {
    CljObject *result = eval_string("(coll? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// seq? - Returns true if x is a sequence
// ============================================================================

TEST_SHARED(test_predicate_seq_true_list) {
    CljObject *result = eval_string("(seq? '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_seq_false_vector) {
    CljObject *result = eval_string("(seq? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// seqable? - Returns true if (seq x) will succeed
// ============================================================================

TEST_SHARED(test_predicate_seqable_true_list) {
    CljObject *result = eval_string("(seqable? '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_seqable_true_string) {
    CljObject *result = eval_string("(seqable? \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_seqable_true_set) {
    CljObject *result = eval_string("(seqable? #{1 2})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_seqable_true_nil) {
    CljObject *result = eval_string("(seqable? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_seqable_false_number) {
    CljObject *result = eval_string("(seqable? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

// ============================================================================
// Set Operations (hash-set, conj, disj, contains?)
// ============================================================================

TEST_SHARED(test_contains_set_true) {
    CljObject *result = eval_string("(contains? #{1 2} 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_contains_set_false) {
    CljObject *result = eval_string("(contains? #{1 2} 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}

TEST_SHARED(test_conj_disj_set) {
    CljObject *result1 = eval_string("(contains? (conj #{1} 2) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);

    CljObject *result2 = eval_string("(contains? (disj #{1 2} 2) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

// ============================================================================
// ifn? - Returns true if x implements IFn (fn, keyword, map, vector)
// ============================================================================

TEST_SHARED(test_predicate_ifn_true_fn) {
    CljObject *result = eval_string("(ifn? inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_ifn_true_keyword) {
    CljObject *result = eval_string("(ifn? :foo)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_ifn_true_map) {
    CljObject *result = eval_string("(ifn? {:a 1})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_ifn_true_vector) {
    CljObject *result = eval_string("(ifn? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST_SHARED(test_predicate_ifn_false_number) {
    CljObject *result = eval_string("(ifn? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_false);
}
