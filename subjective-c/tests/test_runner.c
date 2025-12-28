/*
 * Unified Test Runner for Subjective-C and Tiny-CLJ
 * 
 * Supports both simple subjective-c tests and complex tiny-clj tests
 * with command-line interface, setUp/tearDown, and advanced features.
 */

#include "test_common.h"
#include "test_registry.h"
#include "unity.h"
#include "unity/src/unity_internals.h"  // For Unity.TestFile and Unity.CurrentTestLineNumber
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <stdlib.h>

// Flag to track if summary was already printed
static bool g_summary_printed = false;
static clock_t g_start_time = 0;

// Signal handler to print summary on crash
static void print_summary_on_exit(void) {
    if (!g_summary_printed) {
        fflush(stdout);
        fflush(stderr);
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("TEST SUMMARY (printed due to early termination)\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        fflush(stdout);
        UNITY_END();
        fflush(stdout);
        if (g_start_time > 0) {
            clock_t end_time = clock();
            double elapsed = ((double)(end_time - g_start_time)) / CLOCKS_PER_SEC;
            printf("\nTotal runtime: %.3fs\n", elapsed);
            fflush(stdout);
        }
        g_summary_printed = true;
    }
}

static void signal_handler(int sig) {
    // Print summary immediately
    print_summary_on_exit();
    fflush(stdout);
    fflush(stderr);
    // Reset to default handler and re-raise
    signal(sig, SIG_DFL);
    raise(sig);
}

// Check if we're building for tiny-clj (has more complex setup)
#ifdef TINY_CLJ_TEST_RUNNER
// Include tiny-clj specific setup/teardown
// These are defined in unity_test_runner.c for tiny-clj
extern void setUp(void);
extern void tearDown(void);
extern void run_shared_tests_batched(void);
extern void set_unity_test_file_info(const SubjectiveCTestEntry *entry);
extern void run_test_with_exception_handling(const SubjectiveCTestEntry *entry);
extern void run_tests_by_registry_impl(void);
extern void run_specific_test_impl(const char *test_name_or_pattern);
#else
// Simple setup/teardown for subjective-c tests
void setUp(void) {}
void tearDown(void) {}
#endif

// Helper function to set Unity's TestFile and CurrentTestLineNumber for correct error reporting
#ifndef TINY_CLJ_TEST_RUNNER
static void set_unity_test_file_info(const SubjectiveCTestEntry *entry) {
    if (entry->file) {
        Unity.TestFile = entry->file;
    }
    if (entry->line > 0) {
        Unity.CurrentTestLineNumber = (UNITY_LINE_TYPE)entry->line;
    }
}

// Helper function to run a single test with exception handling
static void run_test_with_exception_handling(const SubjectiveCTestEntry *entry) {
    // Simple execution for subjective-c tests
    const char *cname = entry->qualified_name ? entry->qualified_name : entry->name;
    UnityDefaultTestRun(entry->fn, cname, (UNITY_LINE_TYPE)entry->line);
}
#endif

// Run all tests by registry
static void run_tests_by_registry(void) {
#ifdef TINY_CLJ_TEST_RUNNER
    // tiny-clj has its own implementation in unity_test_runner.c
    extern void run_tests_by_registry_impl(void);
    run_tests_by_registry_impl();
#else
    // Simple execution for subjective-c tests
    size_t test_count;
    const SubjectiveCTestEntry *all_tests = subjective_c_test_registry_entries(&test_count);
    
    for (size_t i = 0; i < test_count; i++) {
        setUp();
        set_unity_test_file_info(&all_tests[i]);
        run_test_with_exception_handling(&all_tests[i]);
        tearDown();
    }
#endif
}

// Run specific test by name or pattern
static void run_specific_test(const char *test_name_or_pattern) {
#ifdef TINY_CLJ_TEST_RUNNER
    // tiny-clj has its own implementation in unity_test_runner.c
    extern void run_specific_test_impl(const char *test_name_or_pattern);
    run_specific_test_impl(test_name_or_pattern);
    return;
#endif
#ifndef TINY_CLJ_TEST_RUNNER
    if (strchr(test_name_or_pattern, '*') != NULL) {
        size_t test_count;
        const SubjectiveCTestEntry *all_tests = subjective_c_test_registry_entries(&test_count);
        int found = 0;
        
        for (size_t i = 0; i < test_count; i++) {
            if (subjective_c_test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern) ||
                subjective_c_test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                found++;
            }
        }
        
        if (found == 0) {
            printf("❌ No tests found matching pattern: %s\n", test_name_or_pattern);
            return;
        }
        
        for (size_t i = 0; i < test_count; i++) {
            if (subjective_c_test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern) ||
                subjective_c_test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                setUp();
                set_unity_test_file_info(&all_tests[i]);
                run_test_with_exception_handling(&all_tests[i]);
                tearDown();
            }
        }
    } else {
        SubjectiveCTestEntry *test = subjective_c_test_registry_find_by_qualified_name(test_name_or_pattern);
        if (!test) {
            test = subjective_c_test_registry_find(test_name_or_pattern);
        }
        
        if (test) {
            setUp();
            set_unity_test_file_info(test);
            run_test_with_exception_handling(test);
            tearDown();
        } else {
            printf("ERROR: Test '%s' not found.\n", test_name_or_pattern);
            Unity.NumberOfTests++;
            Unity.TestFailures++;
        }
    }
