/*
 * Transient Performance Benchmark
 * 
 * Compares performance between persistent and transient operations
 * for both vectors and maps.
 */

#include "value.h"
#include "vector.h"
#include "map.h"
#include "memory.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#define BENCHMARK_ITERATIONS 10000
#define BENCHMARK_SIZE 1000

// Benchmark persistent vector operations
void benchmark_persistent_vector() {
    printf("=== Persistent Vector Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue vec = make_vector_v(0, 0); // Start with empty vector
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue item = fixnum(i);
            vec = vector_conj(vec, item);
        }
        
        RELEASE(vec);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Persistent Vector: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    printf("Time: %.4f seconds\n", time_spent);
    printf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    printf("\n");
}

// Benchmark transient vector operations
void benchmark_transient_vector() {
    printf("=== Transient Vector Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue vec = make_vector_v(0, 0); // Start with empty vector
        CljValue tvec = transient(vec);
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue item = fixnum(i);
            clj_conj(tvec, item);
        }
        
        CljValue final_vec = persistent(tvec);
        
        RELEASE(vec);
        RELEASE(tvec);
        RELEASE(final_vec);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Transient Vector: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    printf("Time: %.4f seconds\n", time_spent);
    printf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    printf("\n");
}

// Benchmark persistent map operations
void benchmark_persistent_map() {
    printf("=== Persistent Map Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue map = (CljValue)make_map(0); // Start with empty map
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue key = make_string("key");
            CljValue value = fixnum(i);
            map_assoc(map, key, value);
        }
        
        RELEASE(map);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Persistent Map: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    printf("Time: %.4f seconds\n", time_spent);
    printf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    printf("\n");
}

// Benchmark transient map operations
void benchmark_transient_map() {
    printf("=== Transient Map Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue map = (CljValue)make_map(0); // Start with empty map
        CljValue tmap = transient_map(map);
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue key = make_string("key");
            CljValue value = fixnum(i);
            conj_map(tmap, key, value);
        }
        
        CljValue final_map = persistent_map(tmap);
        
        RELEASE(map);
        RELEASE(tmap);
        RELEASE(final_map);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Transient Map: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    printf("Time: %.4f seconds\n", time_spent);
    printf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    printf("\n");
}

// Benchmark immediate values
void benchmark_immediates() {
    printf("=== Immediate Values Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS * 10; iter++) {
        // Test fixnum immediates
        for (int i = 0; i < 100; i++) {
            CljValue val = fixnum(i);
            int extracted = as_fixnum(val);
            (void)extracted; // Prevent optimization
        }
        
        // Test special values
        CljValue nil_val = make_nil();
        CljValue true_val = make_true();
        CljValue false_val = make_false();
        (void)nil_val;
        (void)true_val;
        (void)false_val;
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Immediates: %d iterations, 100 fixnums + 3 specials each\n", BENCHMARK_ITERATIONS * 10);
    printf("Time: %.4f seconds\n", time_spent);
    printf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * 10 * 103) / time_spent);
    printf("\n");
}

int main() {
    printf("Tiny-CLJ Transient Performance Benchmark\n");
    printf("========================================\n\n");
    
    // Initialize memory profiling (debug only)
#ifdef ENABLE_MEMORY_PROFILING
    MEMORY_PROFILER_INIT();
    enable_memory_profiling(true);
#endif
    
    // Run benchmarks
    benchmark_immediates();
    benchmark_persistent_vector();
    benchmark_transient_vector();
    benchmark_persistent_map();
    benchmark_transient_map();
    
    // Print memory statistics
    memory_profiler_print_stats("Benchmark Complete");
    memory_profiler_check_leaks("Benchmark Complete");
    memory_profiler_cleanup();
    
    return 0;
}
