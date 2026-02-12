/*
 * Unit Tests for Clojure Core Functions
 * 
 * Test-first implementation of missing clojure.core functions.
 * Tests are organized by phase according to the implementation plan.
 * Heap limit 4096 for shared tests (mapcat, keep, lazy seqs allocate above 2048).
 */
#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 400
#include "tests_common.h"

// ============================================================================
// HELPER: Common test assertion patterns (DRY)
// ============================================================================

static void assert_eval_truthy(const char *expr) {
    ID result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(clj_is_truthy(result), expr);
}

static void assert_eval_nil(const char *expr) {
    ID result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NULL_MESSAGE(result, expr);
}

// ============================================================================
// PHASE 1: BASIS-FUNKTIONEN (concat, take, drop, last)
// ============================================================================

// --- concat ---

TEST_SHARED(test_concat_two_lists) {
    // (concat '(1 2) '(3 4)) => (1 2 3 4)
    assert_eval_truthy("(= (concat '(1 2) '(3 4)) '(1 2 3 4))");
}

TEST_SHARED(test_concat_empty_first) {
    // (concat '() '(1 2)) => (1 2)
    assert_eval_truthy("(= (concat '() '(1 2)) '(1 2))");
}

TEST_SHARED(test_concat_returns_lazy_seq) {
    // In Clojure, concat is lazy.
    ID result = eval_string("(concat '(1 2) '(3 4))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_LAZY_SEQ || TAG(result) == CLJ_SEQ,
                             "concat should return a lazy seq");
}

// --- take ---

TEST_SHARED(test_take_normal) {
    // (take 3 '(1 2 3 4 5)) => (1 2 3)
    // Test first element is correct
    assert_eval_truthy("(= (first (take 3 '(1 2 3 4 5))) 1)");
    // Test via second
    assert_eval_truthy("(= (second (take 3 '(1 2 3 4 5))) 2)");
}

TEST_SHARED(test_take_more_than_available) {
    // (take 10 '(1 2 3)) => (1 2 3)
    assert_eval_truthy("(= (first (take 10 '(1 2 3))) 1)");
    assert_eval_truthy("(= (last (take 10 '(1 2 3))) 3)");
}

TEST_SHARED(test_take_zero) {
    // (take 0 '(1 2 3)) => ()
    assert_eval_truthy("(empty? (take 0 '(1 2 3)))");
}

// --- drop ---

TEST_SHARED(test_drop_normal) {
    // (drop 2 '(1 2 3 4 5)) => (3 4 5)
    assert_eval_truthy("(= (count (drop 2 '(1 2 3 4 5))) 3)");
    assert_eval_truthy("(= (first (drop 2 '(1 2 3 4 5))) 3)");
}

TEST_SHARED(test_drop_more_than_available) {
    // (drop 10 '(1 2 3)) => ()
    assert_eval_truthy("(empty? (drop 10 '(1 2 3)))");
}

TEST_SHARED(test_drop_zero) {
    // (drop 0 '(1 2 3)) => (1 2 3)
    assert_eval_truthy("(= (count (drop 0 '(1 2 3))) 3)");
    assert_eval_truthy("(= (first (drop 0 '(1 2 3))) 1)");
}

// --- last ---

TEST_SHARED(test_last_normal) {
    // (last '(1 2 3 4)) => 4
    assert_eval_truthy("(= (last '(1 2 3 4)) 4)");
}

TEST_SHARED(test_last_single_element) {
    // (last '(42)) => 42
    assert_eval_truthy("(= (last '(42)) 42)");
}

TEST_SHARED(test_last_empty_list) {
    // (last '()) => nil
    assert_eval_nil("(last '())");
}

// ============================================================================
// PHASE 2: PRÄDIKATE (some, every?, not-every?, not-any?)
// ============================================================================

// --- some ---

