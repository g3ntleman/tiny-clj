/*
 * Performance & Benchmark Tests for Tiny-Clj
 * 
 * Consolidated test file for all performance-related tests including:
 * - Basic benchmarks (arithmetic, collections, parsing, memory)
 * - For-loop performance
 * - Seq iteration performance
 * - Direct vs abstraction comparisons
 */

#include "minunit.h"
#include "memory_profiler.h"
#include "memory_hooks.h"
#include "seq.h"
#include "vector.h"
#include "map.h"
#include "function_call.h"
#include "object.h"
#include "clj_symbols.h"
#include "namespace.h"
#include "clj_parser.h"
#include "reader.h"
#include "list_operations.h"
#include "tiny_clj.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// BENCHMARK CONFIGURATION
// ============================================================================

#define BENCHMARK_ITERATIONS_SMALL  10000
#define BENCHMARK_ITERATIONS_MEDIUM 100000
#define BENCHMARK_ITERATIONS_LARGE  1000000
#define VECTOR_SIZE_SMALL 100
#define VECTOR_SIZE_LARGE 1000

// ============================================================================
// BENCHMARK HELPERS
// ============================================================================

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
// BASIC OBJECT CREATION BENCHMARKS
// ============================================================================

static char *benchmark_primitive_object_creation(void) {
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_MEDIUM; i++) {
        CljObject *obj1 = make_int(i);
        CljObject *obj2 = make_string("test");
        CljObject *obj3 = clj_true();  // Singleton - no release needed
        CljObject *obj4 = clj_nil();   // Singleton - no release needed
        (void)obj3; (void)obj4;  // Mark as intentionally accessed
        
        // Release non-singleton objects to measure full memory lifecycle
        RELEASE(obj1);
        RELEASE(obj2);
        // Singletons (obj3, obj4) are not released
    }
    
    double elapsed = get_time_ms() - start;
    // 2 objects per iteration (int + string, singletons don't count)
    double ops_per_sec = (BENCHMARK_ITERATIONS_MEDIUM * 2 * 1000.0) / elapsed;
    
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_MEDIUM);
    printf("  Operations/sec (alloc+free): %.0f\n", ops_per_sec);
    
    return NULL;
}

static char *benchmark_collection_creation(void) {
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_SMALL; i++) {
        CljObject *vec = make_vector(16, 1);
        for (int j = 0; j < 16; j++) {
            CljObject *num = make_int(j);
            vector_conj(vec, num);
            RELEASE(num);  // Release the integer after adding to vector
        }
        RELEASE(vec);  // Release the vector
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_SMALL);
    printf("  Per operation (alloc+free): %.6f ms\n", elapsed / (BENCHMARK_ITERATIONS_SMALL * 17));
    
    return NULL;
}

// ============================================================================
// FOR-LOOP PERFORMANCE BENCHMARKS
// ============================================================================

static char *benchmark_dotimes_performance(void) {
    
    EvalState *st = evalstate_new();
    init_special_symbols();
    
    // Load clojure.core for dotimes macro
    load_clojure_core(st);
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < 100; iter++) {
        const char *dotimes_code = "(dotimes [i 100] (+ i 1))";
        CljObject *result = eval_string(dotimes_code, st);
        (void)result;
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms (100 iterations)\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / 100);
    
    return NULL;
}

static char *benchmark_doseq_performance(void) {
    
    EvalState *st = evalstate_new();
    init_special_symbols();
    load_clojure_core(st);
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < 100; iter++) {
        const char *doseq_code = "(doseq [x [1 2 3 4 5]] (+ x 1))";
        CljObject *result = eval_string(doseq_code, st);
        (void)result;
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms (100 iterations)\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / 100);
    
    return NULL;
}

static char *benchmark_for_performance(void) {
    
    EvalState *st = evalstate_new();
    init_special_symbols();
    load_clojure_core(st);
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < 100; iter++) {
        const char *for_code = "(for [x [1 2 3 4 5]] (* x 2))";
        CljObject *result = eval_string(for_code, st);
        (void)result;
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms (100 iterations)\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / 100);
    
    return NULL;
}

// ============================================================================
// SEQ ITERATION PERFORMANCE BENCHMARKS
// ============================================================================

static char *benchmark_direct_vector_iteration(void) {
    
    CljObject *vec = create_test_vector(VECTOR_SIZE_LARGE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    volatile long long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS_MEDIUM; iter++) {
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
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_MEDIUM);
    printf("  Sum (check): %lld\n", sum);
    
    return NULL;
}

