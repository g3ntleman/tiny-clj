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
#include <stdio.h>

static int script_path_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
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
    const char *path = "libs/test/core_async/smoke.clj";
    if (!script_path_exists(path)) {
        TEST_IGNORE_MESSAGE("core_async smoke script not found (run from project root)");
        return;
    }
    assert_load_file_ok(path);
}

TEST(test_plan_trackA_core_async_callbacks_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    const char *path = "libs/test/core_async/callbacks.clj";
    if (!script_path_exists(path)) {
        TEST_IGNORE_MESSAGE("core_async callbacks script not found (run from project root)");
        return;
    }
    assert_load_file_ok(path);
}

TEST(test_plan_trackA_core_async_go_unsupported_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    const char *path = "libs/test/core_async/go_unsupported.clj";
    if (!script_path_exists(path)) {
        TEST_IGNORE_MESSAGE("core_async go_unsupported script not found (run from project root)");
        return;
    }
    assert_load_file_ok(path);
}

TEST(test_plan_trackA_gpio_smoke_script) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    const char *path = "libs/test/gpio/smoke.clj";
    if (!script_path_exists(path)) {
        TEST_IGNORE_MESSAGE("gpio smoke script not found (run from project root)");
        return;
    }
    assert_load_file_ok(path);
}
