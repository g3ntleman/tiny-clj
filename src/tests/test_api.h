#ifndef TINY_CLJ_TEST_API_H
#define TINY_CLJ_TEST_API_H

// Test API to run test suites directly from the main executable
// Each function returns 0 on success, non-zero on failures.

int run_unit_tests(void);
int run_integration_tests(void);
int run_parser_tests(void);

// Convenience helper to run all suites
static inline int run_all_tests(void) {
    int rc = 0;
    rc |= run_unit_tests();
    rc |= run_integration_tests();
    rc |= run_parser_tests();
    return rc;
}

#endif // TINY_CLJ_TEST_API_H