static char *benchmark_seq_vector_iteration(void) {
    
    CljObject *vec = create_test_vector(VECTOR_SIZE_LARGE);
    if (!vec) return "Failed to create test vector";
    
    double start = get_time_ms();
    
    volatile long long sum = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS_MEDIUM; iter++) {
        CljObject *s = seq_create(vec);
        while (s && !seq_empty(s)) {
            CljObject *item = seq_first(s);
            if (item && item->type == CLJ_INT) {
                sum += item->as.i;
            }
            s = seq_rest(s);
        }
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per iteration: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_MEDIUM);
    printf("  Sum (check): %lld\n", sum);
    
    return NULL;
}

// ============================================================================
// PARSING PERFORMANCE BENCHMARKS
// ============================================================================

static char *benchmark_parsing_integers(void) {
    
    EvalState st;
    memset(&st, 0, sizeof(EvalState));
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_MEDIUM; i++) {
        CljObject *obj = parse("42", &st);
        (void)obj;
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per parse: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_MEDIUM);
    
    return NULL;
}

static char *benchmark_parsing_expressions(void) {
    
    EvalState st;
    memset(&st, 0, sizeof(EvalState));
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_SMALL; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(+ %d %d)", i % 100, (i + 1) % 100);
        CljObject *expr = parse(buf, &st);
        (void)expr;
    }
    
    double elapsed = get_time_ms() - start;
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per parse: %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_SMALL);
    
    return NULL;
}

// ============================================================================
// SYMBOL RESOLUTION / ENVIRONMENT LOOKUP BENCHMARKS
// ============================================================================

static char *benchmark_symbol_lookup_local(void) {
    
    EvalState *st = evalstate_new();
    init_special_symbols();
    
    // Define 10 variables in current namespace
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "var%d", i);
        CljObject *sym = intern_symbol(NULL, name);
        CljObject *val = make_int(i * 10);
        ns_define(st, sym, val);
    }
    
    // Benchmark lookup of last defined variable (worst case in map)
    CljObject *lookup_sym = intern_symbol(NULL, "var9");
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_LARGE; i++) {
        CljObject *result = ns_resolve(st, lookup_sym);
        (void)result;
    }
    
    double elapsed = get_time_ms() - start;
    double lookups_per_sec = (BENCHMARK_ITERATIONS_LARGE * 1000.0) / elapsed;
    
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per lookup: %.6f µs\n", (elapsed * 1000) / BENCHMARK_ITERATIONS_LARGE);
    printf("  Lookups/sec: %.0f\n", lookups_per_sec);
    printf("  Namespace size: 10 symbols\n");
    
    return NULL;
}

static char *benchmark_symbol_lookup_with_fallback(void) {
    
    EvalState *st = evalstate_new();
    init_special_symbols();
    load_clojure_core(st);
    
    // Define local variables
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "local%d", i);
        CljObject *sym = intern_symbol(NULL, name);
        CljObject *val = make_int(i);
        ns_define(st, sym, val);
    }
    
    // Benchmark lookup of non-existent symbol (forces fallback to clojure.core)
    CljObject *not_found_sym = intern_symbol(NULL, "not-found-symbol");
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_MEDIUM; i++) {
        CljObject *result = ns_resolve(st, not_found_sym);
        (void)result;  // Will be NULL
    }
    
    double elapsed = get_time_ms() - start;
    double lookups_per_sec = (BENCHMARK_ITERATIONS_MEDIUM * 1000.0) / elapsed;
    
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per lookup: %.6f µs\n", (elapsed * 1000) / BENCHMARK_ITERATIONS_MEDIUM);
    printf("  Lookups/sec: %.0f\n", lookups_per_sec);
    printf("  Namespaces searched: 2 (user + clojure.core)\n");
    
    return NULL;
}

static char *benchmark_map_get_performance(void) {
    
    // Create map with 50 entries
    CljObject *map = make_map(64);
    CljObject *keys[50];
    
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "key%d", i);
        keys[i] = intern_symbol(NULL, name);
        CljObject *val = make_int(i);
        map_assoc(map, keys[i], val);
    }
    
    // Benchmark lookup of middle key
    CljObject *lookup_key = keys[25];
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_LARGE; i++) {
        CljObject *result = map_get(map, lookup_key);
        (void)result;
    }
    
    double elapsed = get_time_ms() - start;
    double lookups_per_sec = (BENCHMARK_ITERATIONS_LARGE * 1000.0) / elapsed;
    
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per lookup: %.6f µs\n", (elapsed * 1000) / BENCHMARK_ITERATIONS_LARGE);
    printf("  Lookups/sec: %.0f\n", lookups_per_sec);
    printf("  Map size: 50 entries\n");
    
    return NULL;
}

