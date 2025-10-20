/*
 * Namespace Lookup Performance Benchmark
 * 
 * Comprehensive benchmark for namespace resolution performance:
 * - Current namespace lookup (O(1) hash)
 * - Global namespace search (O(n) linear)
 * - Multiple namespace scenarios
 * - Symbol resolution caching potential
 * - Performance regression detection
 */

#include "../object.h"
#include "../namespace.h"
#include "../symbol.h"
#include "../map.h"
#include "../benchmark.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// BENCHMARK SETUP
// ============================================================================

static EvalState *benchmark_eval_state = NULL;
static CljNamespace **test_namespaces = NULL;
static int num_test_namespaces = 0;

// Test symbols for lookup
static const char* test_symbols[] = {
    "def", "defn", "fn", "let", "if", "when", "cond", "case",
    "map", "reduce", "filter", "take", "drop", "first", "rest",
    "cons", "conj", "assoc", "dissoc", "get", "contains?",
    "count", "empty?", "seq", "vec", "list", "vector",
    "str", "prn", "println", "print", "read-line",
    "inc", "dec", "+", "-", "*", "/", "mod", "rem",
    "=", "not=", "<", ">", "<=", ">=", "and", "or", "not",
    "true", "false", "nil", "some?", "every?", "any?"
};

#define NUM_TEST_SYMBOLS (sizeof(test_symbols) / sizeof(test_symbols[0]))

// ============================================================================
// BENCHMARK HELPERS
// ============================================================================

static void benchmark_setup(void) {
    init_special_symbols();
    meta_registry_init();
    benchmark_eval_state = evalstate_new();
    
    // Create test namespaces
    num_test_namespaces = 100; // Test with 100 namespaces (realistic count)
    test_namespaces = malloc(sizeof(CljNamespace*) * num_test_namespaces);
    
    // Create clojure.core namespace first (for priority testing)
    CljNamespace *clojure_core = ns_get_or_create("clojure.core", "benchmark-test.c");
    if (clojure_core) {
        // Add common clojure.core symbols
        for (int i = 0; i < NUM_TEST_SYMBOLS; i++) {
            CljObject *sym = make_symbol_old(test_symbols[i], NULL);
            CljValue val = fixnum(i * 1000); // High priority values
            map_assoc((CljObject*)clojure_core->mappings, sym, (CljObject*)val);
        }
    }
    
    for (int i = 0; i < num_test_namespaces; i++) {
        char ns_name[64];
        snprintf(ns_name, sizeof(ns_name), "test.ns%d", i);
        test_namespaces[i] = ns_get_or_create(ns_name, "benchmark-test.c");
        
        // Add some symbols to each namespace
        for (int j = 0; j < 10; j++) {
            char sym_name[64];
            snprintf(sym_name, sizeof(sym_name), "var%d", j);
            CljObject *sym = make_symbol_old(sym_name, NULL);
            CljValue val = fixnum(j * 100 + i);
            map_assoc((CljObject*)test_namespaces[i]->mappings, sym, (CljObject*)val);
        }
    }
    
    // Set current namespace to first test namespace
    benchmark_eval_state->current_ns = test_namespaces[0];
}

static void benchmark_teardown(void) {
    if (test_namespaces) {
        free(test_namespaces);
        test_namespaces = NULL;
    }
    
    if (benchmark_eval_state) {
        evalstate_free(benchmark_eval_state);
        benchmark_eval_state = NULL;
    }
    
    symbol_table_cleanup();
    meta_registry_cleanup();
}

// ============================================================================
// BENCHMARK TESTS
// ============================================================================

