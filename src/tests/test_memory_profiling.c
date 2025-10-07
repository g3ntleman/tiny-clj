/*
 * Memory Profiling Tests for Tiny-CLJ
 * 
 * Demonstrates memory profiling capabilities and heap analysis
 */

#include "minunit.h"
#include "memory_profiler.h"
#include "object.h"
#include "vector.h"
#include "seq.h"
#include "map.h"
#include "function_call.h"
#include "clj_symbols.h"
#include <stdio.h>

// ============================================================================
// MEMORY PROFILING DEMONSTRATION TESTS
// ============================================================================

static char *test_basic_object_creation_memory(void) {
    printf("\n=== Testing Basic Object Creation Memory Usage ===\n");
    
    MEMORY_TEST_START("Basic Object Creation");
    
    // Create some basic objects
    CljObject *int_obj = make_int(42);
    CljObject *float_obj = make_float(3.14);
    CljObject *str_obj = make_string("hello");
    
    mu_assert("int object created", int_obj != NULL);
    mu_assert("float object created", float_obj != NULL);
    mu_assert("string object created", str_obj != NULL);
    
    // Release objects
    release(int_obj);
    release(float_obj);
    release(str_obj);
    
    MEMORY_TEST_END("Basic Object Creation");
    
    printf("✓ Basic object creation memory test passed\n");
    return 0;
}