// ============================================================================
// MEMORY OPERATIONS BENCHMARKS
// ============================================================================

static char *benchmark_memory_allocation(void) {
    
    double start = get_time_ms();
    
    for (int i = 0; i < BENCHMARK_ITERATIONS_MEDIUM; i++) {
        CljObject *s = make_string("test string");
        RELEASE(s);
    }
    
    double elapsed = get_time_ms() - start;
    double ops_per_sec = (BENCHMARK_ITERATIONS_MEDIUM * 1000.0) / elapsed;
    
    printf("  Total time: %.3f ms\n", elapsed);
    printf("  Per cycle (alloc+free): %.6f ms\n", elapsed / BENCHMARK_ITERATIONS_MEDIUM);
    printf("  Cycles/sec: %.0f\n", ops_per_sec);
    
    return NULL;
}

// ============================================================================
// FOR-LOOPS VS DIRECT ITERATION COMPARISON
// ============================================================================

static char *benchmark_clojure_doseq_vs_direct(void) {
    
    EvalState *st = evalstate_new();
    init_special_symbols();
    load_clojure_core(st);
    
    // Create test vector
    const char* vec_str = R"(["A" "B" "C" "D" "E" "F" "G" "H" "I" "J"])";
    CljObject *vec = parse(vec_str, st);
    
    // Benchmark doseq
    double start_doseq = get_time_ms();
    for (int i = 0; i < 1000; i++) {
        const char *code = "(doseq [x [\"A\" \"B\" \"C\" \"D\" \"E\" \"F\" \"G\" \"H\" \"I\" \"J\"]] x)";
        eval_string(code, st);
    }
    double elapsed_doseq = get_time_ms() - start_doseq;
    
    // Benchmark direct iteration
    double start_direct = get_time_ms();
    for (int i = 0; i < 1000; i++) {
        CljPersistentVector *v = as_vector(vec);
        for (int j = 0; j < v->count; j++) {
            CljObject *item = v->data[j];
            (void)item;
        }
    }
    double elapsed_direct = get_time_ms() - start_direct;
    
    printf("  doseq:  %.3f ms (%.6f ms/iter)\n", elapsed_doseq, elapsed_doseq / 1000);
    printf("  direct: %.3f ms (%.6f ms/iter)\n", elapsed_direct, elapsed_direct / 1000);
    printf("  Overhead: %.1fx\n", elapsed_doseq / elapsed_direct);
    
    return NULL;
}

// ============================================================================
// TEST SUITE RUNNERS
// ============================================================================

static char *all_object_benchmarks(void) {
    mu_run_test(benchmark_primitive_object_creation);
    mu_run_test(benchmark_collection_creation);
    return NULL;
}

static char *all_loop_benchmarks(void) {
    mu_run_test(benchmark_dotimes_performance);
    mu_run_test(benchmark_doseq_performance);
    mu_run_test(benchmark_for_performance);
    mu_run_test(benchmark_clojure_doseq_vs_direct);
    return NULL;
}

static char *all_seq_benchmarks(void) {
    mu_run_test(benchmark_direct_vector_iteration);
    mu_run_test(benchmark_seq_vector_iteration);
    return NULL;
}

static char *all_parsing_benchmarks(void) {
    mu_run_test(benchmark_parsing_integers);
    mu_run_test(benchmark_parsing_expressions);
    return NULL;
}

static char *all_memory_benchmarks(void) {
    mu_run_test(benchmark_memory_allocation);
    return NULL;
}

static char *all_lookup_benchmarks(void) {
    mu_run_test(benchmark_symbol_lookup_local);
    mu_run_test(benchmark_symbol_lookup_with_fallback);
    mu_run_test(benchmark_map_get_performance);
    return NULL;
}

static char *all_performance_tests(void) {
    mu_run_test(all_object_benchmarks);
    mu_run_test(all_loop_benchmarks);
    mu_run_test(all_seq_benchmarks);
    mu_run_test(all_parsing_benchmarks);
    mu_run_test(all_memory_benchmarks);
    mu_run_test(all_lookup_benchmarks);
    return NULL;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(void) {
    printf("\n🚀 === Performance & Benchmark Tests ===\n");
    
    init_special_symbols();
    
    // Create global autorelease pool for all benchmarks
    cljvalue_pool_push();
    
    int result = run_minunit_tests(all_performance_tests, "Performance & Benchmark Tests");
    
    // Cleanup autorelease pool
    cljvalue_pool_pop();
    
    printf("\n✅ Performance benchmarks completed\n");
    printf("   Tests run: %d\n", tests_run);
    
    return result;
}