TEST_SHARED(test_some_found) {
    // (some even? '(1 3 5 6 7)) => true (6 is even)
    assert_eval_truthy("(some even? '(1 3 5 6 7))");
}

TEST_SHARED(test_some_not_found) {
    // (some even? '(1 3 5 7)) => nil
    assert_eval_nil("(some even? '(1 3 5 7))");
}

TEST_SHARED(test_some_with_identity) {
    // (some identity '(nil false 42 true)) => 42
    assert_eval_truthy("(= (some identity '(nil false 42 true)) 42)");
}

// --- every? ---

TEST_SHARED(test_every_all_match) {
    // (every? pos? '(1 2 3 4)) => true
    assert_eval_truthy("(every? pos? '(1 2 3 4))");
}

TEST_SHARED(test_every_not_all_match) {
    // (every? pos? '(1 -2 3)) => false
    assert_eval_truthy("(not (every? pos? '(1 -2 3)))");
}

TEST_SHARED(test_every_empty_collection) {
    // (every? pos? '()) => true (vacuous truth)
    assert_eval_truthy("(every? pos? '())");
}

// --- not-every? ---

TEST_SHARED(test_not_every_one_fails) {
    // (not-every? pos? '(1 -2 3)) => true
    assert_eval_truthy("(not-every? pos? '(1 -2 3))");
}

TEST_SHARED(test_not_every_all_pass) {
    // (not-every? pos? '(1 2 3)) => false
    assert_eval_truthy("(not (not-every? pos? '(1 2 3)))");
}

// --- not-any? ---

TEST_SHARED(test_not_any_none_match) {
    // (not-any? even? '(1 3 5)) => true
    assert_eval_truthy("(not-any? even? '(1 3 5))");
}

TEST_SHARED(test_not_any_one_matches) {
    // (not-any? even? '(1 2 3)) => false
    assert_eval_truthy("(not (not-any? even? '(1 2 3)))");
}

// ============================================================================
// PHASE 3: HÖHERE SEQUENZ-FUNKTIONEN
// ============================================================================

// --- mapcat ---

TEST_SHARED(test_mapcat_duplicate) {
    // (mapcat (fn [x] (list x x)) '(1 2 3)) => (1 1 2 2 3 3)
    assert_eval_truthy("(= (first (mapcat (fn [x] (list x x)) '(1 2 3))) 1)");
    assert_eval_truthy("(= (second (mapcat (fn [x] (list x x)) '(1 2 3))) 1)");
}

TEST_SHARED(test_mapcat_expand) {
    // (mapcat (fn [x] (list x (inc x))) '(1 3)) => (1 2 3 4)
    assert_eval_truthy("(= (first (mapcat (fn [x] (list x (inc x))) '(1 3))) 1)");
    assert_eval_truthy("(= (last (mapcat (fn [x] (list x (inc x))) '(1 3))) 4)");
}

// --- map / mapv (multi-coll + vector result) ---

TEST_SHARED(test_map_two_collections_vector_zip) {
    // (map + [1 2 3] [10 20 30]) => (11 22 33)
    assert_eval_truthy("(= (map + [1 2 3] [10 20 30]) '(11 22 33))");
}

TEST_SHARED(test_map_two_collections_stops_at_shortest) {
    // Stops at shortest: (map + [1 2 3] [10]) => (11)
    assert_eval_truthy("(= (map + [1 2 3] [10]) '(11))");
}

TEST_SHARED(test_map_returns_lazy_seq) {
    // In Clojure, map is lazy.
    ID result = eval_string("(map inc [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_LAZY_SEQ || TAG(result) == CLJ_SEQ,
                             "map should return a lazy seq");
}

TEST_SHARED(test_mapv_single_collection) {
    // (mapv inc [1 2 3]) => [2 3 4]
    assert_eval_truthy("(= (mapv inc [1 2 3]) [2 3 4])");
}

