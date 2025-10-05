/*
 * Simple Memory Profiling Test for Tiny-CLJ
 * 
 * Demonstrates basic memory profiling capabilities
 */

#include "minunit.h"
#include "memory_profiler.h"
#include "CljObject.h"
#include "vector.h"
#include "clj_symbols.h"
#include <stdio.h>

// ============================================================================
// SIMPLE MEMORY PROFILING TESTS
// ============================================================================

static char *test_basic_memory_tracking(void) {
    printf("\n=== Testing Basic Memory Tracking ===\n");
    
    printf("🔍 About to call MEMORY_TEST_START\n");
#ifdef DEBUG
    printf("🔍 DEBUG is defined\n");
    MEMORY_TEST_START("Basic Memory Operations");
#else
    printf("🔍 DEBUG is NOT defined - calling functions directly\n");
    memory_profiler_reset();
    printf("🔍 Memory Profiling: Basic Memory Operations\n");
#endif
    printf("🔍 MEMORY_TEST_START called\n");
    
    // Create and release objects
    CljObject *obj1 = make_int(42);
    CljObject *obj2 = make_float(3.14);
    
    mu_assert("obj1 created", obj1 != NULL);
    mu_assert("obj2 created", obj2 != NULL);
    
    // Test retain/release
    retain(obj1);
    retain(obj2);
    
    release(obj1);
    release(obj1); // Should be freed now
    release(obj2);
    release(obj2); // Should be freed now
    
    printf("🔍 About to call MEMORY_TEST_END\n");
#ifdef DEBUG
    MEMORY_TEST_END("Basic Memory Operations");
#else
    printf("🔍 DEBUG is NOT defined - calling functions directly\n");
    memory_profiler_print_stats("Basic Memory Operations");
    memory_profiler_check_leaks("Basic Memory Operations");
#endif
    printf("🔍 MEMORY_TEST_END called\n");
    
    printf("✓ Basic memory tracking test passed\n");
    return 0;
}

static char *test_vector_memory_tracking(void) {
    printf("\n=== Testing Vector Memory Tracking ===\n");
    
    MEMORY_TEST_START("Vector Memory Operations");
    
    // Create a vector
    CljObject *vec = make_vector(5, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    mu_assert("vector created", vec != NULL);
    mu_assert("vector data valid", vec_data != NULL);
    
    // Add elements
    for (int i = 0; i < 5; i++) {
        CljObject *elem = make_int(i);
        vec_data->data[i] = elem;
    }
    vec_data->count = 5;
    
    // Test element access
    CljObject *first = vec_data->data[0];
    mu_assert("first element accessible", first != NULL);
    
    release(vec);
    
    MEMORY_TEST_END("Vector Memory Operations");
    
    printf("✓ Vector memory tracking test passed\n");
    return 0;
}

static char *test_memory_efficiency_analysis(void) {
    printf("\n=== Memory Efficiency Analysis ===\n");
    
    printf("Memory Profiling Features:\n");
    printf("  ✅ Object allocation tracking\n");
    printf("  ✅ Object deallocation tracking\n");
    printf("  ✅ Reference counting operations tracking\n");
    printf("  ✅ Memory leak detection\n");
    printf("  ✅ Peak memory usage monitoring\n");
    printf("  ✅ Heap efficiency metrics\n");
    printf("\n");
    printf("Usage in Tests:\n");
    printf("  • MEMORY_TEST_START(test_name) - Begin profiling\n");
    printf("  • MEMORY_TEST_END(test_name) - End profiling and show stats\n");
    printf("  • MEMORY_PROFILER_PRINT_STATS(name) - Show current stats\n");
    printf("  • MEMORY_PROFILER_CHECK_LEAKS(location) - Check for leaks\n");
    printf("\n");
    printf("Debug vs Release:\n");
    printf("  • DEBUG builds: Full memory profiling enabled\n");
    printf("  • RELEASE builds: Zero overhead (all macros are no-ops)\n");
    
    printf("✓ Memory efficiency analysis completed\n");
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_simple_memory_tests(void) {
    mu_run_test(test_basic_memory_tracking);
    mu_run_test(test_vector_memory_tracking);
    mu_run_test(test_memory_efficiency_analysis);
    
    return 0;
}

int main(void) {
    printf("=== Tiny-CLJ Simple Memory Profiling Test ===\n");
    
    // Initialize memory profiler
    MEMORY_PROFILER_INIT();
    
    // Initialize symbol table
    init_special_symbols();
    
    int result = run_minunit_tests(all_simple_memory_tests, "Simple Memory Profiling Tests");
    
    // Cleanup memory profiler
    MEMORY_PROFILER_CLEANUP();
    
    return result;
}
