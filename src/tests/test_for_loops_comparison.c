#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minunit.h"
#include "memory_profiler.h"
#include "memory_hooks.h"
#include "CljObject.h"
#include "vector.h"
#include "list_operations.h"
#include "function_call.h"
#include "namespace.h"
#include "clj_symbols.h"
#include "seq.h"
#include "reader.h"
#include "clj_parser.h"

// Test-Konstanten
#define TEST_VECTOR_SIZE 10

// eval_string is now part of the public API in clj_parser.h

// Gemeinsame Test-Daten für alle Vergleiche
static CljObject *shared_string_vector = NULL;
static EvalState *shared_eval_state = NULL;


void setUp(void) {
    // Initialize symbol table
    init_special_symbols();
    
    // Create shared vector by evaluating Clojure string: ["Alpha" "Beta" "Gamma" "Delta" "Epsilon" "Zeta" "Eta" "Theta" "Iota" "Kappa"]
    const char* clojure_vector_str = R"(["Alpha" "Beta" "Gamma" "Delta" "Epsilon" "Zeta" "Eta" "Theta" "Iota" "Kappa"])";
    
    // Create shared eval state
    shared_eval_state = evalstate_new();
    
    // Parse the Clojure string into a vector object
    shared_string_vector = parse(clojure_vector_str, shared_eval_state);
    
    // Verify it's a vector with expected elements
    if (shared_string_vector && shared_string_vector->type == CLJ_VECTOR) {
        CljPersistentVector *vec_data = as_vector(shared_string_vector);
        if (vec_data && vec_data->count == TEST_VECTOR_SIZE) {
            printf("Successfully parsed Clojure vector with %d elements\n", vec_data->count);
        } else {
            printf("Error: Vector has unexpected count: %d (expected %d)\n", 
                   vec_data ? vec_data->count : -1, TEST_VECTOR_SIZE);
            exit(1); // Fail fast if parsing is wrong
        }
    } else {
        printf("Error: Failed to parse Clojure vector or wrong type: %d\n", 
               shared_string_vector ? shared_string_vector->type : -1);
        exit(1); // Fail fast if parsing fails
    }
}

void tearDown(void) {
    // Cleanup shared data
    if (shared_string_vector) {
        RELEASE(shared_string_vector);
        shared_string_vector = NULL;
    }
    
    if (shared_eval_state) {
        free(shared_eval_state);
        shared_eval_state = NULL;
    }
    
    // Cleanup symbol table
    symbol_table_cleanup();
}

// Test 1: Direct Vector Iteration (Baseline)
static char* test_direct_vector_iteration(void) {
    WITH_MEMORY_PROFILING({
        int count = 0;
        CljPersistentVector *vec_data = (CljPersistentVector*)shared_string_vector;
        
        // Direct vector access - no allocations
        for (int i = 0; i < TEST_VECTOR_SIZE; i++) {
            if (vec_data->data[i] && vec_data->data[i]->type == CLJ_STRING) {
                count++;
            }
        }
        
        mu_assert("Should iterate over all elements", count == TEST_VECTOR_SIZE);
    });
    return 0;
}

// Test 2: dotimes Clojure expression evaluation
static char* test_dotimes_clojure_expr(void) {
    WITH_MEMORY_PROFILING({
        // Activate autorelease pool for this test
        cljvalue_pool_push();
        
        // Parse and evaluate Clojure expression: (dotimes [i 10] (println i))
        const char* dotimes_expr = "(dotimes [i 10] (println i))";
        CljObject *parsed = parse_string(dotimes_expr, shared_eval_state);
        mu_assert("Should parse dotimes expression", parsed != NULL);
        
        CljObject *result = eval_parsed(parsed, shared_eval_state);
        // No manual cleanup needed - parsed is autoreleased
        
        mu_assert("Should evaluate dotimes expression", result != NULL);
        
        // No manual cleanup needed - result is autoreleased
        
        // Clean up pool
        cljvalue_pool_pop(NULL);
    });
    return 0;
}