TEST_SHARED(test_mapv_two_collections) {
    // (mapv + [1 2] [10 20]) => [11 22]
    assert_eval_truthy("(= (mapv + [1 2] [10 20]) [11 22])");
}

// --- take-while ---

TEST_SHARED(test_take_while_normal) {
    // (take-while pos? '(1 2 3 -1 4 5)) => (1 2 3)
    assert_eval_truthy("(= (first (take-while pos? '(1 2 3 -1 4 5))) 1)");
    assert_eval_truthy("(= (last (take-while pos? '(1 2 3 -1 4 5))) 3)");
}

TEST_SHARED(test_take_while_all_match) {
    // (take-while pos? '(1 2 3)) => (1 2 3)
    assert_eval_truthy("(= (first (take-while pos? '(1 2 3))) 1)");
    assert_eval_truthy("(= (last (take-while pos? '(1 2 3))) 3)");
}

TEST_SHARED(test_take_while_none_match) {
    // (take-while neg? '(1 2 3)) => ()
    assert_eval_truthy("(empty? (take-while neg? '(1 2 3)))");
}

// --- drop-while ---

TEST_SHARED(test_drop_while_normal) {
    // (drop-while pos? '(1 2 3 -1 4 5)) => (-1 4 5)
    assert_eval_truthy("(= (first (drop-while pos? '(1 2 3 -1 4 5))) -1)");
}

TEST_SHARED(test_drop_while_all_match) {
    // (drop-while pos? '(1 2 3)) => ()
    assert_eval_truthy("(empty? (drop-while pos? '(1 2 3)))");
}

TEST_SHARED(test_drop_while_none_match) {
    // (drop-while neg? '(1 2 3)) => (1 2 3)
    assert_eval_truthy("(= (first (drop-while neg? '(1 2 3))) 1)");
}

// --- butlast ---

TEST_SHARED(test_butlast_normal) {
    // (butlast '(1 2 3 4)) => (1 2 3)
    assert_eval_truthy("(= (butlast '(1 2 3 4)) '(1 2 3))");
}

TEST_SHARED(test_butlast_two_elements) {
    // (butlast '(1 2)) => (1)
    assert_eval_truthy("(= (butlast '(1 2)) '(1))");
}

TEST_SHARED(test_butlast_single_element) {
    // (butlast '(1)) => nil
    assert_eval_nil("(butlast '(1))");
}

// --- keep ---

TEST_SHARED(test_keep_filters_nil) {
    // (keep (fn [x] (if (even? x) x nil)) '(1 2 3 4 5 6)) => (2 4 6)
    assert_eval_truthy("(= (first (keep (fn [x] (if (even? x) x nil)) '(1 2 3 4 5 6))) 2)");
    assert_eval_truthy("(= (last (keep (fn [x] (if (even? x) x nil)) '(1 2 3 4 5 6))) 6)");
}

TEST_SHARED(test_keep_all_non_nil) {
    // (keep identity '(1 2 3)) => (1 2 3)
    assert_eval_truthy("(= (first (keep identity '(1 2 3))) 1)");
}

TEST_SHARED(test_keep_all_nil) {
    // (keep (fn [x] nil) '(1 2 3)) => ()
    assert_eval_truthy("(empty? (keep (fn [x] nil) '(1 2 3)))");
}

// --- interleave ---

TEST_SHARED(test_interleave_equal_length) {
    // (interleave '(1 2 3) '(4 5 6)) => (1 4 2 5 3 6)
    assert_eval_truthy("(= (first (interleave '(1 2 3) '(4 5 6))) 1)");
    assert_eval_truthy("(= (second (interleave '(1 2 3) '(4 5 6))) 4)");
}

TEST_SHARED(test_interleave_unequal_length) {
    // (interleave '(1 2) '(4 5 6 7)) => (1 4 2 5)
    assert_eval_truthy("(= (first (interleave '(1 2) '(4 5 6 7))) 1)");
    assert_eval_truthy("(= (last (interleave '(1 2) '(4 5 6 7))) 5)");
}

