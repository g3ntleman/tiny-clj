/*
 * Unity Test Runner for Tiny-CLJ
 * 
 * Central test runner that includes all test suites with command-line parameter support.
 */

#include "tests_common.h"
#include "test_registry.h"
#include "memory_profiler.h"
#include "../tiny_clj.h"
#include "../event_loop.h"
#include "unity/src/unity_internals.h"  // For Unity.TestFile and Unity.CurrentTestLineNumber

// Forward declaration for clojure_core_set_quiet
extern void clojure_core_set_quiet(bool quiet);

// Access to global memory stats for leak checking
extern MemoryStats g_memory_stats;
extern bool g_memory_verbose_mode;

// Forward declaration for load_clojure_core
int load_clojure_core(EvalState *st);

// Static flags to ensure initialization happens only once
static bool g_special_symbols_initialized = false;

// Global test EvalState (available in all tests via tests_common.h)
EvalState *g_test_eval_state = NULL;

// ============================================================================
// GLOBAL SETUP/TEARDOWN
// ============================================================================


void setUp(void) {
    // Reset memory profiler statistics BEFORE each test
    memory_profiler_reset();
    
    // Suppress time output in tests
    set_suppress_time_output(true);
    
    // CRITICAL: Reset all runtime state for test isolation
    // This consolidates all reset operations for CljRuntime
    runtime_reset(&g_runtime);
    
    runtime_init(&g_runtime);
    event_loop_init();  // Initialize event loop keywords
    
    // Set clojure.core to quiet mode BEFORE any eval state is created
    // This suppresses "=== Loading Clojure Core Functions ===" output
    clojure_core_set_quiet(true);
    
    // Initialize special symbols only once (they should persist across tests)
    if (!g_special_symbols_initialized) {
        init_special_symbols();
        g_special_symbols_initialized = true;
    }
    
    
    if (!g_runtime.builtins_registered) {
        meta_registry_init();
        register_builtins();
        g_runtime.builtins_registered = true;
    }
        
#ifdef ENABLE_MEMORY_PROFILING
        MEMORY_PROFILER_INIT();
        enable_memory_profiling(true);
        set_memory_verbose_mode(false);
        // Update cached debug output flag after initialization
        extern void memory_update_debug_output_active(void);
        memory_update_debug_output_active();
#endif

#ifdef DEBUG
        enable_zombie_mode();
#endif
    
    // Load clojure.core for each test (refresh state between tests)
    // Use autorelease pool for load_clojure_core to handle AUTORELEASE calls
    WITH_AUTORELEASE_POOL({
        evalstate_reset(&g_test_eval_state, true);
    });
}

// Get the global test evalState (with inc available)
EvalState* test_get_eval_state(void) {
    return g_test_eval_state;
}

void tearDown(void) {
    // Reset time output suppression (for consistency)
    set_suppress_time_output(false);
    
    // runtime_reset() is called in setUp() before runtime_init()
    // Also called here in tearDown() to clean up after the test
    
    // JUnit-style: Only print memory stats if verbose mode is enabled
    // (memory_profiler_check_leaks already handles leak reporting)
    if (g_memory_verbose_mode) {
        memory_profiler_print_stats("Test Complete");
    }
    // Check for leaks silently (reporting disabled by default - no output for passing tests)
    memory_profiler_check_leaks("Test Complete");
    
    runtime_reset(&g_runtime);
}

// ============================================================================
// EMBEDDED ARRAY TESTS (defined in this file)
// ============================================================================

// Forward declarations for embedded array tests (now using TEST macro)
// No forward declarations needed - TEST macro handles registration automatically

// Note: All other tests are automatically registered via TEST() macro
// No extern declarations needed for tests using TEST() macro

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================


// Legacy test group functions removed - all tests use TEST() macro for automatic registration
// Legacy command-line options now use run_tests_by_registry() which runs all registered tests

// ============================================================================
// MAIN FUNCTION
// ============================================================================

// ============================================================================
// NEW COMMAND-LINE INTERFACE
// ============================================================================

static void print_new_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nOptions:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  --list                  List all available tests\n");
    printf("  --test <test_name>      Run a specific test (supports wildcards)\n");
    printf("  --quiet                 Reduce memory leak reporting for cleaner output\n");
    printf("  --memory-summary        Show memory profiler summary after all tests\n");
    printf("\nExamples:\n");
    printf("  %s                      Run all tests\n", program_name);
    printf("  %s --test test_atom/*   Run all atom tests\n", program_name);
    printf("  %s --memory-summary     Run all tests with memory summary\n", program_name);
}

// Helper function to set Unity's TestFile and CurrentTestLineNumber for correct error reporting
static void set_unity_test_file_info(const Test *test) {
    if (test->file) {
        Unity.TestFile = test->file;
    }
    if (test->line > 0) {
        Unity.CurrentTestLineNumber = (UNITY_LINE_TYPE)test->line;
    }
}