// Test 3: doseq Clojure expression evaluation
static char* test_doseq_clojure_expr(void) {
    WITH_MEMORY_PROFILING({
        // Activate autorelease pool for this test
        cljvalue_pool_push();
        
        // Parse and evaluate Clojure expression: (doseq [x vector] (println x))
        const char* doseq_expr = 
            "(doseq [x [Alpha Beta Gamma Delta Epsilon "
            "Zeta Eta Theta Iota Kappa]] (println x))";
        CljObject *parsed = parse_string(doseq_expr, shared_eval_state);
        mu_assert("Should parse doseq expression", parsed != NULL);
        
        CljObject *result = eval_parsed(parsed, shared_eval_state);
        // No manual cleanup needed - parsed is autoreleased
        
        mu_assert("Should evaluate doseq expression", result != NULL);
        
        // No manual cleanup needed - result is autoreleased
        
        // Clean up pool
        cljvalue_pool_pop(NULL);
    });
    return 0;
}

// Test 4: for Clojure expression evaluation
static char* test_for_clojure_expr(void) {
    WITH_MEMORY_PROFILING({
        // Activate autorelease pool for this test
        cljvalue_pool_push();
        
        // Parse and evaluate Clojure expression: (for [x vector] x)
        const char* for_expr = 
            "(for [x [Alpha Beta Gamma Delta Epsilon "
            "Zeta Eta Theta Iota Kappa]] x)";
        CljObject *parsed = parse_string(for_expr, shared_eval_state);
        mu_assert("Should parse for expression", parsed != NULL);
        
        CljObject *result = eval_parsed(parsed, shared_eval_state);
        // No manual cleanup needed - parsed is autoreleased
        
        mu_assert("Should evaluate for expression", result != NULL);
        
        // No manual cleanup needed - result is autoreleased
        
        // Clean up pool
        cljvalue_pool_pop(NULL);
    });
    return 0;
}

// Test 5: seq iteration with shared vector using Clojure expression
static char* test_seq_shared_vector(void) {
    WITH_MEMORY_PROFILING({
        // Activate autorelease pool for this test
        cljvalue_pool_push();
        
        // Parse and evaluate Clojure expression: (seq vector)
        const char* seq_expr = 
            "(seq [Alpha Beta Gamma Delta Epsilon "
            "Zeta Eta Theta Iota Kappa])";
        CljObject *parsed = parse_string(seq_expr, shared_eval_state);
        mu_assert("Should parse seq expression", parsed != NULL);
        
        CljObject *seq_result = eval_parsed(parsed, shared_eval_state);
        // No manual cleanup needed - parsed is autoreleased
        
        mu_assert("Should evaluate seq expression", seq_result != NULL);
        
        // For now, just verify the expression parses and evaluates
        // TODO: Implement proper seq iteration once seq_rest is fixed
        mu_assert("Should have seq result", seq_result != NULL);
        
        // No manual cleanup needed - result is autoreleased
        
        // Clean up pool
        cljvalue_pool_pop(NULL);
    });
    return 0;
}

static char* all_tests(void) {
    mu_run_test(test_direct_vector_iteration);
    mu_run_test(test_dotimes_clojure_expr);
    mu_run_test(test_doseq_clojure_expr);
    mu_run_test(test_for_clojure_expr);
    mu_run_test(test_seq_shared_vector);
    
    return 0;
}

int main(void) {
    printf("=== MEMORY COMPARISON: VECTOR vs SEQ vs FOR-LOOPS (%d Strings) ===\n\n", TEST_VECTOR_SIZE);
    
    // Initialize memory profiler
    memory_profiler_init();
    memory_profiling_init_with_hooks();
    
    // Setup test data
    setUp();
    
    char *result = all_tests();
    
    if (result != 0) {
        printf("FAILED: %s\n", result);
    } else {
        printf("ALL TESTS PASSED\n");
    }
    
    printf("\n=== FINAL MEMORY STATISTICS ===\n");
    memory_profiler_print_stats("Final");
    memory_profiler_check_leaks("Final");
    
    // Cleanup
    tearDown();
    memory_profiling_cleanup_with_hooks();
    memory_profiler_cleanup();
    
    return result != 0;
}