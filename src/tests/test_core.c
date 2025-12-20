/*
 * Unity Tests for clojure.core functions in Tiny-CLJ
 * 
 * Consolidated tests for core Clojure functions from clojure.core namespace
 */

#include "tests_common.h"
#include "namespace.h"
#include "symbol.h"
#include "map.h"
#include "object.h"
#include "kv_macros.h"
#include "value.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// COLLECTION FUNCTIONS
// ============================================================================

TEST(test_core_count) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (count [1 2 3]) => 3
    CljObject *result1 = eval_string("(count [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result1));
    
    // Test: (count '()) => 0
    CljObject *result2 = eval_string("(count '())", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result2));
    
    // Test: (count nil) => 0
    CljObject *result3 = eval_string("(count nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(is_fixnum(result3));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result3));
}

TEST(test_core_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (first [1 2 3]) => 1
    CljObject *result1 = eval_string("(first [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result1));
    
    // Test: (first nil) => nil
    CljObject *result2 = eval_string("(first nil)", g_test_eval_state);
    TEST_ASSERT_NULL(result2);
}

TEST(test_core_rest) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (first (rest [1 2 3])) => 2
    CljObject *result1 = eval_string("(first (rest [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
    
    // Test: (rest []) => ()
    CljObject *result2 = eval_string("(rest [])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
}

TEST(test_core_conj) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (conj [1 2] 3) => [1 2 3]
    CljObject *result1 = eval_string("(count (conj [1 2] 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result1));
    
    // Test: vector conj puts element at end
    CljObject *result2 = eval_string("(nth (conj [1 2] 3) 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result2));
}

TEST(test_core_cons) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (cons 1 [2 3]) => (1 2 3)
    CljObject *result1 = eval_string("(first (cons 1 [2 3]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result1));
}

TEST(test_core_nth) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (nth [1 2 3] 1) => 2
    CljObject *result1 = eval_string("(nth [1 2 3] 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
    
    // Test: (nth [1 2 3] 0) => 1
    CljObject *result2 = eval_string("(nth [1 2 3] 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result2));
}

// ============================================================================
// ARITHMETIC FUNCTIONS
// ============================================================================

TEST(test_core_inc) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (inc 1) => 2
    CljObject *result1 = eval_string("(inc 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
    
    // Test: (inc 0) => 1
    CljObject *result2 = eval_string("(inc 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result2));
}

TEST(test_core_dec) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (dec 2) => 1
    CljObject *result1 = eval_string("(dec 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result1));
    
    // Test: (dec 0) => -1
    CljObject *result2 = eval_string("(dec 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(-1, as_fixnum(result2));
}

TEST(test_core_plus) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (+ 1 2) => 3
    CljObject *result1 = eval_string("(+ 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result1));
    
    // Test: (+ 1 2 3 4) => 10
    CljObject *result2 = eval_string("(+ 1 2 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result2));
}

TEST(test_core_minus) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (- 5 3) => 2
    CljObject *result1 = eval_string("(- 5 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
    
    // Test: (- 10) => -10
    CljObject *result2 = eval_string("(- 10)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(-10, as_fixnum(result2));
}

TEST(test_core_multiply) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (* 2 3) => 6
    CljObject *result1 = eval_string("(* 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result1));
    
    // Test: (* 2 3 4) => 24
    CljObject *result2 = eval_string("(* 2 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum(result2));
}

// ============================================================================
// COMPARISON FUNCTIONS
// ============================================================================

TEST(test_core_equals) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (= 1 1) => true
    CljObject *result1 = eval_string("(= 1 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (= 1 2) => false
    CljObject *result2 = eval_string("(= 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
    
    // Test: (= [1 2] [1 2]) => true
    CljObject *result3 = eval_string("(= [1 2] [1 2])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_true);
}

TEST(test_core_not_equals) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (not= 1 2) => true
    CljObject *result1 = eval_string("(not= 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (not= 1 1) => false
    CljObject *result2 = eval_string("(not= 1 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_core_less_than) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (< 1 2) => true
    CljObject *result1 = eval_string("(< 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (< 2 1) => false
    CljObject *result2 = eval_string("(< 2 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_core_greater_than) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (> 2 1) => true
    CljObject *result1 = eval_string("(> 2 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (> 1 2) => false
    CljObject *result2 = eval_string("(> 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

// ============================================================================
// PREDICATE FUNCTIONS
// ============================================================================

TEST(test_core_nil_predicate) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (nil? nil) => true
    CljObject *result1 = eval_string("(nil? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (nil? 1) => false
    CljObject *result2 = eval_string("(nil? 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_core_empty_predicate) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (empty? []) => true
    CljObject *result1 = eval_string("(empty? [])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (empty? [1]) => false
    CljObject *result2 = eval_string("(empty? [1])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_core_vector_predicate) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (vector? []) => true
    CljObject *result1 = eval_string("(vector? [])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (vector? '()) => false
    CljObject *result2 = eval_string("(vector? '())", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_core_map_predicate) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (map? {}) => true
    CljObject *result1 = eval_string("(map? {})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (map? {:a 1 :b 2}) => true
    CljObject *result2 = eval_string("(map? {:a 1 :b 2})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
    
    // Test: (map? []) => false
    CljObject *result3 = eval_string("(map? [])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_false);
    
    // Test: (map? '()) => false
    CljObject *result4 = eval_string("(map? '())", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(result4 == clj_false);
    
    // Test: (map? nil) => false
    CljObject *result5 = eval_string("(map? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_TRUE(result5 == clj_false);
    
    // Test: (map? 42) => false
    CljObject *result6 = eval_string("(map? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result6);
    TEST_ASSERT_TRUE(result6 == clj_false);
    
    // Test: (map? "string") => false
    CljObject *result7 = eval_string("(map? \"string\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result7);
    TEST_ASSERT_TRUE(result7 == clj_false);
}

// ============================================================================
// MAP FUNCTIONS
// ============================================================================

TEST(test_core_get) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (get {:a 1} :a) => 1
    CljObject *result1 = eval_string("(get {:a 1} :a)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result1));
    
    // Test: (get {:a 1} :b) => nil
    CljObject *result2 = eval_string("(get {:a 1} :b)", g_test_eval_state);
    TEST_ASSERT_NULL(result2);
    
    // Test: (get {:a 1} :b 99) => 99 (default value)
    CljObject *result3 = eval_string("(get {:a 1} :b 99)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(is_fixnum(result3));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(result3));
}

TEST(test_core_assoc) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (get (assoc {} :a 1) :a) => 1
    CljObject *result1 = eval_string("(get (assoc {} :a 1) :a)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result1));
}

TEST(test_core_dissoc) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (get (dissoc {:a 1 :b 2} :a) :a) => nil
    CljObject *result1 = eval_string("(get (dissoc {:a 1 :b 2} :a) :a)", g_test_eval_state);
    TEST_ASSERT_NULL(result1);
    
    // Test: (get (dissoc {:a 1 :b 2} :a) :b) => 2
    CljObject *result2 = eval_string("(get (dissoc {:a 1 :b 2} :a) :b)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result2));
}

TEST(test_core_keys) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (count (keys {:a 1 :b 2})) => 2
    CljObject *result1 = eval_string("(count (keys {:a 1 :b 2}))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

TEST(test_core_vals) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (count (vals {:a 1 :b 2})) => 2
    CljObject *result1 = eval_string("(count (vals {:a 1 :b 2}))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

// ============================================================================
// BOOLEAN FUNCTIONS
// ============================================================================

TEST(test_core_not) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (not true) => false
    CljObject *result1 = eval_string("(not true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_false);
    
    // Test: (not false) => true
    CljObject *result2 = eval_string("(not false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
    
    // Test: (not nil) => true
    CljObject *result3 = eval_string("(not nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_true);
}

// ============================================================================
// SEQUENCE FUNCTIONS
// ============================================================================

TEST(test_core_map) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (first (map inc [1 2 3])) => 2
    CljObject *result1 = eval_string("(first (map inc [1 2 3]))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

TEST(test_core_filter) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (count (filter (fn [x] (> x 2)) [1 2 3 4])) => 2
    // Use let to bind once (avoids evaluation issues with lazy sequences)
    CljObject *result1 = eval_string("(let [f (filter (fn [x] (> x 2)) [1 2 3 4])] (count f))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

TEST(test_core_reduce) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (reduce + [1 2 3 4]) => 10
    CljObject *result1 = eval_string("(reduce + [1 2 3 4])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result1));
    
    // Test: (reduce + 100 [1 2 3]) => 106
    CljObject *result2 = eval_string("(reduce + 100 [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(106, as_fixnum(result2));
}

TEST(test_core_range) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (count (range 5)) => 5
    CljObject *result1 = eval_string("(count (range 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum(result1));
    
    // Test: (first (range 5)) => 0
    CljObject *result2 = eval_string("(first (range 5))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result2));
}

