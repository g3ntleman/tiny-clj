/*
 * Plan Track A (ESP32 Serial REPL + core.async) — Script smoke tests.
 *
 * These tests "dogfood" the user-facing .clj scripts under libs/test/ by
 * loading them through the runtime on macOS.
 *
 * Goal:
 * - Keep Track A usable and stable while we evolve the runtime.
 * - Ensure core.async subset + GPIO simulation + event loop integration work.
 */

#include "tests_common.h"
#include <unistd.h>

static void require_readable_script_or_ignore(const char *path) {
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "path must not be NULL");
    if (access(path, R_OK) != 0) {
        char msg[256];
        test_snprintf(msg, sizeof(msg), "Skipping: missing script file %s", path);
        TEST_IGNORE_MESSAGE(msg);
    }
}

static void assert_load_file_ok(const char *path) {
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "path must not be NULL");

    char expr[512];
    test_snprintf(expr, sizeof(expr), "(load-file \"%s\")", path);

    TRY {
        (void)eval_string(expr, g_test_eval_state);
    } CATCH(ex) {
        if (ex) {
            print_exception((CLJException*)ex);
        }
        TEST_FAIL_MESSAGE("load-file threw an exception");
    } END_TRY
}

TEST(test_plan_trackA_core_async_smoke_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_readable_script_or_ignore("libs/test/core_async/smoke.clj");
    assert_load_file_ok("libs/test/core_async/smoke.clj");
}

TEST(test_plan_trackA_core_async_callbacks_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_readable_script_or_ignore("libs/test/core_async/callbacks.clj");
    assert_load_file_ok("libs/test/core_async/callbacks.clj");
}

TEST(test_plan_trackA_core_async_go_unsupported_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_readable_script_or_ignore("libs/test/core_async/go_unsupported.clj");
    assert_load_file_ok("libs/test/core_async/go_unsupported.clj");
}

TEST(test_plan_trackB_core_async_parking_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_readable_script_or_ignore("libs/test/core_async/parking.clj");
    assert_load_file_ok("libs/test/core_async/parking.clj");
}

TEST(test_plan_trackB_core_async_qualified_go_returns_channel_result) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        eval_string("(require 'clojure.core.async)", g_test_eval_state);
        eval_string(
            "(defn __drain_tasks_for_test__ [] "
            "  (if (run-next-task) "
            "    (__drain_tasks_for_test__) "
            "    nil))",
            g_test_eval_state);
        result = eval_string(
            "(let [out (clojure.core.async/go (+ 1 2))] "
            "  (__drain_tasks_for_test__) "
            "  (clojure.core.async/poll! out))",
            g_test_eval_state);
    } CATCH(ex) {
        if (ex) {
            print_exception((CLJException*)ex);
        }
        TEST_FAIL_MESSAGE("qualified clojure.core.async/go should evaluate without exception");
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "qualified clojure.core.async/go must yield fixnum result");
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_plan_trackA_gpio_smoke_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    require_readable_script_or_ignore("libs/test/gpio/smoke.clj");
    assert_load_file_ok("libs/test/gpio/smoke.clj");
}
