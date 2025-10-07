/*
 * Fast Seq Benchmark
 * 
 * Compares old heap-allocated seq vs new stack-allocated fast_seq
 */

#include "minunit.h"
#include "seq.h"
#include "seq_fast.h"
#include "vector.h"
#include "CljObject.h"
#include "clj_symbols.h"
#include "memory_hooks.h"
#include <stdio.h>
#include <time.h>

// ============================================================================
// BENCHMARK HELPERS
// ============================================================================

#define BENCHMARK_ITERATIONS 100000
#define VECTOR_SIZE 1000

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static CljObject* create_test_vector(int size) {
    CljObject *vec = make_vector(size, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        for (int i = 0; i < size; i++) {
            vec_data->data[i] = make_int(i);
        }
        vec_data->count = size;
    }
    return vec;
}

// ============================================================================
// OLD SEQ BENCHMARKS (Heap-Allocated)
// ============================================================================

static char *benchmark_old_seq_iteration(void) {
    printf("\n=== OLD Seq (Heap-Allocated) ===\n");
    
    CljObject *vec = create_test_vector(VECTOR_SIZE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    volatile long long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljObject *s = seq_create(vec);
        while (s && !seq_empty(s)) {
            CljObject *item = seq_first(s);
            if (item && item->type == CLJ_INT) {
                sum += item->as.i;
            }
            CljObject *next = seq_rest(s);
            // seq_rest creates NEW object each time - huge overhead!
            s = next;
        }
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS);
    printf("  Sum (check): %lld\n", sum);
    printf("  Overhead: HEAP ALLOCATION per seq_rest()\n");
    
    return NULL;
}

// ============================================================================
// NEW FAST SEQ BENCHMARKS (Stack-Allocated)
// ============================================================================

static char *benchmark_fast_seq_iteration(void) {
    printf("\n=== NEW Fast Seq (Stack-Allocated) ===\n");
    
    CljObject *vec = create_test_vector(VECTOR_SIZE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    volatile long long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        FastSeqIterator seq;
        if (fast_seq_init(&seq, vec)) {
            while (!fast_seq_empty(&seq)) {
                CljObject *item = fast_seq_first(&seq);
                if (item && item->type == CLJ_INT) {
                    sum += item->as.i;
                }
                fast_seq_next(&seq);
            }
        }
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS);
    printf("  Sum (check): %lld\n", sum);
    printf("  Overhead: ZERO HEAP ALLOCATION!\n");
    
    return NULL;
}

// ============================================================================
// FAST SEQ WITH MACRO (Most Ergonomic)
// ============================================================================

static char *benchmark_fast_seq_macro(void) {
    printf("\n=== NEW Fast Seq (With Macro) ===\n");
    
    CljObject *vec = create_test_vector(VECTOR_SIZE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    volatile long long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        FAST_SEQ_FOREACH(vec, item) {
            if (item && item->type == CLJ_INT) {
                sum += item->as.i;
            }
        }
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS);
    printf("  Sum (check): %lld\n", sum);
    printf("  Overhead: ZERO HEAP + Clean Syntax!\n");
    
    return NULL;
}

// ============================================================================
// DIRECT ITERATION (Baseline)
// ============================================================================

static char *benchmark_direct_iteration(void) {
    printf("\n=== BASELINE Direct Vector Access ===\n");
    
    CljObject *vec = create_test_vector(VECTOR_SIZE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    volatile long long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CljPersistentVector *vec_data = as_vector(vec);
        for (int i = 0; i < vec_data->count; i++) {
            CljObject *item = vec_data->data[i];
            if (item && item->type == CLJ_INT) {
                sum += item->as.i;
            }
        }
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS);
    printf("  Sum (check): %lld\n", sum);
    
    return NULL;
}

// ============================================================================
// TEST SUITE
// ============================================================================

static char *all_seq_benchmarks(void) {
    mu_run_test(benchmark_direct_iteration);
    mu_run_test(benchmark_old_seq_iteration);
    mu_run_test(benchmark_fast_seq_iteration);
    mu_run_test(benchmark_fast_seq_macro);
    return NULL;
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    printf("\n🚀 === Fast Seq Optimization Benchmark ===\n");
    printf("Vector size: %d elements\n", VECTOR_SIZE);
    printf("Iterations: %d\n\n", BENCHMARK_ITERATIONS);
    
    init_special_symbols();
    cljvalue_pool_push();
    
    int result = run_minunit_tests(all_seq_benchmarks, "Seq Performance Comparison");
    
    cljvalue_pool_pop();
    
    printf("\n✅ Benchmark completed\n");
    printf("   Tests run: %d\n", tests_run);
    
    return result;
}