// Helper function to run a single test with exception handling
static void run_test_with_exception_handling(const Test *test) {
    TRY {
        // Call Unity directly with the line number from the test registry.
        // This avoids using RUN_TEST(__LINE__) from this file, so that the
        // reported line matches the TEST() macro in the test source file.
        const char *name = test->qualified_name ? test->qualified_name : test->name;
        UnityDefaultTestRun(test->func, name, (UNITY_LINE_TYPE)test->line);
    } CATCH(ex) {
        // Unhandled exception caught - mark test as failed
        if (ex) {
            fprintf(stderr, "Unhandled exception in %s: %s - %s\n", 
                    test->qualified_name, ex->type, ex->message);
            if (ex->stacktrace) {
                print_exception(ex);
            }
        }
        Unity.TestFailures++;
        Unity.CurrentTestFailed = 1;
    } END_TRY
}

// One-line test runner: Unity already prints one line per test
static void run_tests_by_registry(void) {
    size_t test_count;
    Test *all_tests = test_registry_get_all(&test_count);
    for (size_t i = 0; i < test_count; i++) {
        set_unity_test_file_info(&all_tests[i]);
        run_test_with_exception_handling(&all_tests[i]);
    }
}

static bool contains_wildcard(const char *pattern) {
    return strchr(pattern, '*') != NULL;
}

static void run_specific_test(const char *test_name_or_pattern) {
    // Check if it's a wildcard pattern
    if (contains_wildcard(test_name_or_pattern)) {
        // Use pattern matching logic
        size_t test_count;
        Test *all_tests = test_registry_get_all(&test_count);
        int found = 0;
        
        // First pass: count matching tests
        for (size_t i = 0; i < test_count; i++) {
            // Try matching against qualified name first
            if (test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern)) {
                found++;
            }
            // Fallback to simple name matching for backward compatibility
            else if (test_name_matches_pattern(all_tests[i].name, test_name_or_pattern)) {
                found++;
            }
        }
        
        if (found == 0) {
            printf("❌ No tests found matching pattern: %s\n", test_name_or_pattern);
            printf("Use --list to see available tests\n");
            return;
        }
        
        // One-line output for pattern matching
        for (size_t i = 0; i < test_count; i++) {
            if (test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern)) {
                set_unity_test_file_info(&all_tests[i]);
                run_test_with_exception_handling(&all_tests[i]);
            }
        }
    } else {
        // Exact name match (existing logic)
        Test *test = NULL;
        
        // First try to find by qualified name
        test = test_registry_find_by_qualified_name(test_name_or_pattern);
        
        // If not found, try by simple name (backward compatibility)
        if (!test) {
            test = test_registry_find(test_name_or_pattern);
        }
        
        if (test) {
            set_unity_test_file_info(test);
            run_test_with_exception_handling(test);
            // Summary will be printed at end of main()
        } else {
            // Test not found - print error message
            printf("ERROR: Test '%s' not found.\n", test_name_or_pattern);
            printf("Use --list to see all available tests.\n");
            Unity.NumberOfTests++;
            Unity.TestFailures++;
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Enable memory profiling for tests (only in DEBUG builds)
#ifdef ENABLE_MEMORY_PROFILING
    enable_memory_profiling(true);
    // Disable memory leak reporting by default (only show on failures)
    set_memory_leak_reporting_enabled(false);
    set_memory_verbose_mode(false);
#ifdef DEBUG
    enable_zombie_mode();
#endif
#endif
    
    // Parse command line arguments
    bool show_memory_summary = false;
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_new_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[1], "--list") == 0) {
            test_registry_list_all();
            return 0;
        } else if (strcmp(argv[1], "--quiet") == 0) {
            // Reduce memory leak spam for cleaner output
            set_memory_leak_reporting_enabled(false);
            run_tests_by_registry();
        } else if (strcmp(argv[1], "--memory-summary") == 0) {
            // Enable memory profiler summary
            show_memory_summary = true;
#ifdef ENABLE_MEMORY_PROFILING
            set_memory_verbose_mode(false);
            set_memory_leak_reporting_enabled(true);
#endif
            run_tests_by_registry();
        } else if (strcmp(argv[1], "--test") == 0) {
            if (argc < 3) {
                return 1;
            }
            run_specific_test(argv[2]);
        } else {
            // Legacy suite-based interface for backward compatibility
            // All legacy commands now run all tests via registry (tests are automatically registered)
            run_tests_by_registry();
        }
    } else {
        // Run all tests by default using new registry system
        run_tests_by_registry();
    }
    
    // Memory summary if requested
#ifdef ENABLE_MEMORY_PROFILING
    if (show_memory_summary) {
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("MEMORY PROFILER SUMMARY\n");
        printf("═══════════════════════════════════════════════════════════════\n");
        memory_profiler_print_stats("All Tests Complete");
        memory_profiler_check_leaks("All Tests Complete");
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("Total allocations: %zu\n", g_memory_stats.total_allocations);
    } else {
        // Memory leak summary only if there are leaks (JUnit-style: minimal output)
        if (g_memory_stats.memory_leaks > 0) {
            memory_profiler_check_leaks("All Tests Complete");
        }
    }
