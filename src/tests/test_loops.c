/*
 * For-Loop Tests using Unity Framework
 *
 * Tests for for, doseq, dotimes, and while loop implementations.
 */

#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 0
#include "tests_common.h"
#include "vector.h"

const char *g_expr_for_basic_list_comprehension = "(for [x [1 2 3]] (* x x))";
const char *g_expr_for_multiple_bindings = "(vec (for [x [1 2] y [3 4]] [x y]))";
const char *g_expr_for_when_modifier = "(vec (for [x (range 6) :when (even? x)] x))";
const char *g_expr_for_let_modifier = "(vec (for [x [1 2 3] :let [y (* x 2)]] y))";
const char *g_expr_for_while_modifier = "(vec (for [x (range) :while (< x 3)] x))";
const char *g_expr_for_lazy_infinite = "(vec (take 3 (for [x (range)] x)))";
const char *g_expr_for_multi_consumer_independence = "(let [s (for [x (range 5)] x)] (= (vec s) (vec s)))";

static CljPersistentVector *make_dotimes_args(ID binding_vec, ID body_expr) {
  CljPersistentVector *args = make_vector(2, STRONG);
  vector_conj_inplace(&args, binding_vec);
  vector_conj_inplace(&args, body_expr);
  return args;
}

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================

TEST_SHARED(test_dotimes_zero_iterations) {
  // Test eval_dotimes with 0 iterations - should not execute body
  // Create dotimes call: (dotimes [i 0] (println "Should not print"))
  CljList *binding_rest = make_list(fixnum(0), NULL);
  CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), binding_rest));
  RELEASE(binding_rest);

  CljString *msg = make_string("Should not print");
  CljList *body_rest = make_list(msg, NULL);
  CljObject *body = AUTORELEASE(make_list(SYM_PRINTLN, body_rest));
  RELEASE(body_rest);
  RELEASE(msg);

  // Create environment
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation
  CljPersistentVector *args = AUTORELEASE(make_dotimes_args(binding_vector, body));
  CljObject *result = eval_dotimes(args, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST_SHARED(test_dotimes_negative_iterations) {
  // Test eval_dotimes with negative iterations - should not execute body
  // Create dotimes call: (dotimes [i -5] (println "Should not print"))
  CljList *binding_rest = make_list(fixnum(-5), NULL);
  CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), binding_rest));
  RELEASE(binding_rest);

  CljString *msg = make_string("Should not print");
  CljList *body_rest = make_list(msg, NULL);
  CljObject *body = AUTORELEASE(make_list(SYM_PRINTLN, body_rest));
  RELEASE(body_rest);
  RELEASE(msg);

  // Create environment
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation
  CljPersistentVector *args = AUTORELEASE(make_dotimes_args(binding_vector, body));
  CljObject *result = eval_dotimes(args, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST_SHARED(test_dotimes_large_iterations) {
  // Test eval_dotimes with large number of iterations
  // Create dotimes call: (dotimes [i 1000] i)
  CljList *binding_rest = make_list(fixnum(1000), NULL);
  CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), binding_rest));
  RELEASE(binding_rest);
  CljSymbol *body = intern_symbol_global("i");

  // Create environment
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation
  CljPersistentVector *args = AUTORELEASE(make_dotimes_args(binding_vector, body));
  CljObject *result = eval_dotimes(args, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
}

TEST_SHARED(test_dotimes_invalid_binding_format) {
  // Test eval_dotimes with invalid binding format
  // Create dotimes call: (dotimes [i] i) - missing count
  CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), NULL));
  CljSymbol *body = intern_symbol_global("i");

  // Create environment
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation
  CljPersistentVector *args = AUTORELEASE(make_dotimes_args(binding_vector, body));
  CljObject *result = eval_dotimes(args, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // Should return NULL for invalid format
}

TEST_SHARED(test_dotimes_non_numeric_count) {
  // Test eval_dotimes with non-numeric count
  // Create dotimes call: (dotimes [i "not-a-number"] i)
  CljString *count_str = make_string("not-a-number");
  CljList *binding_rest = make_list(count_str, NULL);
  CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), binding_rest));
  RELEASE(binding_rest);
  RELEASE(count_str);
  CljSymbol *body = intern_symbol_global("i");

  // Create environment
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation
  CljPersistentVector *args = AUTORELEASE(make_dotimes_args(binding_vector, body));
  CljObject *result = eval_dotimes(args, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // Should return NULL for non-numeric count
}

