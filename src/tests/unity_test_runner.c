/*
 * Unity Test Runner for Tiny-CLJ
 * 
 * Central test runner that includes all test suites with command-line parameter support.
 */

#include "tests_common.h"
#include "test_registry.h"
#include "memory_profiler.h"

// Access to global memory stats for leak checking
extern MemoryStats g_memory_stats;
extern bool g_memory_verbose_mode;



// ============================================================================
// GLOBAL SETUP/TEARDOWN
// ============================================================================


void setUp(void) {
    // Reset memory profiler statistics BEFORE each test
    memory_profiler_reset();
    
    runtime_init();
    
    if (!g_runtime.builtins_registered) {
        init_special_symbols();
        meta_registry_init();
            register_builtins();
            g_runtime.builtins_registered = true;
        }
        
#ifdef ENABLE_MEMORY_PROFILING
        MEMORY_PROFILER_INIT();
        enable_memory_profiling(true);
        set_memory_verbose_mode(false);
#endif
}

void tearDown(void) {
    // JUnit-style: Only print memory stats if verbose mode is enabled
    // (memory_profiler_check_leaks already handles leak reporting)
    if (g_memory_verbose_mode) {
        memory_profiler_print_stats("Test Complete");
    }
    // Check for leaks silently (only prints if leaks detected and reporting enabled)
    memory_profiler_check_leaks("Test Complete");
    
    runtime_free();
}

// ============================================================================
// EMBEDDED ARRAY TESTS (defined in this file)
// ============================================================================

// Forward declarations for embedded array tests (defined in this file)
void test_embedded_array_single_malloc(void);
void test_embedded_array_memory_efficiency(void);
void test_embedded_array_cow(void);
void test_embedded_array_capacity_growth(void);
void test_embedded_array_performance(void);

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
    printf("Unity Test Runner for Tiny-CLJ (Dynamic Registry)\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  --test <name>        Run specific test by name or pattern (supports * wildcard)\n");
    printf("  --list              List all available tests\n");
    printf("  --quiet             Run all tests with minimal memory leak output\n");
    printf("  --help, -h          Show this help\n");
    printf("  (no args)           Run all tests\n\n");
    printf("Test Names:\n");
    printf("  Tests use qualified names: <group>/<test> (without 'test_' prefix)\n");
    printf("  Examples: values/cljvalue_immediate_helpers\n");
    printf("           basics/list_count\n");
    printf("           fixed_point/fixed_creation_and_conversion\n\n");
    printf("Examples:\n");
    printf("  %s --test values/cljvalue_immediate_helpers\n", program_name);
    printf("  %s --test \"values/*\"\n", program_name);
    printf("  %s --test \"*/cljvalue_*\"\n", program_name);
    printf("  %s --test \"*cow*\"\n", program_name);
    printf("  %s --quiet\n", program_name);
    printf("  %s --list\n", program_name);
    printf("  %s\n", program_name);
}

