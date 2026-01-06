/*
 * Regression tests for closure lexical symbol resolution.
 *
 * Clojure semantics:
 * - Unqualified symbols inside a function body resolve against the namespace where
 *   the function was defined (lexical), not the caller's dynamic *ns*.
 * - The dynamic var *ns* itself is not changed by calling a closure.
 */

#include "tests_common.h"
#include "namespace.h"
#include "eval.h"

TEST(test_closure_lexical_symbol_resolution_is_clojure_compliant) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // Define in namespace A.
    eval_string("(ns closure.lexical.a)", g_test_eval_state);
    eval_string("(def x 111)", g_test_eval_state);
    eval_string("(defn my-inc [n] (+ n 10))", g_test_eval_state);
    eval_string("(def f (fn [] x))", g_test_eval_state);
    eval_string("(def h (fn [n] (my-inc n)))", g_test_eval_state);
    eval_string("(def g (fn [] *ns*))", g_test_eval_state);

    // Switch to namespace B and shadow the same names.
    eval_string("(ns closure.lexical.b)", g_test_eval_state);
    eval_string("(def x 222)", g_test_eval_state);
    eval_string("(defn my-inc [n] (+ n 100))", g_test_eval_state);

    // Call A's functions from B using qualified symbols (avoid aliasing and extra resolution).
    CljObject *x_result = eval_string("(closure.lexical.a/f)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(x_result, "(closure.lexical.a/f) should return a number");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)x_result), "(closure.lexical.a/f) should return a fixnum");
    TEST_ASSERT_EQUAL_INT_MESSAGE(111, as_fixnum((CljValue)x_result), "closure should resolve x in its definition namespace");

    CljObject *h_result = eval_string("(closure.lexical.a/h 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(h_result, "(closure.lexical.a/h 1) should return a number");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)h_result), "(closure.lexical.a/h 1) should return a fixnum");
    TEST_ASSERT_EQUAL_INT_MESSAGE(11, as_fixnum((CljValue)h_result), "closure should resolve unqualified function calls in its definition namespace");

    // Dynamic *ns* should remain the caller's namespace (B), not switch to A.
    CljObject *ns_result = eval_string("(closure.lexical.a/g)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(ns_result, "(closure.lexical.a/g) should return a namespace object");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_NAMESPACE, TAG(ns_result), "*ns* should evaluate to a namespace object");

    CljNamespace *ns_obj = (CljNamespace*)ns_result;
    TEST_ASSERT_NOT_NULL(ns_obj->name);
    TEST_ASSERT_NOT_NULL(ns_obj->name->cname);
    TEST_ASSERT_EQUAL_STRING("closure.lexical.b", ns_obj->name->cname);

    // Also assert interpreter state was not mutated by calling the closure.
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name);
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns->name->cname);
    TEST_ASSERT_EQUAL_STRING("closure.lexical.b", g_test_eval_state->current_ns->name->cname);
}
