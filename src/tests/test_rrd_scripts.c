/*
 * RRD (Round-Robin Database) — Script smoke tests.
 *
 * These tests "dogfood" the user-facing .clj scripts under libs/test/rrd/ by
 * loading them through the runtime on macOS.
 *
 * Goal:
 * - Ensure the time-series DB (tiny-db.rrd) and its spline/classic handlers keep working.
 * - Keep the tests close to the end-user examples (the scripts remain runnable standalone).
 *
 * Paths are relative to project root; run unit-tests with cwd = project root
 * (e.g. ./build/unit-tests from repo root, or run-unit-tests target).
 */

#include "tests_common.h"
#include <stdio.h>

static int script_path_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void assert_load_file_ok(const char *path)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(path, "path must not be NULL");

    char expr[512];
    test_snprintf(expr, sizeof(expr), "(load-file \"%s\")", path);

    TRY {
        (void)eval_string(expr, g_test_eval_state);
    } CATCH(ex) {
        if (ex) {
            print_exception((CLJException *)ex);
        }
        TEST_FAIL_MESSAGE("load-file threw an exception");
    } END_TRY
}

TEST(test_rrd_smoke_script)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    const char *path = "libs/test/rrd/smoke.clj";
    if (!script_path_exists(path)) {
        TEST_IGNORE_MESSAGE("RRD script not found (run from project root)");
        return;
    }
    assert_load_file_ok(path);
}

TEST(test_rrd_spline_script)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    const char *path = "libs/test/rrd/spline_test.clj";
    if (!script_path_exists(path)) {
        TEST_IGNORE_MESSAGE("RRD script not found (run from project root)");
        return;
    }
    assert_load_file_ok(path);
}

