/*
 * Tests for dynamic vars via (binding ...)
 *
 * Tiny-CLJ treats earmuffed symbols (*foo*) as dynamically bindable.
 * *ns* is a special case: it is represented by EvalState.current_ns for fast access.
 */

#include "tests_common.h"
#include "vector.h"

TEST(test_dynamic_binding_basic_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *result = eval_string("(binding [*x* 42] *x*)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_dynamic_binding_allows_nil_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *result = eval_string("(binding [*x* nil] *x*)", g_test_eval_state);
    TEST_ASSERT_NIL(result);
}

TEST(test_dynamic_binding_nested_restores_outer_value) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *result = eval_string("(binding [*x* 1] [(binding [*x* 2] *x*) *x*])", g_test_eval_state);
    assert_vector(result);

    CljVector *vec = as_vector(result);
    TEST_ASSERT_EQUAL_INT(2, vector_count(vec));

    CljObject *inner = (CljObject *)vector_nth(vec, 0);
    CljObject *outer = (CljObject *)vector_nth(vec, 1);

    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_TRUE(is_fixnum(inner));
    TEST_ASSERT_TRUE(is_fixnum(outer));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(inner));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(outer));
}

TEST(test_dynamic_binding_can_be_used_as_arg) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *result = eval_string("(binding [*x* 2] (+ 1 *x*))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_dynamic_binding_nil_can_be_used_as_arg) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *result = eval_string("(binding [*x* nil] (nil? *x*))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_dynamic_binding_rejects_non_dynamic_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    unsigned int base_depth = vector_count(g_test_eval_state->dynamic_bindings);

    CljObject *result = NULL;
    bool exception_caught = false;

    TRY {
        result = eval_string("(binding [x 1] x)", g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected exception for non-dynamic binding key");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL_UINT(base_depth, vector_count(g_test_eval_state->dynamic_bindings));
}

TEST(test_ns_star_binding_changes_current_ns_and_restores) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    evalstate_set_ns(g_test_eval_state, "test-dynamic-binding-ns");
    CljNamespace *before = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(before);

    CljNamespace *user_ns = ns_find("user");
    TEST_ASSERT_NOT_NULL_MESSAGE(user_ns, "Expected user namespace to exist");

    CljObject *result = eval_string("(binding [*ns* \"user\"] *ns*)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_NAMESPACE, TAG(result));
    TEST_ASSERT_EQUAL_PTR(user_ns, result);

    // Must restore to the namespace active before the binding.
    TEST_ASSERT_EQUAL_PTR(before, g_test_eval_state->current_ns);
}

TEST(test_ns_star_binding_rejects_nil_and_restores) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    evalstate_set_ns(g_test_eval_state, "test-dynamic-binding-ns-2");
    CljNamespace *before = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(before);

    unsigned int base_depth = vector_count(g_test_eval_state->dynamic_bindings);

    CljObject *result = NULL;
    bool exception_caught = false;

    TRY {
        result = eval_string("(binding [*ns* nil] *ns*)", g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected exception for (*ns* nil)");
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(before, g_test_eval_state->current_ns);
    TEST_ASSERT_EQUAL_UINT(base_depth, vector_count(g_test_eval_state->dynamic_bindings));
}

TEST(test_dynamic_binding_unwinds_on_exception) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    evalstate_set_ns(g_test_eval_state, "test-dynamic-binding-unwind");

    unsigned int base_depth = vector_count(g_test_eval_state->dynamic_bindings);
    CljNamespace *before = g_test_eval_state->current_ns;
    TEST_ASSERT_NOT_NULL(before);

    bool exception_caught = false;
    TRY {
        (void)eval_string("(binding [*x* 1] (/ 1 0))", g_test_eval_state);
        TEST_FAIL_MESSAGE("Expected exception inside binding");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY

    TEST_ASSERT_TRUE(exception_caught);
    TEST_ASSERT_EQUAL_UINT(base_depth, vector_count(g_test_eval_state->dynamic_bindings));
    TEST_ASSERT_EQUAL_PTR(before, g_test_eval_state->current_ns);

    // Ensure the evaluator still works after unwind.
    CljObject *result = eval_string("(binding [*x* 2] *x*)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result));
}

TEST(test_get_thread_bindings_snapshot_contains_dynamic_binding) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *snapshot = eval_string("(binding [*x* 42] (get-thread-bindings))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_TRUE(is_map(snapshot));

    CljSymbol *x = intern_symbol_global("*x*");
    TEST_ASSERT_NOT_NULL(x);

    ID val = map_get((CljMap*)snapshot, (ID)x);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
}
