/*
 * Unified Test Runner for MinUnit Tests
 * 
 * Single executable that runs all MinUnit tests with command-line options.
 * Uses manual registry (no dlsym) for STM32 compatibility.
 */

#include "../object.h"
#include "../clj_symbols.h"
#include "minunit.h"
#include "test_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// GLOBAL SETUP/TEARDOWN
// ============================================================================

static void global_setup(void) {
    init_special_symbols();
    meta_registry_init();
    cljvalue_pool_push(); // Create global autorelease pool for all tests
}

static void global_teardown(void) {
    cljvalue_pool_cleanup_all();
    symbol_table_cleanup();
    meta_registry_cleanup();
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

static void print_usage(const char *program) {
    printf("Usage: %s [options]\n\n", program);
    printf("Options:\n");
    printf("  (no args)          Run all tests\n");
    printf("  --help, -h         Show this help\n");
    printf("  --list, -l         List all tests\n");
    printf("  --suite SUITE      Run tests from specific suite\n");
    printf("  --test TEST        Run specific test\n\n");
    printf("Available test categories:\n");
    printf("  core     - Core functionality (unit, parser)\n");
    printf("  data     - Data structures (seq)\n");
    printf("  control  - Control flow (for_loops)\n");
    printf("  api      - Public API tests\n");
}

static void list_tests(void) {
    printf("\n📋 === Available MinUnit Tests ===\n\n");
    
    const char *current_suite = NULL;
    for (int i = 0; i < minunit_test_count; i++) {
        if (!current_suite || strcmp(current_suite, all_minunit_tests[i].suite) != 0) {
            current_suite = all_minunit_tests[i].suite;
            printf("\n[%s]\n", current_suite);
        }
        printf("  • %s\n", all_minunit_tests[i].name);
    }
    printf("\n");
}

static int run_suite(const char *suite_name) {
    printf("\n🧪 Running suite: %s\n", suite_name);
    
    int run_count = 0;
    int failed = 0;
    
    for (int i = 0; i < minunit_test_count; i++) {
        if (strcmp(all_minunit_tests[i].suite, suite_name) == 0) {
            printf("\n  Running %s...\n", all_minunit_tests[i].name);
            tests_run = 0;
            
            char *result = all_minunit_tests[i].test_func();
            run_count++;
            
            if (result) {
                printf("  ❌ %s FAILED: %s\n", all_minunit_tests[i].name, result);
                failed++;
            } else {
                printf("  ✅ %s PASSED\n", all_minunit_tests[i].name);
            }
        }
    }
    
    if (run_count == 0) {
        printf("❌ No tests found for suite: %s\n", suite_name);
        return 1;
    }
    
    printf("\n📊 Suite '%s': %d tests run, %d passed, %d failed\n", 
           suite_name, run_count, run_count - failed, failed);
    
    return failed > 0 ? 1 : 0;
}

static int run_test(const char *test_name) {
    printf("\n🧪 Running test: %s\n\n", test_name);
    
    for (int i = 0; i < minunit_test_count; i++) {
        if (strcmp(all_minunit_tests[i].name, test_name) == 0) {
            tests_run = 0;
            char *result = all_minunit_tests[i].test_func();
            
            if (result) {
                printf("❌ FAILED: %s\n", result);
                return 1;
            } else {
                printf("✅ PASSED\n");
                return 0;
            }
        }
    }
    
    printf("❌ Test not found: %s\n", test_name);
    printf("Use --list to see available tests\n");
    return 1;
}

static int run_all_tests(void) {
    printf("\n🧪 === Running All MinUnit Tests ===\n");
    
    int total_tests = 0;
    int total_failed = 0;
    
    const char *current_suite = NULL;
    
    for (int i = 0; i < minunit_test_count; i++) {
        if (!current_suite || strcmp(current_suite, all_minunit_tests[i].suite) != 0) {
            current_suite = all_minunit_tests[i].suite;
            printf("\n[%s]\n", current_suite);
        }
        
        printf("  • %s... ", all_minunit_tests[i].name);
        fflush(stdout);
        
        tests_run = 0;
        char *result = all_minunit_tests[i].test_func();
        total_tests++;
        
        if (result) {
            printf("❌ FAILED: %s\n", result);
            total_failed++;
        } else {
            printf("✅\n");
        }
    }
    
    printf("\n📊 === Test Summary ===\n");
    printf("Total:  %d test suites\n", total_tests);
    printf("Passed: %d test suites\n", total_tests - total_failed);
    printf("Failed: %d test suites\n", total_failed);
    
    if (total_failed == 0) {
        printf("\n✅ ALL TESTS PASSED\n\n");
        return 0;
    } else {
        printf("\n❌ SOME TESTS FAILED\n\n");
        return 1;
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char **argv) {
    global_setup();
    
    int result = 0;
    
    if (argc == 1) {
        // No arguments - run all tests
        result = run_all_tests();
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
    } else if (strcmp(argv[1], "--list") == 0 || strcmp(argv[1], "-l") == 0) {
        list_tests();
    } else if (strcmp(argv[1], "--suite") == 0 && argc > 2) {
        result = run_suite(argv[2]);
    } else if (strcmp(argv[1], "--test") == 0 && argc > 2) {
        result = run_test(argv[2]);
    } else {
        printf("Unknown option: %s\n\n", argv[1]);
        print_usage(argv[0]);
        result = 1;
    }
    
    global_teardown();
    return result;
}