#endif
    
    // Free global test evalState at the end (no memory leaks)
    evalstate_free(g_test_eval_state);
    g_test_eval_state = NULL;
    
    // Unity will print its own summary (Tests X Failures Y Ignored Z)
    return UNITY_END();
}

// ============================================================================
// EMBEDDED ARRAY TESTS
// ============================================================================

TEST(test_embedded_array_single_malloc) {
    
    WITH_AUTORELEASE_POOL({
        // Create map with embedded array
        CljMap *map = make_map(4);
        
        // Verify embedded array is accessible
        TEST_ASSERT_NOT_NULL(map->data);
        TEST_ASSERT_EQUAL(4, map->capacity);
        TEST_ASSERT_EQUAL(0, map->count);
        
        // Add entries to test embedded array
        map = map_assoc(map, fixnum(1), fixnum(10));
        map = map_assoc(map, fixnum(2), fixnum(20));
        
        // Verify entries in embedded array
        CljValue val1 = map_get((CljMap*)map, fixnum(1), NULL);
        CljValue val2 = map_get((CljMap*)map, fixnum(2), NULL);
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
    });
}

TEST(test_embedded_array_memory_efficiency) {
    
    WITH_AUTORELEASE_POOL({
        // Create multiple maps to test memory efficiency
        CljMap *map1 = make_map(2);
        CljMap *map2 = make_map(4);
        CljMap *map3 = make_map(8);
        
        // Add entries to each map
        map1 = map_assoc(map1, fixnum(1), fixnum(10));
        map2 = map_assoc(map2, fixnum(2), fixnum(20));
        map3 = map_assoc(map3, fixnum(3), fixnum(30));
        
        // Verify all maps work independently
        TEST_ASSERT_NOT_NULL(map_get((CljMap*)map1, fixnum(1), NULL));
        TEST_ASSERT_NOT_NULL(map_get((CljMap*)map2, fixnum(2), NULL));
        TEST_ASSERT_NOT_NULL(map_get((CljMap*)map3, fixnum(3), NULL));
        
        // Verify embedded arrays are separate
        TEST_ASSERT_NOT_EQUAL(map1->data, map2->data);
        TEST_ASSERT_NOT_EQUAL(map2->data, map3->data);
        TEST_ASSERT_NOT_EQUAL(map1->data, map3->data);
        
    });
}

TEST(test_embedded_array_cow) {
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = make_map(4);
        map = map_assoc(map, fixnum(1), fixnum(10));
        
        // Simulate sharing (RC=2)
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation should create new map with embedded array
        CljMap *new_map = map_assoc(map, fixnum(2), fixnum(20));
        
        // Verify new map has embedded array
        TEST_ASSERT_NOT_NULL(new_map->data);
        TEST_ASSERT_EQUAL(4, new_map->capacity);
        TEST_ASSERT_EQUAL(2, new_map->count);
        
        // Verify entries in new map
        CljValue val1 = map_get(new_map, fixnum(1), NULL);
        CljValue val2 = map_get(new_map, fixnum(2), NULL);
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        // Verify original unchanged
        TEST_ASSERT_EQUAL(1, map->count);
        TEST_ASSERT_NULL(map_get((CljMap*)map, fixnum(2), NULL));
        
        
        RELEASE(map);  // Cleanup
    });
}

TEST(test_embedded_array_capacity_growth) {
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = make_map(2);  // Small capacity
        
        // Fill initial capacity
        map = map_assoc(map, fixnum(1), fixnum(10));
        map = map_assoc(map, fixnum(2), fixnum(20));
        
        // Simulate sharing to trigger COW with growth
        RETAIN(map);
        
        // Add more entries - should trigger COW with capacity growth
        CljMap *new_map = map_assoc(map, fixnum(3), fixnum(30));
        
        // Verify new map has larger capacity
        TEST_ASSERT_TRUE(new_map->capacity > map->capacity);
        
        // Verify all entries exist in new map
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(1), NULL));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(2), NULL));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(3), NULL));
        
        
        RELEASE(map);  // Cleanup
    });
}

TEST(test_embedded_array_performance) {
    
    WITH_AUTORELEASE_POOL({
        CljMap *env = make_map(4);
        
        // Simulate loop pattern with embedded arrays
        for (int i = 0; i < 50; i++) {
            env = (CljMap*)AUTORELEASE(map_assoc(env, fixnum(i), fixnum(i * 10)));
            
            // RC should stay 1 (in-place optimization)
            TEST_ASSERT_EQUAL(1, env->base.rc);
            
            // Performance check every 10 iterations
            if (i % 10 == 0) {
                // RC should stay 1 (in-place optimization)
                TEST_ASSERT_EQUAL(1, env->base.rc);
            }
        }
        
        // Verify final state
        TEST_ASSERT_EQUAL(50, env->count);
        CljValue val25 = map_get((CljMap*)env, fixnum(25), NULL);
        TEST_ASSERT_NOT_NULL(val25);
        TEST_ASSERT_EQUAL_INT(250, as_fixnum(val25));
        
    });
}
