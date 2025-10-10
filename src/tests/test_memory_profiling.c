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
#include "namespace.h"
#include <stdio.h>
#include <time.h>

// ============================================================================
// BENCHMARK HELPERS
// ============================================================================

#define BENCHMARK_ITERATIONS_SMALL  1000
#define BENCHMARK_ITERATIONS_MEDIUM 10000

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

// ============================================================================
// MEMORY PROFILING DEMONSTRATION TESTS
// ============================================================================

static char *test_basic_object_creation_memory(void) {
    
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
    
    return 0;
}

static char *test_vector_creation_memory(void) {
    
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
    
    return 0;
}

static char *test_seq_iteration_memory(void) {
    
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
    
    return 0;
}

static char *test_for_loop_memory(void) {
    
    MEMORY_TEST_START("For-Loop Operations");
    
    // Create a test vector
    CljObject *vec = make_vector(3, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    // Add elements
    for (int i = 0; i < 3; i++) {
        vec_data->data[i] = make_int(i + 1);
    }
    vec_data->count = 3;
    
    // Test dotimes with a simple body that doesn't require symbol resolution
    CljList *binding_list = make_list(intern_symbol_global("i"), make_list(make_int(3), NULL));
    
    CljObject *body = make_int(42); // Simple literal instead of symbol
    
    CljList *dotimes_call = make_list(intern_symbol_global("dotimes"), make_list((CljObject*)binding_list, make_list(body, NULL)));
    
    // Execute dotimes - this should work with simple literals
    CljObject *result = eval_dotimes(dotimes_call, NULL);
    mu_assert("dotimes executed", result == NULL || result->type == CLJ_NIL);
    
    release(dotimes_call);
    release(vec);
    
    MEMORY_TEST_END("For-Loop Operations");
    
    return 0;
}

static char *test_map_creation_memory(void) {
    
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
    
    return 0;
}

static char *test_memory_benchmark_large_maps(void) {
    printf("\n=== Memory Benchmark: Large Maps ===\n");
    
    MEMORY_TEST_BENCHMARK_START("Large Map Creation");
    
    // Create maps with many key-value pairs
    for (int i = 0; i < 10; i++) {
        CljObject *map = make_map(50);
        
        // Fill with 50 key-value pairs
        for (int j = 0; j < 50; j++) {
            char key_buf[32];
            snprintf(key_buf, sizeof(key_buf), "key-%d", j);
            CljObject *key = make_string(key_buf);
            CljObject *val = make_int(j * 10);
            
            map_assoc(map, key, val);
            
            release(key);
            release(val);
        }
        
        // Verify map size
        int count = map_count(map);
        mu_assert("map has correct count", count == 50);
        
        release(map);
    }
    
    MEMORY_TEST_BENCHMARK_END("Large Map Creation");
    
    return 0;
}

// ============================================================================
// PERFORMANCE + MEMORY BENCHMARKS
// ============================================================================

static char *benchmark_vector_iteration_with_memory(void) {
    
    MEMORY_TEST_START("Vector Iteration Performance");
    
    // Create test vector
    CljObject *vec = make_vector(100, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    for (int i = 0; i < 100; i++) {
        vec_data->data[i] = make_int(i);
    }
    vec_data->count = 100;
    
    // Direct iteration benchmark
    double start = get_time_ms();
    long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS_SMALL; iter++) {
        for (int i = 0; i < 100; i++) {
            CljObject *obj = vec_data->data[i];
            if (obj && obj->type == CLJ_INT) {
                sum += obj->as.i;
            }
        }
    }
    double direct_time = get_time_ms() - start;
    
    // Seq iteration benchmark
    start = get_time_ms();
    sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS_SMALL; iter++) {
        SeqIterator it;
        seq_iter_init(&it, vec);
        while (!seq_iter_empty(&it)) {
            CljObject *obj = seq_iter_first(&it);
            if (obj && obj->type == CLJ_INT) {
                sum += obj->as.i;
            }
            seq_iter_next(&it);
        }
    }
    double seq_time = get_time_ms() - start;
    
    release(vec);
    
    printf("  Direct iteration: %.2f ms (%.0f ops/sec)\n", 
           direct_time, (BENCHMARK_ITERATIONS_SMALL * 100) / (direct_time / 1000.0));
    printf("  Seq iteration:    %.2f ms (%.0f ops/sec)\n", 
           seq_time, (BENCHMARK_ITERATIONS_SMALL * 100) / (seq_time / 1000.0));
    printf("  Overhead: %.1fx\n", seq_time / direct_time);
    
    MEMORY_TEST_END("Vector Iteration Performance");
    
    return 0;
}

