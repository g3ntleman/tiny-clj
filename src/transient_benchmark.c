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
#include "mini_format.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#define BENCHMARK_ITERATIONS 10000
#define BENCHMARK_SIZE 1000

static void bench_outf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)clj_mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
}

// Benchmark persistent vector operations
void benchmark_persistent_vector() {
    bench_outf("=== Persistent Vector Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue vec = make_vector_v(0, 0); // Start with empty vector
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue item = fixnum(i);
            vec = (CljValue)vector_conj((CljVector*)vec, item);
        }
        
        RELEASE(vec);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    bench_outf("Persistent Vector: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    bench_outf("Time: %.4f seconds\n", time_spent);
    bench_outf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    bench_outf("\n");
}

// Benchmark transient vector operations
void benchmark_transient_vector() {
    bench_outf("=== Transient Vector Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue vec = make_vector_v(0, 0); // Start with empty vector
        CljValue tvec = vector_transient((CljVector*)vec);
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue item = fixnum(i);
            clj_conj((CljVector*)tvec, item);
        }
        
        CljValue final_vec = persistent(tvec);
        
        RELEASE(vec);
        RELEASE(tvec);
        RELEASE(final_vec);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    bench_outf("Transient Vector: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    bench_outf("Time: %.4f seconds\n", time_spent);
    bench_outf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    bench_outf("\n");
}

// Benchmark persistent map operations
void benchmark_persistent_map() {
    bench_outf("=== Persistent Map Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljValue map = (CljValue)make_map(0); // Start with empty map
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            CljValue key = make_string("key");
            CljValue value = fixnum(i);
            (void)map_assoc(map, key, value);
        }
        
        RELEASE(map);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    bench_outf("Persistent Map: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    bench_outf("Time: %.4f seconds\n", time_spent);
    bench_outf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    bench_outf("\n");
}

// Benchmark transient map operations
void benchmark_transient_map() {
    bench_outf("=== Transient Map Benchmark ===\n");
    
    clock_t start = clock();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljMap *map = (CljValue)make_map(0); // Start with empty map
        CljMap *tmap = map_transient((CljMap*)map);
        
        for (int i = 0; i < BENCHMARK_SIZE; i++) {
            ID key = make_string("key");
            ID value = fixnum(i);
            map_conj(tmap, key, value);
        }
        
        CljMap * final_map = map_persistent(tmap);
        
        RELEASE(map);
        RELEASE(tmap);
        RELEASE(final_map);
    }
    
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC;
    bench_outf("Transient Map: %d iterations, %d elements each\n", BENCHMARK_ITERATIONS, BENCHMARK_SIZE);
    bench_outf("Time: %.4f seconds\n", time_spent);
    bench_outf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * BENCHMARK_SIZE) / time_spent);
    bench_outf("\n");
}

// Benchmark immediate values
void benchmark_immediates() {
    bench_outf("=== Immediate Values Benchmark ===\n");
    
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
    bench_outf("Immediates: %d iterations, 100 fixnums + 3 specials each\n", BENCHMARK_ITERATIONS * 10);
    bench_outf("Time: %.4f seconds\n", time_spent);
    bench_outf("Operations per second: %.0f\n", (BENCHMARK_ITERATIONS * 10 * 103) / time_spent);
    bench_outf("\n");
}

int main() {
    bench_outf("Tiny-CLJ Transient Performance Benchmark\n");
    bench_outf("========================================\n\n");
    
    // Initialize memory profiling (debug only)
#if MEMORY_PROFILING_ENABLED
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
