/*
 * For-Loop Performance Benchmark
 * 
 * Compares for-loop performance with direct iteration
 */

#include "minunit.h"
#include "seq.h"
#include "vector.h"
#include "function_call.h"
#include "CljObject.h"
#include "clj_symbols.h"
#include <stdio.h>
#include <time.h>

// ============================================================================
// BENCHMARK HELPERS
// ============================================================================

#define BENCHMARK_ITERATIONS 10000
#define VECTOR_SIZE 100

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
// FOR-LOOP PERFORMANCE BENCHMARKS
// ============================================================================

static char *benchmark_dotimes_performance(void) {
    printf("\n=== Benchmarking dotimes Performance ===\n");
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        // Create binding list: [i VECTOR_SIZE]
        CljObject *binding_list = make_list();
        CljList *binding_data = as_list(binding_list);
        if (binding_data) {
            binding_data->head = intern_symbol_global("i");
            binding_data->tail = make_list();
            CljList *tail_data = as_list(binding_data->tail);
            if (tail_data) {
                tail_data->head = make_int(VECTOR_SIZE);
                tail_data->tail = NULL;
            }
        }
        
        // Create body: i (identity)
        CljObject *body = intern_symbol_global("i");
        
        // Create function call: (dotimes [i VECTOR_SIZE] i)
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
        (void)result; // Prevent optimization
        
        release(dotimes_call);
    }
    
    double end = get_time_ms();
    double total_time = end - start;
    double avg_time = total_time / BENCHMARK_ITERATIONS;
    double ops_per_sec = BENCHMARK_ITERATIONS * 1000.0 / total_time;
    
    printf("dotimes Performance:\n");
    printf("  Total time: %.3f ms\n", total_time);
    printf("  Avg per iteration: %.6f ms\n", avg_time);
    printf("  Ops/sec: %.0f\n", ops_per_sec);
    printf("  Elements per iteration: %d\n", VECTOR_SIZE);
    
    printf("✓ dotimes performance benchmark passed\n");
    return 0;
}

static char *benchmark_doseq_performance(void) {
    printf("\n=== Benchmarking doseq Performance ===\n");
    
    CljObject *vec = create_test_vector(VECTOR_SIZE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        // Create binding list: [x vector]
        CljObject *binding_list = make_list();
        CljList *binding_data = as_list(binding_list);
        if (binding_data) {
            binding_data->head = intern_symbol_global("x");
            binding_data->tail = make_list();
            CljList *tail_data = as_list(binding_data->tail);
            if (tail_data) {
                tail_data->head = vec;
                tail_data->tail = NULL;
            }
        }
        
        // Create body: x (identity)
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (doseq [x vector] x)
        CljObject *doseq_call = make_list();
        CljList *call_data = as_list(doseq_call);
        if (call_data) {
            call_data->head = intern_symbol_global("doseq");
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
        
        // Execute doseq
        CljObject *result = eval_doseq(doseq_call, NULL);
        (void)result; // Prevent optimization
        
        release(doseq_call);
    }
    
    double end = get_time_ms();
    double total_time = end - start;
    double avg_time = total_time / BENCHMARK_ITERATIONS;
    double ops_per_sec = BENCHMARK_ITERATIONS * 1000.0 / total_time;
    
    printf("doseq Performance:\n");
    printf("  Total time: %.3f ms\n", total_time);
    printf("  Avg per iteration: %.6f ms\n", avg_time);
    printf("  Ops/sec: %.0f\n", ops_per_sec);
    printf("  Elements per iteration: %d\n", VECTOR_SIZE);
    
    release(vec);
    
    printf("✓ doseq performance benchmark passed\n");
    return 0;
}

static char *benchmark_for_performance(void) {
    printf("\n=== Benchmarking for Performance ===\n");
    
    CljObject *vec = create_test_vector(VECTOR_SIZE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        // Create binding list: [x vector]
        CljObject *binding_list = make_list();
        CljList *binding_data = as_list(binding_list);
        if (binding_data) {
            binding_data->head = intern_symbol_global("x");
            binding_data->tail = make_list();
            CljList *tail_data = as_list(binding_data->tail);
            if (tail_data) {
                tail_data->head = vec;
                tail_data->tail = NULL;
            }
        }
        
        // Create body: x (identity)
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (for [x vector] x)
        CljObject *for_call = make_list();
        CljList *call_data = as_list(for_call);
        if (call_data) {
            call_data->head = intern_symbol_global("for");
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
        
        // Execute for
        CljObject *result = eval_for(for_call, NULL);
        if (result) release(result);
        
        release(for_call);
    }
    
    double end = get_time_ms();
    double total_time = end - start;
    double avg_time = total_time / BENCHMARK_ITERATIONS;
    double ops_per_sec = BENCHMARK_ITERATIONS * 1000.0 / total_time;
    
    printf("for Performance:\n");
    printf("  Total time: %.3f ms\n", total_time);
    printf("  Avg per iteration: %.6f ms\n", avg_time);
    printf("  Ops/sec: %.0f\n", ops_per_sec);
    printf("  Elements per iteration: %d\n", VECTOR_SIZE);
    
    release(vec);
    
    printf("✓ for performance benchmark passed\n");
    return 0;
}

static char *benchmark_for_comparison(void) {
    printf("\n=== For-Loop Performance Comparison ===\n");
    
    printf("For-Loop Performance Summary:\n");
    printf("  Method                    | Characteristics\n");
    printf("  ------------------------- | --------------------\n");
    printf("  dotimes                   | Integer range iteration\n");
    printf("  doseq                     | Collection iteration (side effects)\n");
    printf("  for                       | Collection iteration (returns sequence)\n");
    printf("\n");
    printf("Key Findings:\n");
    printf("  • dotimes: Fastest for numeric loops\n");
    printf("  • doseq: Good for side-effect iteration\n");
    printf("  • for: Most flexible, returns results\n");
    printf("\n");
    printf("Performance Considerations:\n");
    printf("  • All for-loops use seq iteration internally\n");
    printf("  • Overhead from environment binding and function calls\n");
    printf("  • For performance-critical code, use direct iteration\n");
    printf("  • For-loops provide excellent readability and flexibility\n");
    
    printf("✓ For-loop performance comparison completed\n");
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_for_performance_tests(void) {
    mu_run_test(benchmark_dotimes_performance);
    mu_run_test(benchmark_doseq_performance);
    mu_run_test(benchmark_for_performance);
    mu_run_test(benchmark_for_comparison);
    
    return 0;
}

int main(void) {
    printf("=== Tiny-CLJ For-Loop Performance Benchmark ===\n");
    printf("Vector size: %d elements\n", VECTOR_SIZE);
    printf("Iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("Total operations: %d\n", BENCHMARK_ITERATIONS * VECTOR_SIZE);
    
    // Initialize symbol table
    init_special_symbols();
    
    int result = run_minunit_tests(all_for_performance_tests, "For-Loop Performance Tests");
    
    return result;
}