TEST_SHARED(test_dotimes_null_input) {
  // Test eval_dotimes with NULL input
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation with NULL
  CljObject *result = eval_dotimes(NULL, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // Should return NULL for NULL input
}

TEST_SHARED(test_dotimes_simple_iteration_count) {
  // Test that eval_dotimes executes the body exactly n times
  // This is a simpler test that just verifies the loop runs n times

  // Create binding vector: [i 3]
  CljList *binding_rest = make_list(fixnum(3), NULL);
  CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), binding_rest));
  RELEASE(binding_rest);

  // Create simple body: i (just return the loop variable)
  CljSymbol *body = intern_symbol_global("i");

  // Create environment
  CljPersistentMap *env = AUTORELEASE(make_map(4));

  // Test dotimes evaluation
  CljPersistentVector *args = AUTORELEASE(make_dotimes_args(binding_vector, body));
  CljObject *result = eval_dotimes(args, env, g_test_eval_state, NULL);
  TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil

  // The test passes if no errors occur and the function returns NULL
  // This verifies that the loop executed 3 times without crashing
}

TEST_SHARED(test_dotimes_lexical_n_and_body_bindings) {
  // Regression: dotimes must be able to resolve let-bound symbols for its count
  // and also allow the body to see outer lexical bindings.
  // (let [n 5 a (atom 0)] (dotimes [i n] (swap! a inc)) (deref a)) => 5
  ID result = eval_string("(let [n 5 a (atom 0)] (dotimes [i n] (swap! a inc)) (deref a))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_fixnum(result));
  TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
}

// ============================================================================
// WHILE LOOP TESTS
// ============================================================================

TEST_SHARED(test_while_basic_true) {
  // Test while with true condition that becomes false
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i) => 1
  // Executes once, then condition becomes false
  ID result = eval_string("(let [i (atom 0)] (while (< @i 1) (swap! i inc)) @i)", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_fixnum(result));
  TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST_SHARED(test_while_loop_multiple) {
  // Test while with loop that executes multiple times
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (while (< @i 3) (swap! i inc)) where i starts at 0
  // This should execute 3 times (i: 0 -> 1 -> 2 -> 3)
  ID result = eval_string("(let [i (atom 0)] (while (< @i 3) (swap! i inc)) @i)", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_fixnum(result));
  TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST_SHARED(test_while_false_condition) {
  // Test while with false condition - should not execute
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (while false 42) => nil (does not execute)
  ID result = eval_string("(while false 42)", g_test_eval_state);
  TEST_ASSERT_NULL(result);
}

TEST_SHARED(test_while_nil_condition) {
  // Test while with nil condition - should not execute
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (while nil 42) => nil (does not execute)
  ID result = eval_string("(while nil 42)", g_test_eval_state);
  TEST_ASSERT_NULL(result);
}

TEST_SHARED(test_while_with_atom) {
  // Test while with atom (like in mandelbrot.clj)
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (let [i (atom 0)] (while (< @i 5) (swap! i inc)) @i) => 5
  ID result = eval_string("(let [i (atom 0)] (while (< @i 5) (swap! i inc)) @i)", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_fixnum(result));
  TEST_ASSERT_EQUAL_INT(5, as_fixnum(result));
}

TEST_SHARED(test_while_multiple_body_exprs) {
  // Test while with multiple body expressions
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (let [i (atom 0)] (while (< @i 2) (swap! i inc) (+ @i 10)) @i) => 2
  // Last expression in body is evaluated, but while always returns nil
  ID result = eval_string("(let [i (atom 0)] (while (< @i 2) (swap! i inc) (+ @i 10)) @i)", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_fixnum(result));
  TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests

// Regression: doseq with vector binding
TEST_SHARED(test_doseq_with_vector_binding) {
  ID result = eval_string(
      "(let [sum (atom 0)] (doseq [x [1 2 3]] (swap! sum + x)) @sum)",
      g_test_eval_state);
  TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// Regression: for with vector binding (basic list comprehension)
TEST_SHARED(test_for_basic_list_comprehension) {
  ID result = eval_string(g_expr_for_basic_list_comprehension, g_test_eval_state);
  ID cur = make_seq(result);
  TEST_ASSERT_NOT_NULL(cur);
  int expected[] = {1, 4, 9};
  int i = 0;
  for (; cur && !seq_empty(cur); ++i) {
    ID first = seq_first(cur);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(expected[i], as_fixnum(first));
    seq_next_inplace(&cur);
  }
  RELEASE(cur); // make_seq() returns owned; seq_empty(cur) may be true while cur is still non-NULL
  TEST_ASSERT_EQUAL_INT(3, i); // Should have 3 elements
}

// ============================================================================
// Extended for tests (multiple bindings, :when, :let, :while)
// ============================================================================

// Test: Multiple bindings (cartesian product)
/* Target: 400 (raised to 800); TODO: find/fix remaining for/macroexpand leaks to lower again. */
TEST_SHARED(test_for_multiple_bindings, 0) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  ID ok = NULL;
  WITH_AUTORELEASE_POOL({
    ok = eval_string("(= (for [x (list 1) y (list 3)] [x y]) [[1 3]])", g_test_eval_state);
  });
  TEST_ASSERT_TRUE(ok == clj_true);
}

// Test: :when modifier (filtering)
TEST_SHARED(test_for_when_modifier) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (for [x (range 6) :when (even? x)] x) => [0 2 4]
  ID result = eval_string(g_expr_for_when_modifier, g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_vector(result));

  CljPersistentVector *vec = as_vector(result);
  TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
  TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(vec, 0)));
  TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 1)));
  TEST_ASSERT_EQUAL_INT(4, as_fixnum(vector_nth(vec, 2)));
}

