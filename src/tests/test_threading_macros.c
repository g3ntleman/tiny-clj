/*
 * Tests for Threading Macros
 * 
 * Tests for: ->, ->>, as->, some->, some->>, cond->, cond->>
 * High-level tests using (source) and (macroexpand) for verification
 */

#include "tests_common.h"

// Forward declarations
int load_clojure_repl(EvalState *st);

// Helper to load clojure.repl namespace
static void load_repl_namespace(void) {
    load_clojure_repl(g_test_eval_state);
}

// ============================================================================
// TEST: -> (thread-first) Basic Functionality
// ============================================================================

TEST(test_threading_thread_first_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Basic threading: (-> 5 (+ 3) (* 2)) => 16
    CljObject *result = eval_string("(-> 5 (+ 3) (* 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(16, as_fixnum((CljValue)result));
}

TEST(test_threading_thread_first_single_form) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Single form: (-> 5 inc) => 6
    CljObject *result = eval_string("(-> 5 inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum((CljValue)result));
}

TEST(test_threading_thread_first_no_forms) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // No forms: (-> 5) => 5
    CljObject *result = eval_string("(-> 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum((CljValue)result));
}

TEST(test_threading_thread_first_map_ops) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Map operations: (-> {:a 1} (assoc :b 2) (get :b)) => 2
    CljObject *result = eval_string("(-> {:a 1} (assoc :b 2) (get :b))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)result));
}

TEST(test_threading_thread_first_non_seq_form) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Non-seq form: (-> 5 :keyword) should create (list :keyword 5)
    CljObject *result = eval_string("(-> 5 :keyword)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    // Should be a list with :keyword and 5
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

// ============================================================================
// TEST: ->> (thread-last) Basic Functionality
// ============================================================================

TEST(test_threading_thread_last_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Basic threading: (->> [1 2 3] (map inc) (filter even?)) => (2 4)
    CljObject *result = eval_string("(->> [1 2 3] (map inc) (filter even?))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL_INT(2, list_count(list));
}

TEST(test_threading_thread_last_single_form) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Single form: (->> [1 2 3] (map inc)) => (2 3 4)
    CljObject *result = eval_string("(->> [1 2 3] (map inc))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL_INT(3, list_count(list));
}

TEST(test_threading_thread_last_no_forms) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // No forms: (->> 5) => 5
    CljObject *result = eval_string("(->> 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum((CljValue)result));
}

// ============================================================================
// TEST: as-> Basic Functionality
// ============================================================================

TEST(test_threading_as_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Basic as->: (as-> 5 x (+ x 3) (* x 2)) => 16
    CljObject *result = eval_string("(as-> 5 x (+ x 3) (* x 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(16, as_fixnum((CljValue)result));
}

TEST(test_threading_as_no_forms) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // No forms: (as-> 5 x) => 5
    CljObject *result = eval_string("(as-> 5 x)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(5, as_fixnum((CljValue)result));
}

// ============================================================================
// TEST: some-> Basic Functionality
// ============================================================================

TEST(test_threading_some_first_stops_at_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Stops at nil: (some-> {:a 1} :b inc) => nil
    CljObject *result = eval_string("(some-> {:a 1} :b inc)", g_test_eval_state);
    TEST_ASSERT_NULL(result);  // nil
}

TEST(test_threading_some_first_continues_when_not_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Continues when not nil: (some-> {:a 1} :a inc) => 2
    CljObject *result = eval_string("(some-> {:a 1} :a inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)result));
}

TEST(test_threading_some_first_nil_expr) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Nil expr: (some-> nil inc) => nil
    CljObject *result = eval_string("(some-> nil inc)", g_test_eval_state);
    TEST_ASSERT_NULL(result);  // nil
}

// ============================================================================
// TEST: some->> Basic Functionality
// ============================================================================

TEST(test_threading_some_last_continues_when_not_nil) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Continues when not nil: (some->> [1 2 3] (map inc) (filter even?)) => (2 4)
    CljObject *result = eval_string("(some->> [1 2 3] (map inc) (filter even?))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

TEST(test_threading_some_last_nil_expr) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Nil expr: (some->> nil (map inc)) => nil
    CljObject *result = eval_string("(some->> nil (map inc))", g_test_eval_state);
    TEST_ASSERT_NULL(result);  // nil
}

// ============================================================================
// TEST: cond-> Basic Functionality
// ============================================================================

TEST(test_threading_cond_first_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Basic cond->: (cond-> 1 true inc false dec) => 2
    CljObject *result = eval_string("(cond-> 1 true inc false dec)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)result));
}

TEST(test_threading_cond_first_multiple_true) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Multiple true tests: (cond-> 1 true inc true (* 2)) => 4
    CljObject *result = eval_string("(cond-> 1 true inc true (* 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)result));
}

TEST(test_threading_cond_first_no_clauses) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // No clauses: (cond-> 1) => 1
    CljObject *result = eval_string("(cond-> 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)result));
}

// ============================================================================
// TEST: cond->> Basic Functionality
// ============================================================================

TEST(test_threading_cond_last_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Basic cond->>: (cond->> [1 2 3] true (map inc) false (filter even?)) => (2 3 4)
    CljObject *result = eval_string("(cond->> [1 2 3] true (map inc) false (filter even?))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

TEST(test_threading_cond_last_no_clauses) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // No clauses: (cond->> 1) => 1
    CljObject *result = eval_string("(cond->> 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)result));
}

// ============================================================================
// TEST: Source Verification
// ============================================================================

TEST(test_threading_source_thread_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that -> macro is defined and has source
    // Note: source prints to stdout, so we just verify it doesn't crash
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source '->)", g_test_eval_state);
    // source returns nil, so result should be NULL
    TEST_ASSERT_NULL(result);
}

TEST(test_threading_source_thread_last) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that ->> macro is defined and has source
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source '->>)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_threading_source_as) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that as-> macro is defined and has source
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source 'as->)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_threading_source_some_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that some-> macro is defined and has source
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source 'some->)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_threading_source_some_last) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that some->> macro is defined and has source
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source 'some->>)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_threading_source_cond_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that cond-> macro is defined and has source
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source 'cond->)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

TEST(test_threading_source_cond_last) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace (source is defined there)
    load_repl_namespace();
    
    // Verify that cond->> macro is defined and has source
    // Use qualified name: clojure.repl/source
    CljObject *result = eval_string("(clojure.repl/source 'cond->>)", g_test_eval_state);
    TEST_ASSERT_NULL(result);
}

// ============================================================================
// TEST: Macroexpansion Verification
// ============================================================================

TEST(test_threading_macroexpand_thread_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Verify macroexpansion: (macroexpand '(-> 5 (+ 3)))
    CljObject *result = eval_string("(macroexpand '(-> 5 (+ 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
    
    // Should expand to something like (+ 5 3)
    CljList *expanded = as_list(result);
    TEST_ASSERT_NOT_NULL(expanded);
}

TEST(test_threading_macroexpand_thread_last) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Verify macroexpansion: (macroexpand '(->> [1 2 3] (map inc)))
    CljObject *result = eval_string("(macroexpand '(->> [1 2 3] (map inc)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

TEST(test_threading_macroexpand_as) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Verify macroexpansion: (macroexpand '(as-> 5 x (+ x 3)))
    CljObject *result = eval_string("(macroexpand '(as-> 5 x (+ x 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

TEST(test_threading_macroexpand_some_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Verify macroexpansion: (macroexpand '(some-> 5 inc))
    CljObject *result = eval_string("(macroexpand '(some-> 5 inc))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

TEST(test_threading_macroexpand_cond_first) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Verify macroexpansion: (macroexpand '(cond-> 1 true inc))
    CljObject *result = eval_string("(macroexpand '(cond-> 1 true inc))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(list_type_matches(TAG(result)));
}