// JUnit-style test runner: simple progress indicator (. for pass, F for fail, I for ignore)
static void run_tests_by_registry(void) {
    size_t test_count;
    Test *all_tests = test_registry_get_all(&test_count);
    
    if (test_count == 0) {
        printf("No tests registered. Make sure test files include REGISTER_TEST() macros.\n");
        return;
    }
    
    // JUnit-style: Simple progress indicator
    for (size_t i = 0; i < test_count; i++) {
        // Capture Unity test state before running
        int failures_before = Unity.TestFailures;
        int ignores_before = Unity.TestIgnores;
        
        // Wrap test in TRY/CATCH to catch unhandled exceptions
        TRY {
            RUN_TEST(all_tests[i].func);
        } CATCH(ex) {
            // Unhandled exception caught - mark test as failed
            printf("\nUNHANDLED EXCEPTION in %s: %s\n", 
                   all_tests[i].qualified_name, ex->message);
            // Mark test as failed using Unity's internal state
            Unity.TestFailures++;
            Unity.CurrentTestFailed = 1;
        } END_TRY
        
        // Check if test failed
        int failures_after = Unity.TestFailures;
        int ignores_after = Unity.TestIgnores;
        
        if (failures_after > failures_before) {
            // Test failed - print name immediately (JUnit-style)
            printf("FAILURE: %s\n", all_tests[i].qualified_name);
        } else if (ignores_after > ignores_before) {
            // Test ignored
            printf("I");
        } else {
            // Test passed
            printf(".");
        }
        
        // Flush output for progress indicator
        fflush(stdout);
    }
    
    printf("\n");
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
        
        // JUnit-style: Simple progress indicator for pattern matching
        printf("Running %d test(s)...\n", found);
        for (size_t i = 0; i < test_count; i++) {
            // Match against qualified name (group/testname format)
            if (test_name_matches_pattern(all_tests[i].qualified_name, test_name_or_pattern)) {
                // Capture Unity test state before running
                int failures_before = Unity.TestFailures;
                int ignores_before = Unity.TestIgnores;
                
                // Wrap test in TRY/CATCH to catch unhandled exceptions
                TRY {
                    RUN_TEST(all_tests[i].func);
                } CATCH(ex) {
                    // Unhandled exception caught - mark test as failed
                    printf("\nUNHANDLED EXCEPTION in %s: %s\n", 
                           all_tests[i].qualified_name, ex->message);
                    // Mark test as failed using Unity's internal state
                    Unity.TestFailures++;
                    Unity.CurrentTestFailed = 1;
                } END_TRY
                
                // Check if test failed
                int failures_after = Unity.TestFailures;
                int ignores_after = Unity.TestIgnores;
                
                if (failures_after > failures_before) {
                    // Test failed - print name immediately (JUnit-style)
                    printf("FAILURE: %s\n", all_tests[i].qualified_name);
                } else if (ignores_after > ignores_before) {
                    // Test ignored
                    printf("I");
                } else {
                    // Test passed
                    printf(".");
                }
                fflush(stdout);
            }
        }
        printf("\n");
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
            printf("Running: %s\n", test->qualified_name);
            RUN_TEST(test->func);
            // Unity will print the result
        } else {
            printf("❌ Test not found: %s\n", test_name_or_pattern);
            printf("Use --list to see available tests\n");
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Enable memory profiling for tests (only in DEBUG builds)
#ifdef ENABLE_MEMORY_PROFILING
    enable_memory_profiling(true);
#endif
    
    // Parse command line arguments
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
        } else if (strcmp(argv[1], "--test") == 0) {
            if (argc < 3) {
                printf("Error: --test requires a test name or pattern\n");
                printf("Use --list to see available tests\n");
                return 1;
            }
            run_specific_test(argv[2]);
        } else {
            // Legacy suite-based interface for backward compatibility
            // All legacy commands now run all tests via registry (tests are automatically registered)
            printf("Note: Legacy command '%s' - running all registered tests\n", argv[1]);
            printf("Use --test <pattern> to filter tests, or --list to see available tests\n");
            run_tests_by_registry();
        }
    } else {
        // Run all tests by default using new registry system
        run_tests_by_registry();
    }
    
    // Memory leak summary only if there are leaks (JUnit-style: minimal output)
#ifdef ENABLE_MEMORY_PROFILING
    if (g_memory_stats.memory_leaks > 0) {
        printf("\nMemory Leak Summary:\n");
        memory_profiler_check_leaks("All Tests Complete");
    }
#endif
    
    // Unity will print its own summary (Tests X Failures Y Ignored Z)
    return UNITY_END();
}

// ============================================================================
// EMBEDDED ARRAY TESTS
// ============================================================================

void test_embedded_array_single_malloc(void) {
    printf("\n=== Test: Single Malloc für embedded array ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Create map with embedded array
        CljMap *map = (CljMap*)make_map(4);
        printf("Map created with embedded array\n");
        
        // Verify embedded array is accessible
        TEST_ASSERT_NOT_NULL(map->data);
        TEST_ASSERT_EQUAL(4, map->capacity);
        TEST_ASSERT_EQUAL(0, map->count);
        
        // Add entries to test embedded array
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        
        // Verify entries in embedded array
        CljValue val1 = map_get((CljValue)map, fixnum(1));
        CljValue val2 = map_get((CljValue)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        printf("✓ Embedded array funktioniert korrekt\n");
    });
}

