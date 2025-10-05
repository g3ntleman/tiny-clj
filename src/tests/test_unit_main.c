/*
 * Unit Tests Main
 * 
 * Consolidates all unit tests into a single executable for better IDE integration
 * and easier debugging while maintaining test isolation.
 */

#include "../unity.h"
#include "../tiny_clj.h"
#include "../CljObject.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../clj_parser.h"
#include "../exception.h"
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
// INCLUDE ALL UNIT TEST FUNCTIONS
// ============================================================================

// From test_unit.c
void test_basic_creation(void);
void test_singleton_objects(void);
void test_symbol_creation(void);
void test_vector_creation(void);
void test_map_creation(void);
void test_reference_counting(void);
void test_equality(void);
void test_assertion_functions(void);
void test_exception_object_creation(void);
void test_vector_conj_basic(void);
void test_double_release_exception(void);
void test_empty_vector_singleton(void);
void test_empty_map_singleton(void);

// From test_assertions.c
void test_clj_assert_success(void);
void test_clj_assert_args_success(void);
void test_clj_assert_args_multiple_success(void);
void test_assertion_functions_exist(void);

// From test_global_singletons.c
void test_singleton_access_functions(void);
void test_singleton_pointer_equality(void);
void test_singleton_global_variables(void);
void test_singleton_pr_str(void);
void test_singleton_boolean_values(void);
void test_singleton_equality(void);

// From test_alloc_macros.c
void test_stack_alloc(void);
void test_heap_alloc(void);
void test_alloc_zero(void);
void test_mixed_allocation(void);
void test_allocation_with_autorelease(void);
void test_large_allocation(void);

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

typedef struct {
    const char *name;
    const char *suite;
    void (*test_func)(void);
} TestEntry;

static TestEntry unit_tests[] = {
    // Basic functionality tests
    {"test_basic_creation", "basic", test_basic_creation},
    {"test_singleton_objects", "basic", test_singleton_objects},
    {"test_symbol_creation", "basic", test_symbol_creation},
    {"test_vector_creation", "basic", test_vector_creation},
    {"test_map_creation", "basic", test_map_creation},
    {"test_reference_counting", "basic", test_reference_counting},
    {"test_equality", "basic", test_equality},
    {"test_assertion_functions", "basic", test_assertion_functions},
    {"test_exception_object_creation", "basic", test_exception_object_creation},
    {"test_vector_conj_basic", "basic", test_vector_conj_basic},
    {"test_empty_vector_singleton", "basic", test_empty_vector_singleton},
    {"test_empty_map_singleton", "basic", test_empty_map_singleton},
    {"test_double_release_exception", "basic", test_double_release_exception},

    // Assertion tests
    {"test_clj_assert_success", "assertions", test_clj_assert_success},
    {"test_clj_assert_args_success", "assertions", test_clj_assert_args_success},
    {"test_clj_assert_args_multiple_success", "assertions", test_clj_assert_args_multiple_success},
    {"test_assertion_functions_exist", "assertions", test_assertion_functions_exist},

    // Singleton tests
    {"test_singleton_access_functions", "singletons", test_singleton_access_functions},
    {"test_singleton_pointer_equality", "singletons", test_singleton_pointer_equality},
    {"test_singleton_global_variables", "singletons", test_singleton_global_variables},
    {"test_singleton_pr_str", "singletons", test_singleton_pr_str},
    {"test_singleton_boolean_values", "singletons", test_singleton_boolean_values},
    {"test_singleton_equality", "singletons", test_singleton_equality},

    // ALLOC macro tests
    {"test_stack_alloc", "memory", test_stack_alloc},
    {"test_heap_alloc", "memory", test_heap_alloc},
    {"test_alloc_zero", "memory", test_alloc_zero},
    {"test_mixed_allocation", "memory", test_mixed_allocation},
    {"test_allocation_with_autorelease", "memory", test_allocation_with_autorelease},
    {"test_large_allocation", "memory", test_large_allocation},
};

static const int unit_test_count = sizeof(unit_tests) / sizeof(unit_tests[0]);

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
    printf("  basic, singletons, memory, assertions\n");
}

void list_tests(void) {
    printf("Available Unit Tests:\n\n");
    
    const char *current_suite = NULL;
    for (int i = 0; i < unit_test_count; i++) {
        if (current_suite == NULL || strcmp(current_suite, unit_tests[i].suite) != 0) {
            current_suite = unit_tests[i].suite;
            printf("\n=== %s ===\n", current_suite);
        }
        printf("  %s\n", unit_tests[i].name);
    }
}

int run_suite(const char *suite_name) {
    printf("Running tests from suite: %s\n\n", suite_name);
    
    int run_count = 0;
    for (int i = 0; i < unit_test_count; i++) {
        if (strcmp(unit_tests[i].suite, suite_name) == 0) {
            printf("Running %s...\n", unit_tests[i].name);
            unit_tests[i].test_func();
            run_count++;
        }
    }
    
    if (run_count == 0) {
        printf("No tests found for suite: %s\n", suite_name);
        return 1;
    }
    
    printf("\nRan %d tests from suite: %s\n", run_count, suite_name);
    return 0;
}

int run_test(const char *test_name) {
    printf("Running test: %s\n\n", test_name);
    
    for (int i = 0; i < unit_test_count; i++) {
        if (strcmp(unit_tests[i].name, test_name) == 0) {
            unit_tests[i].test_func();
            printf("\nTest completed: %s\n", test_name);
            return 0;
        }
    }
    
    printf("Test not found: %s\n", test_name);
    return 1;
}

int run_all_tests(void) {
    printf("Running all unit tests...\n\n");
    
    UNITY_BEGIN();
    
    for (int i = 0; i < unit_test_count; i++) {
        RUN_TEST(unit_tests[i].test_func);
    }
    
    return UNITY_END();
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

// Test API
int run_unit_tests(void) {
    printf("=== Tiny-Clj Unit Test Runner ===\n\n");
    return run_all_tests();
}

#ifndef TINY_CLJ_EMBED_TESTS
int main(int argc, char *argv[]) {
    if (argc <= 1 || strcmp(argv[1], "--all") == 0) {
        return run_unit_tests();
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
