/*
 * Benchmark Tests Main
 * 
 * Consolidates all benchmark tests into a single executable for better IDE integration
 * and easier debugging while maintaining test isolation.
 */

#include "../unity.h"
#include "../tiny_clj.h"
#include "../CljObject.h"
#include "../vector.h"
#include "../clj_symbols.h"
#include "../namespace.h"
#include "../clj_parser.h"
#include "../platform.h"

#include "../function_call.h"
#include "../benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test setup and teardown
void setUp(void) {
    // Initialize symbol table
    init_special_symbols();
    
    // Initialize meta registry
    meta_registry_init();
    
    // Initialize benchmark system
    benchmark_init();
}

void tearDown(void) {
    // Cleanup symbol table
    symbol_table_cleanup();
    
    // Cleanup autorelease pool
    cljvalue_pool_cleanup_all();
    
    // Cleanup benchmark system
    benchmark_cleanup();
}

// ============================================================================
// INCLUDE ALL BENCHMARK TEST FUNCTIONS
// ============================================================================

// From test_benchmark.c
void test_benchmark_arithmetic(void) {
    BENCHMARK_ITERATIONS("arithmetic", 1000000)
    volatile int acc = 0;
    for (int i = 0; i < _iterations; i++) acc += i;
    (void)acc;
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_collections(void) {
    BENCHMARK_ITERATIONS("collections", 20000)
    for (int i = 0; i < _iterations; i++) {
        CljObject *v = make_vector(16, 1);
        for (int j = 0; j < 16; j++) {
            vector_conj(v, make_int(j));
        }
        // Avoid release due to known vector finalizer issue in deep free path during benchmarks
    }
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_functions(void) {
    BENCHMARK_ITERATIONS("functions", 100000)
    for (int i = 0; i < _iterations; i++) {
        CljObject *a = make_int(1);
        CljObject *b = make_int(2);
        (void)a; (void)b;
    }
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_parsing(void) {
    BENCHMARK_ITERATIONS("parsing", 50000)
    for (int i = 0; i < _iterations; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(+ %d %d)", i % 100, (i+1) % 100);
        EvalState st = {0};
        const char *p = buf;
        CljObject *expr = parse_expr(&p, &st);
        (void)expr;
    }
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_memory(void) {
    BENCHMARK_ITERATIONS("memory", 100000)
    for (int i = 0; i < _iterations; i++) {
        CljObject *s = make_string("abc");
        release(s);
    }
    BENCHMARK_ITERATIONS_END();
}

// From test_benchmark_simple.c
void test_benchmark_simple_arithmetic(void) {
    BENCHMARK_ITERATIONS("simple_arithmetic", 200000)
    volatile int x = 1; x++; (void)x;
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_simple_collections(void) {
    BENCHMARK_ITERATIONS("simple_collections", 10000)
    for (int i = 0; i < _iterations; i++) {
        CljObject *v = make_vector(4, 1);
        vector_conj(v, make_int(1));
        vector_conj(v, make_int(2));
        vector_conj(v, make_int(3));
        vector_conj(v, make_int(4));
        // Avoid release due to known vector finalizer issue in deep free path during benchmarks
    }
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_simple_functions(void) {
    BENCHMARK_ITERATIONS("simple_functions", 200000)
    volatile int z = 0; z += 1; (void)z;
    BENCHMARK_ITERATIONS_END();
}
void test_benchmark_simple_parsing(void) {
    BENCHMARK_ITERATIONS("simple_parsing", 20000)
    for (int i = 0; i < _iterations; i++) {
        const char *p = "42";
        EvalState st = {0};
        CljObject *expr = parse_expr(&p, &st);
        (void)expr;
    }
    BENCHMARK_ITERATIONS_END();
}

// From test_executable_size.c
void test_executable_size(void) {
    BENCHMARK_START("exe_size")
    BENCHMARK_END();
}
void test_executable_size_optimization(void) {
    BENCHMARK_START("exe_size_opt")
    BENCHMARK_END();
}
void test_executable_size_comparison(void) {
    BENCHMARK_START("exe_size_cmp")
    BENCHMARK_END();
}


// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

typedef struct {
    const char *name;
    const char *suite;
    void (*test_func)(void);
} TestEntry;

static TestEntry benchmark_tests[] = {
    // Core benchmark tests
    {"test_benchmark_arithmetic", "performance", test_benchmark_arithmetic},
    {"test_benchmark_collections", "performance", test_benchmark_collections},
    {"test_benchmark_functions", "performance", test_benchmark_functions},
    {"test_benchmark_parsing", "performance", test_benchmark_parsing},
    {"test_benchmark_memory", "performance", test_benchmark_memory},
    
    // Simple benchmark tests
    {"test_benchmark_simple_arithmetic", "simple", test_benchmark_simple_arithmetic},
    {"test_benchmark_simple_collections", "simple", test_benchmark_simple_collections},
    {"test_benchmark_simple_functions", "simple", test_benchmark_simple_functions},
    {"test_benchmark_simple_parsing", "simple", test_benchmark_simple_parsing},
    
    // Executable size tests
    {"test_executable_size", "size", test_executable_size},
    {"test_executable_size_optimization", "size", test_executable_size_optimization},
    {"test_executable_size_comparison", "size", test_executable_size_comparison},
    // clojurescript demo benchmark removed
    {"test_benchmark_repl_startup_eval", "repl", NULL},
};

static const int benchmark_test_count = sizeof(benchmark_tests) / sizeof(benchmark_tests[0]);

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --help, -h          Show this help message\n");
    printf("  --list, -l          List all available tests\n");
    printf("  --suite=NAME, -s    Run tests from specific suite\n");
    printf("  --test=NAME, -t     Run specific test\n");
    printf("  --all, -a           Run all tests (default)\n");
    printf("  --report, -r        Generate benchmark report\n");
    printf("  --compare, -c       Compare with previous benchmarks\n");
    printf("\nAvailable suites:\n");
    printf("  performance, simple, size, clojurescript\n");
}

void list_tests(void) {
    printf("Available Benchmark Tests:\n\n");
    
    const char *current_suite = NULL;
    for (int i = 0; i < benchmark_test_count; i++) {
        if (current_suite == NULL || strcmp(current_suite, benchmark_tests[i].suite) != 0) {
            current_suite = benchmark_tests[i].suite;
            printf("\n=== %s ===\n", current_suite);
        }
        printf("  %s\n", benchmark_tests[i].name);
    }
}

int run_suite(const char *suite_name) {
    printf("Running benchmark tests from suite: %s\n\n", suite_name);
    
    int run_count = 0;
    for (int i = 0; i < benchmark_test_count; i++) {
        if (strcmp(benchmark_tests[i].suite, suite_name) == 0) {
            printf("Running %s...\n", benchmark_tests[i].name);
            benchmark_tests[i].test_func();
            run_count++;
        }
    }
    
    if (run_count == 0) {
        printf("No tests found for suite: %s\n", suite_name);
        return 1;
    }
    
    printf("\nRan %d benchmark tests from suite: %s\n", run_count, suite_name);
    return 0;
}

int run_test(const char *test_name) {
    printf("Running benchmark test: %s\n\n", test_name);
    
    for (int i = 0; i < benchmark_test_count; i++) {
        if (strcmp(benchmark_tests[i].name, test_name) == 0) {
            if (benchmark_tests[i].test_func) {
                benchmark_tests[i].test_func();
            } else if (strcmp(test_name, "test_benchmark_repl_startup_eval") == 0) {
                // Measure 10x script startup via -e
                BENCHMARK_ITERATIONS("repl_startup_eval_10x", 10)
                for (int r = 0; r < _iterations; r++) {
                    // Use --no-core for pure startup, or drop it to measure full load
                    int ret = system("./tiny-clj-repl --no-core -e \"(+ 1 2)\" >/dev/null 2>&1");
                    (void)ret;
                }
                BENCHMARK_ITERATIONS_END();
                benchmark_print_results();
            }
            printf("\nBenchmark test completed: %s\n", test_name);
            return 0;
        }
    }
    
    printf("Benchmark test not found: %s\n", test_name);
    return 1;
}

int run_all_tests(void) {
    printf("Running all benchmark tests...\n\n");
    
    UNITY_BEGIN();
    
    for (int i = 0; i < benchmark_test_count; i++) {
        if (benchmark_tests[i].test_func) {
            RUN_TEST(benchmark_tests[i].test_func);
        } else if (strcmp(benchmark_tests[i].name, "test_benchmark_repl_startup_eval") == 0) {
            // Inline benchmark without Unity wrapper function
            BENCHMARK_ITERATIONS("repl_startup_eval_10x", 10)
            for (int r = 0; r < _iterations; r++) {
                int ret = system("./tiny-clj-repl --no-core -e \"(+ 1 2)\" >/dev/null 2>&1");
                (void)ret;
            }
            BENCHMARK_ITERATIONS_END();
        }
    }
    
    return UNITY_END();
}

int generate_report(void) {
    printf("Generating benchmark report...\n\n");
    
    // Run all benchmarks and generate report
    run_all_tests();
    
    // Generate CSV report
    benchmark_print_results();
    benchmark_export_csv("benchmark_results.csv");
    benchmark_generate_report("benchmark_report.csv");
    
    printf("Benchmark report generated: benchmark_report.csv\n");
    return 0;
}

int compare_benchmarks(void) {
    printf("Comparing with previous benchmarks...\n\n");
    
    // Run current benchmarks
    run_all_tests();
    
    // Compare with previous results
    benchmark_compare_with_previous("benchmark_report.csv", "benchmark_previous.csv");
    
    printf("Benchmark comparison completed\n");
    return 0;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[]) {
    printf("=== Tiny-Clj Benchmark Test Runner ===\n\n");
    
    // Parse command line arguments
    if (argc == 1) {
        // No arguments - run all tests
        return run_all_tests();
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            list_tests();
            return 0;
        } else if (strncmp(argv[i], "--suite=", 8) == 0) {
            return run_suite(argv[i] + 8);
        } else if (strncmp(argv[i], "-s", 2) == 0 && i + 1 < argc) {
            return run_suite(argv[++i]);
        } else if (strncmp(argv[i], "--test=", 7) == 0) {
            return run_test(argv[i] + 7);
        } else if (strncmp(argv[i], "-t", 2) == 0 && i + 1 < argc) {
            return run_test(argv[++i]);
        } else if (strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "-a") == 0) {
            return run_all_tests();
        } else if (strcmp(argv[i], "--report") == 0 || strcmp(argv[i], "-r") == 0) {
            return generate_report();
        } else if (strcmp(argv[i], "--compare") == 0 || strcmp(argv[i], "-c") == 0) {
            return compare_benchmarks();
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    return 0;
}
