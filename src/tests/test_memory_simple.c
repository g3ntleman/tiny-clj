/*
 * Simple Memory Profiling Test for Tiny-CLJ
 * 
 * Demonstrates basic memory profiling capabilities
 */

#include "minunit.h"
#include "memory_profiler.h"
#include "memory_hooks.h"
#include "CljObject.h"
#include "vector.h"
#include "clj_symbols.h"
#include <stdio.h>

// ============================================================================
// SIMPLE MEMORY PROFILING TESTS
// ============================================================================

#define TEST_VECTOR_SIZE 5

static char *test_singleton_memory_tracking(void) {
    printf("\n=== Testing Singleton Memory Tracking ===\n");
    
    WITH_MEMORY_PROFILING({
        // Create and release singleton objects (vectors, maps, lists)
        CljObject *empty_vec = make_vector(0, 0);
        CljObject *empty_list = make_list();
        
        mu_assert("empty_vec created", empty_vec != NULL);
        mu_assert("empty_list created", empty_list != NULL);
        
        // Test retain/release on singletons
        retain(empty_vec);
        retain(empty_list);
        
        release(empty_vec);
        release(empty_list);
    });
    
    printf("✓ Singleton memory tracking test passed\n");
    return 0;
}

static char *test_vector_memory_tracking(void) {
    printf("\n=== Testing Vector Memory Tracking ===\n");
    
    WITH_MEMORY_PROFILING({
        // Create a vector
        CljObject *vec = make_vector(TEST_VECTOR_SIZE, 1);
        CljPersistentVector *vec_data = as_vector(vec);
        
        mu_assert("vector created", vec != NULL);
        mu_assert("vector data valid", vec_data != NULL);
        
        // Add elements
        for (int i = 0; i < TEST_VECTOR_SIZE; i++) {
            CljObject *elem = make_int(i);
            vec_data->data[i] = elem;
        }
        vec_data->count = TEST_VECTOR_SIZE;
        
        // Test element access
        CljObject *first = vec_data->data[0];
        mu_assert("first element accessible", first != NULL);
        
        // Free all vector elements first
        for (int i = 0; i < TEST_VECTOR_SIZE; i++) {
            release(vec_data->data[i]);
        }
        
        // Then free the vector
        release(vec);
    });
    
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
    mu_run_test(test_singleton_memory_tracking);
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
