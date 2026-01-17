/*
 * RRD (Round-Robin Database) — Script smoke tests.
 *
 * These tests "dogfood" the user-facing .clj scripts under libs/test/rrd/ by
 * loading them through the runtime on macOS.
 *
 * Goal:
 * - Ensure the time-series DB (tiny-db.rrd) and its spline/classic handlers keep working.
 * - Keep the tests close to the end-user examples (the scripts remain runnable standalone).
 */

#include "tests_common.h"

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
    assert_load_file_ok("libs/test/rrd/smoke.clj");
}

TEST(test_rrd_spline_script)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    assert_load_file_ok("libs/test/rrd/spline_test.clj");
}

