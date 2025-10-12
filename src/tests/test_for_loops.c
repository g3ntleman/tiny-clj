/*
 * For-Loop Tests for Tiny-CLJ
 * 
 * Tests the for, doseq, and dotimes implementations
 */

#include "object.h"
#include "clj_symbols.h"
#include "memory_hooks.h"
#include "memory_profiler.h"
#include "tiny_clj.h"
#include "parser.h"
#include "seq.h"
#include "vector.h"
#include "list_operations.h"
#include "function_call.h"
#include "minunit.h"
#include <stdio.h>

// Helper functions removed - not used in current tests

// ============================================================================
// FOR-LOOP TESTS
// ============================================================================

static char *test_dotimes_basic(void) {
    
    WITH_AUTORELEASE_POOL({
        // Create a simple test: (dotimes [i 3] (println 42))
        // Test that dotimes doesn't crash with a simple body
        
        // Create binding list: [i 3]
        CljList *binding_list = make_list(intern_symbol_global("i"), make_list(make_int(3), NULL));
        
        // Create body: 42 - simple literal without symbol resolution
        CljObject *body = make_int(42);
        
        // Create function call: (dotimes [i 3] 42)
        CljList *dotimes_call = make_list(intern_symbol_global("dotimes"), make_list((CljObject*)binding_list, make_list(body, NULL)));
        
        // Test dotimes evaluation - should not crash
        CljObject *result = eval_dotimes((CljObject*)dotimes_call, NULL);
        mu_assert("dotimes should return nil", result == NULL || result->type == CLJ_NIL);
        
        // Clean up all objects
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)dotimes_call);
    });
    
    return 0;
}

static char *test_doseq_basic(void) {
    
    WITH_AUTORELEASE_POOL({
        // Create a test vector
        CljObject *vec = AUTORELEASE(make_vector(3, 1));
        CljPersistentVector *vec_data = as_vector(vec);
        if (vec_data) {
            vec_data->data[0] = make_int(1);
            vec_data->data[1] = make_int(2);
            vec_data->data[2] = make_int(3);
            vec_data->count = 3;
        }
        
        // Create binding list: [x [1 2 3]]
        CljObject *binding_list = AUTORELEASE(make_list(intern_symbol_global("x"), make_list(vec, NULL)));
        
        // Create body: 42 - simple literal without symbol resolution
        CljObject *body = make_int(42);
        
        // Create function call: (doseq [x [1 2 3]] 42)
        CljObject *doseq_call = AUTORELEASE(make_list(intern_symbol_global("doseq"), make_list(binding_list, make_list(body, NULL))));
        
        // Test doseq evaluation
        CljObject *result = eval_doseq(doseq_call, NULL);
        mu_assert("doseq should return nil", result == NULL || result->type == CLJ_NIL);
        
        // Memory balance is automatically checked by WITH_MEMORY_PROFILING after pool cleanup
    });
    
    return 0;
}

static char *test_for_basic(void) {
    
    WITH_AUTORELEASE_POOL_EVAL({
        // Test for evaluation using parse + eval_parsed
        char *for_expr = "(for [x [1 2 3]] x)";
        CljObject *parsed = parse(for_expr, eval_state);
        CljObject *result = eval_parsed(parsed, eval_state);
        mu_assert("for should return a result", result != NULL);
    });
    
    return 0;
}

static char *test_dotimes_with_variable(void) {
    
    WITH_AUTORELEASE_POOL_EVAL({
        // Test dotimes evaluation using parse + eval_parsed
        char *dotimes_expr = "(dotimes [i 5] i)";
        CljObject *parsed = parse(dotimes_expr, eval_state);
        CljObject *result = eval_parsed(parsed, eval_state);
        mu_assert("dotimes should return nil", result == NULL || result->type == CLJ_NIL);
    });
    
    return 0;
}

static char *test_for_with_simple_expression(void) {
    
    WITH_AUTORELEASE_POOL_EVAL({
        // Test for evaluation using parse + eval_parsed
        char *for_expr = "(for [x [1 2]] x)";
        CljObject *parsed = parse(for_expr, eval_state);
        CljObject *result = eval_parsed(parsed, eval_state);
        mu_assert("for with simple expression should return a result", result != NULL);
    });
    
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_for_loop_tests(void) {
    // TEMPORARY: Tests disabled due to symbol resolution issues
    // The eval_dotimes and eval_doseq functions need proper environment setup
    // for symbol binding in loop constructs
    mu_run_test(test_dotimes_basic);
    mu_run_test(test_doseq_basic);
    mu_run_test(test_for_basic);
    mu_run_test(test_dotimes_with_variable);
    mu_run_test(test_for_with_simple_expression);
    
    return 0;
}

// Export for unified test runner
char *run_for_loop_tests(void) {
    memory_profiling_init_with_hooks();
    init_special_symbols();
    EvalState *st = evalstate_new();
    // Note: set_global_eval_state() removed - Exception handling now independent
    
    // Load clojure_core (objects are manually released in eval_core_source)
    load_clojure_core(st);
    
    char *result = all_for_loop_tests();
    
    // Fix memory leak: Free the EvalState
    evalstate_free(st);
    
    memory_profiling_cleanup_with_hooks();
    return result;
}

#ifndef UNIFIED_TEST_RUNNER
// Standalone mode
int main(void) {
    printf("=== Tiny-CLJ For-Loop Tests with Memory Profiling ===\n");
    memory_profiling_init_with_hooks();
    init_special_symbols();
    EvalState *st = evalstate_new();
    // Note: set_global_eval_state() removed - Exception handling now independent
    // Load clojure_core with autorelease pool to clean up temporary objects
    AUTORELEASE_POOL_SCOPE(pool) {
        load_clojure_core(st);
    }
    
    int res = run_minunit_tests(all_for_loop_tests, "For-Loop Tests");
    
    memory_profiling_cleanup_with_hooks();
    return res;
}
#endif