static char *test_vector_creation_memory(void) {
    printf("\n=== Testing Vector Creation Memory Usage ===\n");
    
    MEMORY_TEST_START("Vector Creation");
    
    // Create a vector with multiple elements
    CljObject *vec = make_vector(10, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    mu_assert("vector created", vec != NULL);
    mu_assert("vector data valid", vec_data != NULL);
    
    // Add elements
    for (int i = 0; i < 10; i++) {
        CljObject *elem = make_int(i);
        vec_data->data[i] = elem;
    }
    vec_data->count = 10;
    
    // Test vector operations
    CljObject *first_elem = vec_data->data[0];
    mu_assert("first element accessible", first_elem != NULL);
    
    release(vec);
    
    MEMORY_TEST_END("Vector Creation");
    
    printf("✓ Vector creation memory test passed\n");
    return 0;
}

static char *test_seq_iteration_memory(void) {
    printf("\n=== Testing Seq Iteration Memory Usage ===\n");
    
    MEMORY_TEST_START("Seq Iteration");
    
    // Create a vector for iteration
    CljObject *vec = make_vector(5, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    // Add elements
    for (int i = 0; i < 5; i++) {
        vec_data->data[i] = make_int(i * 10);
    }
    vec_data->count = 5;
    
    // Iterate using seq
    SeqIterator *seq = seq_create(vec);
    mu_assert("seq created", seq != NULL);
    
    int count = 0;
    while (!seq_empty(seq)) {
        CljObject *element = seq_first(seq);
        mu_assert("element accessible", element != NULL);
        
        SeqIterator *next = seq_next(seq);
        seq_release(seq);
        seq = next;
        count++;
    }
    
    mu_assert("all elements iterated", count == 5);
    
    release(vec);
    
    MEMORY_TEST_END("Seq Iteration");
    
    printf("✓ Seq iteration memory test passed\n");
    return 0;
}

static char *test_for_loop_memory(void) {
    printf("\n=== Testing For-Loop Memory Usage ===\n");
    
    MEMORY_TEST_START("For-Loop Operations");
    
    // Create a test vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    // Add elements
    for (int i = 0; i < 3; i++) {
        vec_data->data[i] = make_int(i + 1);
    }
    vec_data->count = 3;
    
    // Test dotimes
    CljObject *binding_list = make_list();
    CljList *binding_data = as_list(binding_list);
    if (binding_data) {
        binding_data->head = intern_symbol_global("i");
        binding_data->tail = make_list();
        CljList *tail_data = as_list(binding_data->tail);
        if (tail_data) {
            tail_data->head = make_int(3);
            tail_data->tail = NULL;
        }
    }
    
    CljObject *body = intern_symbol_global("i");
    
    CljObject *dotimes_call = make_list();
    CljList *call_data = as_list(dotimes_call);
    if (call_data) {
        call_data->head = intern_symbol_global("dotimes");
        call_data->tail = make_list();
        CljList *call_tail = as_list(call_data->tail);
        if (call_tail) {
            call_tail->head = binding_list;
            call_tail->tail = make_list();
            CljList *call_tail2 = as_list(call_tail->tail);
            if (call_tail2) {
                call_tail2->head = body;
                call_tail2->tail = NULL;
            }
        }
    }
    
    // Execute dotimes
    CljObject *result = eval_dotimes(dotimes_call, NULL);
    mu_assert("dotimes executed", result == NULL || result->type == CLJ_NIL);
    
    release(dotimes_call);
    release(vec);
    
    MEMORY_TEST_END("For-Loop Operations");
    
    printf("✓ For-loop memory test passed\n");
    return 0;
}

static char *test_map_creation_memory(void) {
    printf("\n=== Testing Map Creation and Operations Memory Usage ===\n");
    
    MEMORY_TEST_START("Map Creation");
    
    // Create a map with initial capacity
    CljObject *map = make_map(10);
    mu_assert("map created", map != NULL);
    mu_assert("map is correct type", map->type == CLJ_MAP);
    
    // Create keys and values
    CljObject *k1 = make_string("name");
    CljObject *v1 = make_string("Alice");
    CljObject *k2 = make_string("age");
    CljObject *v2 = make_int(30);
    CljObject *k3 = make_string("city");
    CljObject *v3 = make_string("Berlin");
    
    // Add multiple key-value pairs (map_assoc modifies in-place)
    map_assoc(map, k1, v1);
    mu_assert("map still valid after first assoc", map != NULL);
    
    map_assoc(map, k2, v2);
    mu_assert("map still valid after second assoc", map != NULL);
    
    map_assoc(map, k3, v3);
    mu_assert("map still valid after third assoc", map != NULL);
    
    // Test map retrieval
    CljObject *retrieved = map_get(map, k1);
    mu_assert("retrieved value from map", retrieved != NULL);
    mu_assert("retrieved correct value", retrieved == v1);
    
    // Test map size
    int count = map_count(map);
    mu_assert("map has correct count", count == 3);
    
    // Test map_contains
    mu_assert("map contains k1", map_contains(map, k1));
    mu_assert("map contains k2", map_contains(map, k2));
    
    // Release all objects
    release(map);
    release(k1);
    release(v1);
    release(k2);
    release(v2);
    release(k3);
    release(v3);
    
    MEMORY_TEST_END("Map Creation");
    
    printf("✓ Map creation memory test passed\n");
    return 0;
}

static char *test_memory_comparison_analysis(void) {
    printf("\n=== Memory Comparison Analysis ===\n");
    
    printf("Memory Usage Comparison Summary:\n");
    printf("  ┌─────────────────────────────────────────────────────────┐\n");
    printf("  │ Operation Type          │ Memory Characteristics        │\n");
    printf("  ├─────────────────────────────────────────────────────────┤\n");
    printf("  │ Basic Object Creation  │ Low overhead, predictable     │\n");
    printf("  │ Vector Operations      │ Higher overhead for storage   │\n");
    printf("  │ Map Operations         │ Hash table overhead           │\n");
    printf("  │ Seq Iteration          │ Iterator allocation overhead  │\n");
    printf("  │ For-Loop Operations    │ Environment binding overhead  │\n");
    printf("  └─────────────────────────────────────────────────────────┘\n");
    printf("\n");
    printf("Optimization Recommendations:\n");
    printf("  • Use object pooling for high-frequency allocations\n");
    printf("  • Prefer direct iteration over seq for performance-critical code\n");
    printf("  • Monitor map growth for large datasets\n");
    printf("  │ • Consider iterator reuse for repeated seq operations\n");
    printf("  • Monitor memory leaks in complex nested operations\n");
    
    printf("✓ Memory comparison analysis completed\n");
    return 0;
}

// ============================================================================
// MEMORY BENCHMARK TESTS
// ============================================================================

static char *test_memory_benchmark_small_objects(void) {
    printf("\n=== Memory Benchmark: Small Objects ===\n");
    
    MEMORY_TEST_BENCHMARK_START("Small Object Creation");
    
    // Create many small objects
    for (int i = 0; i < 1000; i++) {
        CljObject *obj = make_int(i);
        release(obj);
    }
    
    MEMORY_TEST_BENCHMARK_END("Small Object Creation");
    
    printf("✓ Small objects memory benchmark passed\n");
    return 0;
}

static char *test_memory_benchmark_large_vectors(void) {
    printf("\n=== Memory Benchmark: Large Vectors ===\n");
    
    MEMORY_TEST_BENCHMARK_START("Large Vector Creation");
    
    // Create large vectors
    for (int i = 0; i < 10; i++) {
        CljObject *vec = make_vector(100, 1);
        CljPersistentVector *vec_data = as_vector(vec);
        
        // Fill with data
        for (int j = 0; j < 100; j++) {
            vec_data->data[j] = make_int(j);
        }
        vec_data->count = 100;
        
        release(vec);
    }
    
    MEMORY_TEST_BENCHMARK_END("Large Vector Creation");
    
    printf("✓ Large vectors memory benchmark passed\n");
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_memory_profiling_tests(void) {
    mu_run_test(test_basic_object_creation_memory);
    mu_run_test(test_vector_creation_memory);
    mu_run_test(test_map_creation_memory);
    mu_run_test(test_seq_iteration_memory);
    mu_run_test(test_for_loop_memory);
    mu_run_test(test_memory_comparison_analysis);
    mu_run_test(test_memory_benchmark_small_objects);
    mu_run_test(test_memory_benchmark_large_vectors);
    
    return 0;
}

int main(void) {
    printf("=== Tiny-CLJ Memory Profiling Tests ===\n");
    
    // Initialize memory profiler
    MEMORY_PROFILER_INIT();
    
    // Initialize symbol table
    init_special_symbols();
    
    int result = run_minunit_tests(all_memory_profiling_tests, "Memory Profiling Tests");
    
    // Cleanup memory profiler
    MEMORY_PROFILER_CLEANUP();
    
    return result;
}
