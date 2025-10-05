/*
 * Integration Tests Main
 * 
 * Consolidates all integration tests into a single executable for better IDE integration
 * and easier debugging while maintaining test isolation.
 */

#include "../unity.h"
#include "../tiny_clj.h"
#include "../CljObject.h"
#include "../vector.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../clj_parser.h"
// clojure_core functions are included via tiny_clj.h
#include "../function_call.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test setup and teardown
void setUp(void) {
    // Initialize symbol table
    init_special_symbols();
    
    // Initialize meta registry
    meta_registry_init();
}

void tearDown(void) {
    // Cleanup symbol table
    symbol_table_cleanup();
    
    // Cleanup autorelease pool
    cljvalue_pool_cleanup_all();
}

// ============================================================================
// INTEGRATION TEST FUNCTIONS
// ============================================================================

void test_basic_integration(void) {
    printf("\n=== Testing Basic Integration ===\n");
    
    // Test basic object creation and manipulation
    CljObject *vec = make_vector(3, 0);
    TEST_ASSERT_NOT_NULL(vec);
    
    CljPersistentVector *vec_data = as_vector(vec);
    vec_data->data[0] = make_int(1);
    vec_data->data[1] = make_int(2);
    vec_data->data[2] = make_int(3);
    vec_data->count = 3;
    
    // Test vector conj
    CljObject *result = vector_conj(vec, make_int(4));
    TEST_ASSERT_NOT_NULL(result);
    
    CljPersistentVector *result_data = as_vector(result);
    TEST_ASSERT_EQUAL_INT(4, result_data->count);
    TEST_ASSERT_EQUAL_INT(4, result_data->data[3]->as.i);
    
    printf("✓ Basic integration test passed\n");
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

typedef struct {
    const char *name;
    const char *suite;
    void (*test_func)(void);
} TestEntry;

static TestEntry integration_tests[] = {
    // Basic integration tests
    {"test_basic_integration", "basic", test_basic_integration},
};

static const int integration_test_count = sizeof(integration_tests) / sizeof(integration_tests[0]);

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --help, -h          Show this help message\n");
    printf("  --list, -l          List all available tests\n");
    printf("  --suite=NAME, -s    Run tests from specific suite\n");
    printf("  --test=NAME, -t     Run specific test\n");
    printf("  --all, -a           Run all tests (default)\n");
    printf("\nAvailable suites:\n");
    printf("  basic\n");
}

void list_tests(void) {
    printf("Available Integration Tests:\n\n");
    
    const char *current_suite = NULL;
    for (int i = 0; i < integration_test_count; i++) {
        if (current_suite == NULL || strcmp(current_suite, integration_tests[i].suite) != 0) {
            current_suite = integration_tests[i].suite;
            printf("\n=== %s ===\n", current_suite);
        }
        printf("  %s\n", integration_tests[i].name);
    }
}

int run_suite(const char *suite_name) {
    printf("Running integration tests from suite: %s\n\n", suite_name);
    
    int run_count = 0;
    for (int i = 0; i < integration_test_count; i++) {
        if (strcmp(integration_tests[i].suite, suite_name) == 0) {
            printf("Running %s...\n", integration_tests[i].name);
            integration_tests[i].test_func();
            run_count++;
        }
    }
    
    if (run_count == 0) {
        printf("No tests found for suite: %s\n", suite_name);
        return 1;
    }
    
    printf("\nRan %d integration tests from suite: %s\n", run_count, suite_name);
    return 0;
}

int run_test(const char *test_name) {
    printf("Running integration test: %s\n\n", test_name);
    
    for (int i = 0; i < integration_test_count; i++) {
        if (strcmp(integration_tests[i].name, test_name) == 0) {
            integration_tests[i].test_func();
            printf("\nIntegration test completed: %s\n", test_name);
            return 0;
        }
    }
    
    printf("Integration test not found: %s\n", test_name);
    return 1;
}

int run_all_tests(void) {
    printf("Running all integration tests...\n\n");
    
    UNITY_BEGIN();
    
    for (int i = 0; i < integration_test_count; i++) {
        RUN_TEST(integration_tests[i].test_func);
    }
    
    return UNITY_END();
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

// Test API
int run_integration_tests(void) {
    printf("=== Tiny-Clj Integration Test Runner ===\n\n");
    return run_all_tests();
}

#ifndef TINY_CLJ_EMBED_TESTS
int main(int argc, char *argv[]) {
    if (argc <= 1 || strcmp(argv[1], "--all") == 0) {
        return run_integration_tests();
    }
    if (strcmp(argv[1], "--list") == 0) {
        list_tests();
        return 0;
    }
    if (strncmp(argv[1], "--suite=", 8) == 0) {
        return run_suite(argv[1] + 8);
    }
    if (strncmp(argv[1], "--test=", 7) == 0) {
        return run_test(argv[1] + 7);
    }
    print_usage(argv[0]);
    return 1;
}
#endif
