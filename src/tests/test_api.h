#ifndef TINY_CLJ_TEST_API_H
#define TINY_CLJ_TEST_API_H

// Test API to run test suites directly from the main executable
// Each function returns 0 on success, non-zero on failures.
// Note: MinUnit tests have been migrated to Unity - use unity-tests executable instead

int run_integration_tests(void);
// run_unit_tests() removed - MinUnit tests migrated to Unity
// run_parser_tests() removed - MinUnit tests migrated to Unity

// Convenience helper to run all suites
static inline int run_all_tests(void) {
    int rc = 0;
    // run_unit_tests() removed - MinUnit tests migrated to Unity
    rc |= run_integration_tests();
    // run_parser_tests() removed - MinUnit tests migrated to Unity
    return rc;
}

#endif // TINY_CLJ_TEST_API_H