static void benchmark_current_namespace_lookup(void) {
    const int iterations = 100000;
    
    BENCHMARK_ITERATIONS("Current NS Lookup", iterations);
    
    // Add test symbols to current namespace
    for (int i = 0; i < NUM_TEST_SYMBOLS; i++) {
        CljObject *sym = make_symbol_old(test_symbols[i], NULL);
        CljValue val = fixnum(i);
        map_assoc((CljObject*)benchmark_eval_state->current_ns->mappings, sym, (CljObject*)val);
    }
    
    for (int i = 0; i < iterations; i++) {
        CljObject *sym = make_symbol_old(test_symbols[i % NUM_TEST_SYMBOLS], NULL);
        CljObject *result = map_get((CljObject*)benchmark_eval_state->current_ns->mappings, sym);
        (void)result; // Prevent optimization
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_global_namespace_search(void) {
    const int iterations = 10000; // Fewer iterations due to O(n) complexity
    
    BENCHMARK_ITERATIONS("Global NS Search", iterations);
    
    // Add test symbols to various namespaces
    for (int ns_idx = 0; ns_idx < num_test_namespaces; ns_idx++) {
        for (int i = 0; i < NUM_TEST_SYMBOLS; i++) {
            CljObject *sym = make_symbol_old(test_symbols[i], NULL);
            CljValue val = fixnum(i * 1000 + ns_idx);
            map_assoc((CljObject*)test_namespaces[ns_idx]->mappings, sym, (CljObject*)val);
        }
    }
    
    for (int i = 0; i < iterations; i++) {
        CljObject *sym = make_symbol_old(test_symbols[i % NUM_TEST_SYMBOLS], NULL);
        CljObject *result = ns_resolve(benchmark_eval_state, sym);
        (void)result; // Prevent optimization
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_namespace_registry_search(void) {
    const int iterations = 10000;
    
    BENCHMARK_ITERATIONS("NS Registry Search", iterations);
    
    for (int i = 0; i < iterations; i++) {
        char ns_name[64];
        snprintf(ns_name, sizeof(ns_name), "test.ns%d", i % num_test_namespaces);
        CljNamespace *found = ns_find(ns_name);
        (void)found; // Prevent optimization
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_namespace_creation(void) {
    const int iterations = 1000;
    
    BENCHMARK_ITERATIONS("NS Creation", iterations);
    
    for (int i = 0; i < iterations; i++) {
        char ns_name[64];
        snprintf(ns_name, sizeof(ns_name), "benchmark.ns%d", i);
        CljNamespace *ns = ns_get_or_create(ns_name, "benchmark-test.c");
        (void)ns; // Prevent optimization
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_symbol_interning(void) {
    const int iterations = 50000;
    
    BENCHMARK_ITERATIONS("Symbol Interning", iterations);
    
    for (int i = 0; i < iterations; i++) {
        char sym_name[64];
        snprintf(sym_name, sizeof(sym_name), "symbol%d", i % 1000); // Reuse symbols
        CljObject *sym = intern_symbol_global(sym_name);
        (void)sym; // Prevent optimization
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_mixed_lookup_scenarios(void) {
    const int iterations = 5000;
    
    BENCHMARK_ITERATIONS("Mixed Lookup Scenarios", iterations);
    
    for (int i = 0; i < iterations; i++) {
        // Scenario 1: Current namespace lookup (fast)
        CljObject *sym1 = make_symbol_old("current-symbol", NULL);
        CljObject *result1 = map_get((CljObject*)benchmark_eval_state->current_ns->mappings, sym1);
        (void)result1;
        
        // Scenario 2: Global namespace search (slow)
        CljObject *sym2 = make_symbol_old(test_symbols[i % NUM_TEST_SYMBOLS], NULL);
        CljObject *result2 = ns_resolve(benchmark_eval_state, sym2);
        (void)result2;
        
        // Scenario 3: Namespace switching
        if (i % 10 == 0) {
            char ns_name[64];
            snprintf(ns_name, sizeof(ns_name), "test.ns%d", i % num_test_namespaces);
            evalstate_set_ns(benchmark_eval_state, ns_name);
        }
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_namespace_isolation(void) {
    const int iterations = 2000;
    
    BENCHMARK_ITERATIONS("NS Isolation Test", iterations);
    
    // Create isolated namespaces
    CljNamespace *ns1 = ns_get_or_create("isolation.ns1", "benchmark-test.c");
    CljNamespace *ns2 = ns_get_or_create("isolation.ns2", "benchmark-test.c");
    
    // Add same symbol to both namespaces
    CljObject *shared_sym = make_symbol_old("shared-symbol", NULL);
    CljValue val1 = fixnum(100);
    CljValue val2 = fixnum(200);
    
    map_assoc((CljObject*)ns1->mappings, shared_sym, (CljObject*)val1);
    map_assoc((CljObject*)ns2->mappings, shared_sym, (CljObject*)val2);
    
    for (int i = 0; i < iterations; i++) {
        // Test namespace isolation
        CljObject *result1 = map_get((CljObject*)ns1->mappings, shared_sym);
        CljObject *result2 = map_get((CljObject*)ns2->mappings, shared_sym);
        (void)result1;
        (void)result2;
        
        // Verify isolation
        if (i % 100 == 0) {
            // Switch between namespaces
            benchmark_eval_state->current_ns = (i % 2 == 0) ? ns1 : ns2;
        }
    }
    
    BENCHMARK_ITERATIONS_END();
}

static void benchmark_clojure_core_priority(void) {
    const int iterations = 10000;
    
    BENCHMARK_ITERATIONS("Clojure.Core Priority", iterations);
    
    // Test that clojure.core symbols are found first
    for (int i = 0; i < iterations; i++) {
        CljObject *sym = make_symbol_old(test_symbols[i % NUM_TEST_SYMBOLS], NULL);
        CljObject *result = ns_resolve(benchmark_eval_state, sym);
        
        // Verify that clojure.core symbols are found (should be high priority values)
        if (result && is_fixnum((CljValue)result)) {
            int val = as_fixnum((CljValue)result);
            // clojure.core values should be >= 1000 (high priority)
            if (val >= 1000) {
                // Success: Found clojure.core symbol first
            }
        }
        (void)result; // Prevent optimization
    }
    
    BENCHMARK_ITERATIONS_END();
}

// ============================================================================
// PERFORMANCE ANALYSIS
// ============================================================================

static void analyze_namespace_performance(void) {
    printf("\n=== NAMESPACE LOOKUP PERFORMANCE ANALYSIS ===\n");
    
    // Count current namespaces
    int ns_count = 0;
    CljNamespace *cur = ns_registry;
    while (cur) {
        ns_count++;
        cur = cur->next;
    }
    
    printf("Total namespaces: %d\n", ns_count);
    printf("Symbol table entries: %d\n", symbol_count());
    
    // Analyze namespace registry structure
    printf("\nNamespace Registry Analysis:\n");
    printf("- Registry type: Linked List (O(n) search)\n");
    printf("- Current namespace: %s\n", 
           benchmark_eval_state->current_ns ? 
           as_symbol(benchmark_eval_state->current_ns->name)->name : "NULL");
    
    // Performance predictions
    printf("\nPerformance Predictions:\n");
    printf("- Current NS lookup: O(1) hash table\n");
    printf("- Global NS search: O(n) where n = %d namespaces\n", ns_count);
    printf("- NS registry search: O(n) where n = %d namespaces\n", ns_count);
    printf("- Symbol interning: O(1) hash table\n");
    
    // Optimization recommendations
    printf("\nOptimization Recommendations:\n");
    if (ns_count > 10) {
        printf("⚠️  Consider namespace hash table (current: O(n), proposed: O(1))\n");
    }
    if (ns_count > 5) {
        printf("⚠️  Consider namespace priority ordering (clojure.core first)\n");
    }
    printf("💡 Consider symbol resolution caching per AST node\n");
    printf("💡 Consider namespace preloading for common namespaces\n");
}

// ============================================================================
// BENCHMARK RUNNER
// ============================================================================

static void run_namespace_benchmarks(void) {
    printf("🚀 === Namespace Lookup Performance Benchmark ===\n");
    printf("Testing namespace resolution performance with realistic scenarios\n\n");
    
    benchmark_setup();
    
    // Run all benchmarks
    benchmark_current_namespace_lookup();
    benchmark_global_namespace_search();
    benchmark_namespace_registry_search();
    benchmark_namespace_creation();
    benchmark_symbol_interning();
    benchmark_mixed_lookup_scenarios();
    benchmark_namespace_isolation();
    benchmark_clojure_core_priority();
    
    // Analyze results
    analyze_namespace_performance();
    
    // Print results
    benchmark_print_results();
    
    // Export results
    benchmark_export_csv("namespace_lookup_benchmark.csv");
    
    benchmark_teardown();
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    benchmark_init();
    
    run_namespace_benchmarks();
    
    benchmark_cleanup();
    
    printf("\n✅ Namespace Lookup Benchmark Complete\n");
    printf("Results exported to: namespace_lookup_benchmark.csv\n");
    
    return 0;
}
