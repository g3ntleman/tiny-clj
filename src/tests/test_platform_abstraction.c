#include "minunit.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// PLATFORM ABSTRACTION TESTS
// ============================================================================

static char *test_platform_name_macos(void) {
    const char *name = platform_name();
    mu_assert("Platform name should not be NULL", name != NULL);
    mu_assert("Should be macOS on macOS", strcmp(name, "macOS") == 0);
    return NULL;
}

static char *test_platform_init(void) {
    // Should not crash
    platform_init();
    return NULL;
}

static char *test_platform_print(void) {
    // Test with valid string
    platform_print("test message");
    
    // Test with NULL (should not crash)
    platform_print(NULL);
    
    return NULL;
}

static char *test_platform_set_stdin_nonblocking(void) {
    // Test enabling non-blocking mode
    int result = platform_set_stdin_nonblocking(1);
    mu_assert("Should enable non-blocking mode", result == 0);
    
    // Test disabling non-blocking mode
    result = platform_set_stdin_nonblocking(0);
    mu_assert("Should disable non-blocking mode", result == 0);
    
    return NULL;
}

static char *test_platform_readline_nb_basic(void) {
    char buffer[256];
    
    // Test with NULL buffer
    int result = platform_readline_nb(NULL, 256);
    mu_assert("Should return error for NULL buffer", result == -1);
    
    // Test with zero max size
    result = platform_readline_nb(buffer, 0);
    mu_assert("Should return error for zero max size", result == -1);
    
    // Test with negative max size
    result = platform_readline_nb(buffer, -1);
    mu_assert("Should return error for negative max size", result == -1);
    
    return NULL;
}

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

static char *test_platform_readline_nb_edge_cases(void) {
    char buffer[256];
    
    // Test with very small buffer
    int result = platform_readline_nb(buffer, 1);
    mu_assert("Should handle small buffer", result == -1);
    
    // Test with large buffer (should not crash, may return 0 if no data)
    result = platform_readline_nb(buffer, 10000);
    mu_assert("Should handle large buffer without crashing", result == 0);
    
    return NULL;
}

static char *test_platform_consistency(void) {
    // Test that platform functions are consistent
    const char *name1 = platform_name();
    const char *name2 = platform_name();
    
    mu_assert("Platform name should be consistent", strcmp(name1, name2) == 0);
    
    return NULL;
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

static char *test_platform_full_cycle(void) {
    // Test complete platform cycle
    platform_init();
    
    const char *name = platform_name();
    mu_assert("Should have valid platform name", name != NULL);
    
    platform_print("Platform test message");
    
    int result = platform_set_stdin_nonblocking(1);
    mu_assert("Should enable non-blocking mode", result == 0);
    
    result = platform_set_stdin_nonblocking(0);
    mu_assert("Should disable non-blocking mode", result == 0);
    
    return NULL;
}

// ============================================================================
// TEST SUITE RUNNER
// ============================================================================

char *run_platform_abstraction_tests(void) {
    mu_run_test(test_platform_name_macos);
    mu_run_test(test_platform_init);
    mu_run_test(test_platform_print);
    mu_run_test(test_platform_set_stdin_nonblocking);
    mu_run_test(test_platform_readline_nb_basic);
    mu_run_test(test_platform_readline_nb_edge_cases);
    mu_run_test(test_platform_consistency);
    mu_run_test(test_platform_full_cycle);
    
    return NULL;
}