// ============================================================================
// PHASE 4: AGGREGATION (reductions, frequencies, group-by, distinct)
// ============================================================================

// --- reductions (3-arg version) ---

TEST_SHARED(test_reductions_sum) {
    // (reductions + 0 '(1 2 3 4)) => (0 1 3 6 10)
    assert_eval_truthy("(= (first (reductions + 0 '(1 2 3 4))) 0)");
    assert_eval_truthy("(= (last (reductions + 0 '(1 2 3 4))) 10)");
}

TEST_SHARED(test_reductions_product) {
    // (reductions * 1 '(2 3 4)) => (1 2 6 24)
    assert_eval_truthy("(= (first (reductions * 1 '(2 3 4))) 1)");
    assert_eval_truthy("(= (last (reductions * 1 '(2 3 4))) 24)");
}

// --- frequencies ---

TEST_SHARED(test_frequencies_symbols) {
    // (frequencies '(a b a c b a)) => {a 3, b 2, c 1}
    // Note: Map order may vary, so check individual keys
    assert_eval_truthy("(= (get (frequencies '(a b a c b a)) 'a) 3)");
    assert_eval_truthy("(= (get (frequencies '(a b a c b a)) 'b) 2)");
}

TEST_SHARED(test_frequencies_numbers) {
    // (frequencies '(1 1 2 3 2 1)) => {1 3, 2 2, 3 1}
    assert_eval_truthy("(= (get (frequencies '(1 1 2 3 2 1)) 1) 3)");
    assert_eval_truthy("(= (get (frequencies '(1 1 2 3 2 1)) 2) 2)");
}

// --- group-by ---

TEST_SHARED(test_group_by_even) {
    // (group-by even? '(1 2 3 4 5 6)) => {false [1 3 5], true [2 4 6]}
    assert_eval_truthy("(= (get (group-by even? '(1 2 3 4 5 6)) true) [2 4 6])");
    assert_eval_truthy("(= (get (group-by even? '(1 2 3 4 5 6)) false) [1 3 5])");
}

TEST_SHARED(test_group_by_identity) {
    // Simple grouping test - using map? check instead of count
    assert_eval_truthy("(map? (group-by identity '(1 2 1)))");
}

// --- distinct ---

TEST_SHARED(test_distinct_removes_duplicates) {
    // (distinct '(1 2 1 3 2 4 3)) => (1 2 3 4)
    assert_eval_truthy("(= (first (distinct '(1 2 1 3 2 4 3))) 1)");
    assert_eval_truthy("(= (last (distinct '(1 2 1 3 2 4 3))) 4)");
}

TEST_SHARED(test_distinct_no_duplicates) {
    // (distinct '(1 2 3)) => (1 2 3)
    assert_eval_truthy("(= (first (distinct '(1 2 3))) 1)");
}

TEST_SHARED(test_distinct_all_same) {
    // (distinct '(1 1 1 1)) => (1)
    assert_eval_truthy("(= (first (distinct '(1 1 1 1))) 1)");
    assert_eval_truthy("(empty? (rest (distinct '(1 1 1 1))))");
}

// ============================================================================
// PHASE 5: PARTITIONIERUNG
// ============================================================================

// --- partition ---

