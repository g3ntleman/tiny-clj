/*
 * Map Performance Benchmark
 * 
 * Compares performance between:
 * - map (recursive) vs map-recur (tail-call optimized)
 * - Memory usage and stack consumption
 * - Execution time for large collections
 */

#include "unity.h"
#include "runtime.h"
#include "memory_profiler.h"
#include <time.h>
#include <stdio.h>

// Helper function to create test data
CljObject* create_test_vector(int size) {
    CljObject *result = NULL;
    for (int i = size; i > 0; i--) {
        CljObject *num = fixnum(i);
        result = make_list(num, result);
    }
    return result;
}

// Benchmark recursive map
void benchmark_map_recursive(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate();
        
        // Create test data
        CljObject *test_data = create_test_vector(100);
        
        // Start timing
        clock_t start = clock();
        
        // Execute map
        CljObject *result = eval_string("(map inc [1 2 3 4 5])", st);
        
        clock_t end = clock();
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
        
        printf("Recursive map time: %.6f seconds\n", time_taken);
        
        // Memory profiling
        memory_profiler_print_stats();
        
        evalstate_free(st);
    });
}

// Benchmark recur-based map
void benchmark_map_recur(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate();
        
        // Create test data
        CljObject *test_data = create_test_vector(100);
        
        // Start timing
        clock_t start = clock();
        
        // Execute map-recur
        CljObject *result = eval_string("(map-recur inc [1 2 3 4 5] ())", st);
        
        clock_t end = clock();
        double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
        
        printf("Recur-based map time: %.6f seconds\n", time_taken);
        
        // Memory profiling
        memory_profiler_print_stats();
        
        evalstate_free(st);
    });
}

// Benchmark deep recursion (stack overflow test)
void benchmark_deep_recursion(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate();
        
        printf("Testing deep recursion with 1000 levels...\n");
        
        // Test recursive factorial (should fail with stack overflow)
        CljObject *result = eval_string("(def factorial (fn [n] (if (= n 0) 1 (* n (factorial (- n 1))))))", st);
        result = eval_string("(factorial 1000)", st);
        
        if (result) {
            printf("Recursive factorial succeeded (unexpected)\n");
        } else {
            printf("Recursive factorial failed (expected - stack overflow)\n");
        }
        
        // Test recur-based factorial (should succeed)
        result = eval_string("(def factorial-recur (fn [n acc] (if (= n 0) acc (recur (- n 1) (* n acc)))))", st);
        result = eval_string("(factorial-recur 1000 1)", st);
        
        if (result) {
            printf("Recur-based factorial succeeded (expected)\n");
        } else {
            printf("Recur-based factorial failed (unexpected)\n");
        }
        
        evalstate_free(st);
    });
}

// Memory usage comparison
void benchmark_memory_usage(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate();
        
        printf("Memory usage comparison:\n");
        
        // Test recursive map memory
        printf("Recursive map memory usage:\n");
        memory_profiler_reset();
        CljObject *result1 = eval_string("(map inc [1 2 3 4 5 6 7 8 9 10])", st);
        memory_profiler_print_stats();
        
        // Test recur-based map memory
        printf("Recur-based map memory usage:\n");
        memory_profiler_reset();
        CljObject *result2 = eval_string("(map-recur inc [1 2 3 4 5 6 7 8 9 10] ())", st);
        memory_profiler_print_stats();
        
        evalstate_free(st);
    });
}

// Main benchmark function
void run_map_benchmark(void) {
    printf("=== Map Performance Benchmark ===\n\n");
    
    printf("1. Recursive Map Performance:\n");
    benchmark_map_recursive();
    
    printf("\n2. Recur-based Map Performance:\n");
    benchmark_map_recur();
    
    printf("\n3. Deep Recursion Test:\n");
    benchmark_deep_recursion();
    
    printf("\n4. Memory Usage Comparison:\n");
    benchmark_memory_usage();
    
    printf("\n=== Benchmark Complete ===\n");
}