void test_embedded_array_memory_efficiency(void) {
    printf("\n=== Test: Memory Efficiency ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Create multiple maps to test memory efficiency
        CljMap *map1 = (CljMap*)make_map(2);
        CljMap *map2 = (CljMap*)make_map(4);
        CljMap *map3 = (CljMap*)make_map(8);
        
        // Add entries to each map
        map_assoc_cow((CljValue)map1, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map2, fixnum(2), fixnum(20));
        map_assoc_cow((CljValue)map3, fixnum(3), fixnum(30));
        
        // Verify all maps work independently
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map1, fixnum(1)));
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map2, fixnum(2)));
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map3, fixnum(3)));
        
        // Verify embedded arrays are separate
        TEST_ASSERT_NOT_EQUAL(map1->data, map2->data);
        TEST_ASSERT_NOT_EQUAL(map2->data, map3->data);
        TEST_ASSERT_NOT_EQUAL(map1->data, map3->data);
        
        printf("✓ Memory efficiency: Jede Map hat eigenes embedded array\n");
    });
}

void test_embedded_array_cow(void) {
    printf("\n=== Test: COW mit embedded arrays ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("Original map: RC=%d, count=%d\n", map->base.rc, map->count);
        
        // Simulate sharing (RC=2)
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation should create new map with embedded array
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        CljMap *new_map_data = as_map(new_map);
        
        // Verify new map has embedded array
        TEST_ASSERT_NOT_NULL(new_map_data->data);
        TEST_ASSERT_EQUAL(4, new_map_data->capacity);
        TEST_ASSERT_EQUAL(2, new_map_data->count);
        
        // Verify entries in new map
        CljValue val1 = map_get(new_map, fixnum(1));
        CljValue val2 = map_get(new_map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        // Verify original unchanged
        TEST_ASSERT_EQUAL(1, map->count);
        TEST_ASSERT_NULL(map_get((CljValue)map, fixnum(2)));
        
        printf("✓ COW mit embedded arrays funktioniert\n");
        
        RELEASE(map);  // Cleanup
    });
}

void test_embedded_array_capacity_growth(void) {
    printf("\n=== Test: Capacity Growth mit embedded arrays ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(2);  // Small capacity
        printf("Initial capacity: %d\n", map->capacity);
        
        // Fill initial capacity
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        printf("After filling capacity: %d\n", map->capacity);
        
        // Simulate sharing to trigger COW with growth
        RETAIN(map);
        
        // Add more entries - should trigger COW with capacity growth
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(3), fixnum(30));
        CljMap *new_map_data = as_map(new_map);
        
        // Verify new map has larger capacity
        printf("New map capacity: %d\n", new_map_data->capacity);
        TEST_ASSERT_TRUE(new_map_data->capacity > map->capacity);
        
        // Verify all entries exist in new map
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(1)));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(2)));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(3)));
        
        printf("✓ Capacity growth mit embedded arrays funktioniert\n");
        
        RELEASE(map);  // Cleanup
    });
}

void test_embedded_array_performance(void) {
    printf("\n=== Test: Performance mit embedded arrays ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *env = (CljMap*)make_map(4);
        printf("Starting performance test...\n");
        
        // Simulate loop pattern with embedded arrays
        for (int i = 0; i < 50; i++) {
            env = (CljMap*)AUTORELEASE(map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10)));
            
            // RC should stay 1 (in-place optimization)
            TEST_ASSERT_EQUAL(1, env->base.rc);
            
            if (i % 10 == 0) {
                printf("Iteration %d: RC=%d, count=%d, capacity=%d\n", 
                       i, env->base.rc, env->count, env->capacity);
            }
        }
        
        // Verify final state
        TEST_ASSERT_EQUAL(50, env->count);
        CljValue val25 = map_get((CljValue)env, fixnum(25));
        TEST_ASSERT_NOT_NULL(val25);
        TEST_ASSERT_EQUAL_INT(250, as_fixnum(val25));
        
        printf("✓ Performance test erfolgreich (50 Iterationen)\n");
    });
}
