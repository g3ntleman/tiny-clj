/*
 * Clojure-level stacktrace tests (DEBUG builds only).
 *
 * These tests verify that eval_function_call correctly pushes/pops
 * g_clj_callstack and that throw captures the frames into ex->stacktrace.
 *
 * Requires clojure.core (defn, throw). Group name "test_clojure_stacktrace"
 * is NOT in k_default_no_core_groups so setUp loads core automatically.
 */

#include "tests_common.h"

#ifdef DEBUG

TEST(test_clojure_stacktrace_contains_function_names) {
    eval_string("(do"
                "  (defn stacktrace-c [] (throw \"boom\"))"
                "  (defn stacktrace-b [] (stacktrace-c))"
                "  (defn stacktrace-a [] (stacktrace-b)))",
                g_test_eval_state);

    TRY {
        eval_string("(stacktrace-a)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown");
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_NOT_NULL_MESSAGE(ex->stacktrace,
            "stacktrace must be non-NULL in DEBUG builds");
        const char *st = clj_string_data(ex->stacktrace);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(st, "stacktrace-a"),
            "stacktrace must contain stacktrace-a");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(st, "stacktrace-b"),
            "stacktrace must contain stacktrace-b");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(st, "stacktrace-c"),
            "stacktrace must contain stacktrace-c");
    } END_TRY
}

TEST(test_clojure_stacktrace_cleared_after_catch) {
    // After CATCH restores saved_callstack_depth, depth must be 0 at top level.
    eval_string("(do"
                "  (defn st-inner [] (throw \"x\"))"
                "  (defn st-outer [] (st-inner)))",
                g_test_eval_state);

    TRY {
        eval_string("(st-outer)", g_test_eval_state);
        TEST_FAIL_MESSAGE("Should have thrown");
    } CATCH(ex) {
        (void)ex;
    } END_TRY

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, vector_count(g_clj_callstack),
        "callstack depth must be restored to 0 after CATCH");
}

#endif // DEBUG