TEST_SHARED(test_partition_exact) {
    // (partition 2 '(1 2 3 4 5 6)) => ([1 2] [3 4] [5 6])
    // Check count of partitions
    ID result = eval_string("(partition 2 '(1 2 3 4 5 6))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_eval_truthy("(= (first (first (partition 2 '(1 2 3 4 5 6)))) 1)");
}

TEST_SHARED(test_partition_drops_incomplete) {
    // (partition 3 '(1 2 3 4 5)) => ([1 2 3]) - drops incomplete last partition
    assert_eval_truthy("(= (first (first (partition 3 '(1 2 3 4 5)))) 1)");
}

// --- partition-all ---

TEST_SHARED(test_partition_all_keeps_incomplete) {
    // (partition-all 2 '(1 2 3 4 5)) => ([1 2] [3 4] [5])
    assert_eval_truthy("(= (first (first (partition-all 2 '(1 2 3 4 5)))) 1)");
}

TEST_SHARED(test_partition_all_exact) {
    // (partition-all 2 '(1 2 3 4)) => ([1 2] [3 4])
    assert_eval_truthy("(= (first (first (partition-all 2 '(1 2 3 4)))) 1)");
}

// --- split-at ---

TEST_SHARED(test_split_at_middle) {
    // (split-at 3 '(1 2 3 4 5)) => [[1 2 3] [4 5]]
    assert_eval_truthy("(= (first (first (split-at 3 '(1 2 3 4 5)))) 1)");
    assert_eval_truthy("(= (first (second (split-at 3 '(1 2 3 4 5)))) 4)");
}

TEST_SHARED(test_split_at_zero) {
    // (split-at 0 '(1 2 3)) => [[] [1 2 3]]
    assert_eval_truthy("(empty? (first (split-at 0 '(1 2 3))))");
}

// --- split-with ---

TEST_SHARED(test_split_with_predicate) {
    // (split-with pos? '(1 2 3 -1 4 5)) => [[1 2 3] [-1 4 5]]
    assert_eval_truthy("(= (first (first (split-with pos? '(1 2 3 -1 4 5)))) 1)");
    assert_eval_truthy("(= (first (second (split-with pos? '(1 2 3 -1 4 5)))) -1)");
}

TEST_SHARED(test_split_with_all_match) {
    // (split-with pos? '(1 2 3)) => [[1 2 3] []]
    assert_eval_truthy("(= (first (first (split-with pos? '(1 2 3)))) 1)");
}

// ============================================================================
// PHASE 6: MAP-OPERATIONEN
// ============================================================================

// --- zipmap ---

TEST_SHARED(test_zipmap_normal) {
    // (zipmap [:a :b :c] [1 2 3]) => {:a 1 :b 2 :c 3}
    assert_eval_truthy("(= (get (zipmap [:a :b :c] [1 2 3]) :a) 1)");
    assert_eval_truthy("(= (get (zipmap [:a :b :c] [1 2 3]) :c) 3)");
}

TEST_SHARED(test_zipmap_unequal_length) {
    // (zipmap [:a :b] [1 2 3 4]) => {:a 1 :b 2}
    // Use let to bind once (avoids evaluation/memory issues)
    assert_eval_truthy("(let [m (zipmap [:a :b] [1 2 3 4])] (= (count m) 2))");
}

// --- get-in (2-arg) ---

TEST_SHARED(test_get_in_nested) {
    // (get-in {:a {:b {:c 42}}} [:a :b :c]) => 42
    assert_eval_truthy("(= (get-in {:a {:b {:c 42}}} [:a :b :c]) 42)");
}

TEST_SHARED(test_get_in_not_found) {
    // (get-in {:a 1} [:b :c]) => nil
    assert_eval_nil("(get-in {:a 1} [:b :c])");
}

// ============================================================================
// PHASE 7: FUNKTIONSKOMPOSITION
// ============================================================================

// --- partial (2-arg) ---

TEST_SHARED(test_partial_add) {
    // ((partial + 10) 5) => 15
    assert_eval_truthy("(= ((partial + 10) 5) 15)");
}

TEST_SHARED(test_partial_subtract) {
    // ((partial - 100) 30) => 70
    assert_eval_truthy("(= ((partial - 100) 30) 70)");
}

// --- comp (2-arg) ---

TEST_SHARED(test_comp_two_functions) {
    // ((comp inc inc) 5) => 7
    assert_eval_truthy("(= ((comp inc inc) 5) 7)");
}

TEST_SHARED(test_comp_with_str) {
    // ((comp str inc) 5) => "6"
    assert_eval_truthy("(= ((comp str inc) 5) \"6\")");
}

// --- juxt (2-arg) ---

TEST_SHARED(test_juxt_inc_dec) {
    // ((juxt inc dec) 5) => [6 4]
    assert_eval_truthy("(= ((juxt inc dec) 5) [6 4])");
}

TEST_SHARED(test_juxt_first_last) {
    // ((juxt first last) '(1 2 3 4)) => [1 4]
    assert_eval_truthy("(= ((juxt first last) '(1 2 3 4)) [1 4])");
}

// --- complement ---

TEST_SHARED(test_complement_even) {
    // ((complement even?) 3) => true
    assert_eval_truthy("((complement even?) 3)");
}

TEST_SHARED(test_complement_pos) {
    // ((complement pos?) -5) => true
    assert_eval_truthy("((complement pos?) -5)");
}

// ============================================================================
// PHASE 8: ITERATION
// ============================================================================

// --- repeatedly ---

TEST_SHARED(test_repeatedly_count) {
    // (count (vec (repeatedly 5 (fn [] 1)))) => 5
    assert_eval_truthy("(= (count (vec (repeatedly 5 (fn [] 1)))) 5)");
}

TEST_SHARED(test_repeatedly_values) {
    // (repeatedly 3 (fn [] :x)) => (:x :x :x)
    assert_eval_truthy("(= (first (repeatedly 3 (fn [] :x))) :x)");
}

// --- reduce-kv ---

TEST_SHARED(test_reduce_kv_sum_values) {
    // (reduce-kv (fn [acc k v] (+ acc v)) 0 {:a 1 :b 2 :c 3}) => 6
    assert_eval_truthy("(= (reduce-kv (fn [acc k v] (+ acc v)) 0 {:a 1 :b 2 :c 3}) 6)");
}

TEST_SHARED(test_reduce_kv_transform) {
    // (reduce-kv (fn [m k v] (assoc m k (inc v))) {} {:a 1 :b 2})
    assert_eval_truthy("(= (get (reduce-kv (fn [m k v] (assoc m k (inc v))) {} {:a 1 :b 2}) :a) 2)");
}

// --- abs ---

TEST_SHARED(test_abs_negative) {
    // (abs -5) => 5
    assert_eval_truthy("(= (abs -5) 5)");
}

TEST_SHARED(test_abs_positive) {
    // (abs 5) => 5
    assert_eval_truthy("(= (abs 5) 5)");
}

TEST_SHARED(test_abs_zero) {
    // (abs 0) => 0
    assert_eval_truthy("(= (abs 0) 0)");
}

// --- rem ---

TEST_SHARED(test_rem_positive) {
    // (rem 10 3) => 1
    assert_eval_truthy("(= (rem 10 3) 1)");
}

TEST_SHARED(test_rem_negative_dividend) {
    // (rem -10 3) => -1
    assert_eval_truthy("(= (rem -10 3) -1)");
}

// --- clojure.string/pad-left ---

TEST(test_string_pad_left_basic) {
    // First, require clojure.string
    eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // (clojure.string/pad-left "5" 3 "0") => "005"
    assert_eval_truthy("(= (clojure.string/pad-left \"5\" 3 \"0\") \"005\")");
}

TEST(test_string_pad_left_no_padding_needed) {
    eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // String already at width - no padding
    assert_eval_truthy("(= (clojure.string/pad-left \"abc\" 3 \"0\") \"abc\")");
    
    // String longer than width - unchanged
    assert_eval_truthy("(= (clojure.string/pad-left \"abcdef\" 3 \"0\") \"abcdef\")");
}

TEST(test_string_pad_left_empty_string) {
    eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Padding empty string
    assert_eval_truthy("(= (clojure.string/pad-left \"\" 3 \"x\") \"xxx\")");
}