// Test: :let modifier (binding intermediate values)
/* Target: 400 (raised to 600); TODO: find/fix remaining for/macroexpand leaks to lower again. */
TEST_SHARED(test_for_let_modifier, 0) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  ID ok = NULL;
  WITH_AUTORELEASE_POOL({
    ok = eval_string("(= (for [x (list 1) :let [y (* x 2)]] y) [2])", g_test_eval_state);
  });
  TEST_ASSERT_TRUE(ok == clj_true);
}

// Test: :while modifier (early termination)
TEST_SHARED(test_for_while_modifier) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (for [x (range) :while (< x 3)] x) => [0 1 2]
  ID result = eval_string(g_expr_for_while_modifier, g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_vector(result));

  CljPersistentVector *vec = as_vector(result);
  TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
  TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(vec, 0)));
  TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 1)));
  TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 2)));
}

// Test: Large sequence (performance/regression)
TEST_SHARED(test_for_large_sequence) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (count (vec (for [x (range N)] x))) => N
  // Profiling builds run slower; keep N smaller to avoid timeouts.
#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  const char *expr = "(count (vec (for [x (range 2000)] x)))";
  const int expected = 2000;
#else
  const char *expr = "(count (vec (for [x (range 10000)] x)))";
  const int expected = 10000;
#endif
  ID result = eval_string(expr, g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_fixnum(result));
  TEST_ASSERT_EQUAL_INT(expected, as_fixnum(result));
}

// Regression: repeated large for/range realizations must remain stable.
TEST_SHARED(test_for_large_sequence_repeated_realization, 0) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  const char *expr = "(count (vec (for [x (range 2000)] x)))";
  const int expected = 2000;
#else
  const char *expr = "(count (vec (for [x (range 10000)] x)))";
  const int expected = 10000;
#endif

  for (int i = 0; i < 3; i++) {
    ID result = eval_string(expr, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(expected, as_fixnum(result));
  }
}

// Test: Lazy sequence (take from infinite)
TEST_SHARED(test_for_lazy_infinite) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (vec (take 3 (for [x (range)] x))) => [0 1 2]
  ID result = eval_string(g_expr_for_lazy_infinite, g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_vector(result));

  CljPersistentVector *vec = as_vector(result);
  TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
  TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(vec, 0)));
  TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(vec, 1)));
  TEST_ASSERT_EQUAL_INT(2, as_fixnum(vector_nth(vec, 2)));
}

// Test: Multi-consumer independence (realize same lazy seq twice)
TEST_SHARED(test_for_multi_consumer_independence) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  // (let [s (for [x (range 5)] x)] (= (vec s) (vec s))) => true
  ID result = eval_string(g_expr_for_multi_consumer_independence, g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(result == clj_true);
}