static char *benchmark_map_lookup_with_memory(void) {
    
    MEMORY_TEST_START("Map Lookup Performance");
    
    // Create test map
    CljObject *map = make_map(100);
    CljObject *keys[10];
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key-%d", i);
        keys[i] = make_string(buf);
        CljObject *val = make_int(i * 10);
        map_assoc(map, keys[i], val);
        release(val);
    }
    
    // Benchmark map lookups
    double start = get_time_ms();
    long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS_MEDIUM; iter++) {
        for (int i = 0; i < 10; i++) {
            CljObject *result = map_get(map, keys[i]);
            if (result && result->type == CLJ_INT) {
                sum += result->as.i;
            }
        }
    }
    double elapsed = get_time_ms() - start;
    
    double lookups_per_sec = (BENCHMARK_ITERATIONS_MEDIUM * 10) / (elapsed / 1000.0);
    printf("  Map lookups: %.2f ms (%.0f lookups/sec)\n", elapsed, lookups_per_sec);
    
    // Cleanup
    for (int i = 0; i < 10; i++) {
        release(keys[i]);
    }
    release(map);
    
    MEMORY_TEST_END("Map Lookup Performance");
    
    return 0;
}

static char *benchmark_symbol_lookup_with_memory(void) {
    
    MEMORY_TEST_START("Symbol Lookup Performance");
    
    // Create test environment
    EvalState *st = evalstate_new();
    init_special_symbols();
    
    // Define test variables
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "bench-%d", i);
        CljObject *sym = intern_symbol(NULL, name);
        CljObject *val = make_int(i * 10);
        ns_define(st, sym, val);
    }
    
    // Lookup symbol for benchmarking
    CljObject *lookup_sym = intern_symbol(NULL, "bench-5");
    
    // Benchmark symbol lookups
    double start = get_time_ms();
    long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS_MEDIUM; iter++) {
        CljObject *result = ns_resolve(st, lookup_sym);
        if (result && result->type == CLJ_INT) {
            sum += result->as.i;
        }
    }
    double elapsed = get_time_ms() - start;
    
    double lookups_per_sec = BENCHMARK_ITERATIONS_MEDIUM / (elapsed / 1000.0);
    printf("  Symbol lookups: %.2f ms (%.0f lookups/sec)\n", elapsed, lookups_per_sec);
    
    MEMORY_TEST_END("Symbol Lookup Performance");
    
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_memory_profiling_tests(void) {
    // Memory profiling tests
    mu_run_test(test_basic_object_creation_memory);
    mu_run_test(test_vector_creation_memory);
    mu_run_test(test_map_creation_memory);
    mu_run_test(test_seq_iteration_memory);
    mu_run_test(test_for_loop_memory);
    mu_run_test(test_memory_comparison_analysis);
    
    // Memory benchmarks
    mu_run_test(test_memory_benchmark_small_objects);
    mu_run_test(test_memory_benchmark_large_vectors);
    mu_run_test(test_memory_benchmark_large_maps);
    
    // Performance + Memory benchmarks
    mu_run_test(benchmark_vector_iteration_with_memory);
    mu_run_test(benchmark_map_lookup_with_memory);
    mu_run_test(benchmark_symbol_lookup_with_memory);
    
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