#endif
}

// Print usage information
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nOptions:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  --list                  List all available tests\n");
    printf("  --test <test_name>      Run a specific test (supports wildcards)\n");
    printf("  --quiet                 Reduce memory leak reporting for cleaner output\n");
#ifdef TINY_CLJ_TEST_RUNNER
    printf("  --memory-summary        Show memory profiler summary after all tests\n");
#endif
    printf("\nExamples:\n");
    printf("  %s                      Run all tests\n", program_name);
    printf("  %s --test test_map/*    Run all map tests\n", program_name);
    printf("  %s --list               List all available tests\n", program_name);
}

int main(int argc, char **argv) {
    // Initialize autorelease pool before running tests
    autorelease_pool_init();
    
    // Register signal handlers to print summary on crash
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    atexit(print_summary_on_exit);
    
#ifdef ENABLE_MEMORY_PROFILING
    enable_memory_profiling(true);
    set_memory_leak_reporting_enabled(false);
    set_memory_verbose_mode(false);
#endif
    
    UNITY_BEGIN();
    g_start_time = clock();
    clock_t start_time = g_start_time;
    
    bool show_memory_summary = false;
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[1], "--list") == 0) {
            subjective_c_test_registry_list_all();
            return 0;
        } else if (strcmp(argv[1], "--quiet") == 0) {
#ifdef ENABLE_MEMORY_PROFILING
            set_memory_leak_reporting_enabled(false);
#endif
            run_tests_by_registry();
        } else if (strcmp(argv[1], "--memory-summary") == 0) {
#ifdef TINY_CLJ_TEST_RUNNER
            show_memory_summary = true;
#ifdef ENABLE_MEMORY_PROFILING
            set_memory_verbose_mode(false);
            set_memory_leak_reporting_enabled(true);
#endif
            run_tests_by_registry();
#else
            printf("--memory-summary is only available for tiny-clj tests\n");
            return 1;
#endif
        } else if (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-test") == 0) {
            if (argc < 3) {
                return 1;
            }
            run_specific_test(argv[2]);
        } else {
            run_tests_by_registry();
        }
    } else {
        run_tests_by_registry();
    }
    
    clock_t end_time = clock();
    double elapsed = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    // Print test summary BEFORE cleanup to ensure it's always shown
    // even if cleanup crashes
    int result = UNITY_END();
    g_summary_printed = true;  // Mark as printed
    
    printf("\n");
#ifdef TINY_CLJ_TEST_RUNNER
    printf("Total runtime: %.3fs\n", elapsed);
#else
    printf("Tests completed in %.2f seconds\n", elapsed);
#endif
    
#ifdef TINY_CLJ_TEST_RUNNER
    // Tiny-CLJ specific cleanup - use extern function (no includes)
    // Called AFTER summary to ensure summary is always printed
    extern void tiny_clj_test_cleanup(bool show_memory_summary);
    tiny_clj_test_cleanup(show_memory_summary);
#endif
    
    return result;
}

