/*
 * For-Loop Tests for Tiny-CLJ
 * 
 * Tests the for, doseq, and dotimes implementations
 */

#include "minunit.h"
#include "seq.h"
#include "vector.h"
#include "list_operations.h"
#include "function_call.h"
#include "CljObject.h"
#include "clj_symbols.h"
#include "memory_hooks.h"
#include <stdio.h>

// ============================================================================
// FOR-LOOP TESTS
// ============================================================================

static char *test_dotimes_basic(void) {
    printf("\n=== Testing dotimes Basic Functionality ===\n");
    
    WITH_MEMORY_PROFILING({
        // Create a simple test: (dotimes [i 3] (println i))
        // For now, we'll just test that it doesn't crash
        
        // Create binding list: [i 3]
        CljObject *binding_list = AUTORELEASE(make_list());
        CljList *binding_data = as_list(binding_list);
        if (binding_data) {
            binding_data->head = intern_symbol_global("i");
            binding_data->tail = AUTORELEASE(make_list());
            CljList *tail_data = as_list(binding_data->tail);
            if (tail_data) {
                tail_data->head = make_int(3);
                tail_data->tail = NULL;
            }
        }
        
        // Create body: (println i)
        CljObject *body = AUTORELEASE(make_list());
        CljList *body_data = as_list(body);
        if (body_data) {
            body_data->head = intern_symbol_global("println");
            body_data->tail = AUTORELEASE(make_list());
            CljList *body_tail = as_list(body_data->tail);
            if (body_tail) {
                body_tail->head = intern_symbol_global("i");
                body_tail->tail = NULL;
            }
        }
        
        // Create function call: (dotimes [i 3] (println i))
        CljObject *dotimes_call = AUTORELEASE(make_list());
        CljList *call_data = as_list(dotimes_call);
        if (call_data) {
            call_data->head = intern_symbol_global("dotimes");
            call_data->tail = AUTORELEASE(make_list());
            CljList *call_tail = as_list(call_data->tail);
            if (call_tail) {
                call_tail->head = binding_list;
                call_tail->tail = AUTORELEASE(make_list());
                CljList *call_tail2 = as_list(call_tail->tail);
                if (call_tail2) {
                    call_tail2->head = body;
                    call_tail2->tail = NULL;
                }
            }
        }
        
        // Test dotimes evaluation
        CljObject *result = eval_dotimes(dotimes_call, NULL);
        mu_assert("dotimes should return nil", result == NULL || result->type == CLJ_NIL);
        
        RELEASE(dotimes_call);
    });
    
    printf("✓ dotimes basic test passed\n");
    return 0;
}

static char *test_doseq_basic(void) {
    printf("\n=== Testing doseq Basic Functionality ===\n");
    
    MEMORY_TEST_START("doseq Basic Functionality");
    
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
    CljObject *binding_list = AUTORELEASE(make_list());
    CljList *binding_data = as_list(binding_list);
    if (binding_data) {
        binding_data->head = intern_symbol_global("x");
        binding_data->tail = AUTORELEASE(make_list());
        CljList *tail_data = as_list(binding_data->tail);
        if (tail_data) {
            tail_data->head = vec;
            tail_data->tail = NULL;
        }
    }
    
    // Create body: (println x)
    CljObject *body = AUTORELEASE(make_list());
    CljList *body_data = as_list(body);
    if (body_data) {
        body_data->head = intern_symbol_global("println");
        body_data->tail = AUTORELEASE(make_list());
        CljList *body_tail = as_list(body_data->tail);
        if (body_tail) {
            body_tail->head = intern_symbol_global("x");
            body_tail->tail = NULL;
        }
    }
    
    // Create function call: (doseq [x [1 2 3]] (println x))
    CljObject *doseq_call = AUTORELEASE(make_list());
    CljList *call_data = as_list(doseq_call);
    if (call_data) {
        call_data->head = intern_symbol_global("doseq");
        call_data->tail = AUTORELEASE(make_list());
        CljList *call_tail = as_list(call_data->tail);
        if (call_tail) {
            call_tail->head = binding_list;
            call_tail->tail = AUTORELEASE(make_list());
            CljList *call_tail2 = as_list(call_tail->tail);
            if (call_tail2) {
                call_tail2->head = body;
                call_tail2->tail = NULL;
            }
        }
    }
    
    // Test doseq evaluation
    CljObject *result = eval_doseq(doseq_call, NULL);
    mu_assert("doseq should return nil", result == NULL || result->type == CLJ_NIL);
    
    // Cleanup: Release intermediate objects
    RELEASE(vec);
    RELEASE(binding_list);
    RELEASE(body);
    RELEASE(doseq_call);
    
    MEMORY_TEST_END("doseq Basic Functionality");
    
    printf("✓ doseq basic test passed\n");
    return 0;
}

static char *test_for_basic(void) {
    printf("\n=== Testing for Basic Functionality ===\n");
    
    MEMORY_TEST_START("for Basic Functionality");
    
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
    CljObject *binding_list = AUTORELEASE(make_list());
    CljList *binding_data = as_list(binding_list);
    if (binding_data) {
        binding_data->head = intern_symbol_global("x");
        binding_data->tail = AUTORELEASE(make_list());
        CljList *tail_data = as_list(binding_data->tail);
        if (tail_data) {
            tail_data->head = vec;
            tail_data->tail = NULL;
        }
    }
    
    // Create body: x (identity)
    CljObject *body = intern_symbol_global("x");
    
    // Create function call: (for [x [1 2 3]] x)
    CljObject *for_call = AUTORELEASE(make_list());
    CljList *call_data = as_list(for_call);
    if (call_data) {
        call_data->head = intern_symbol_global("for");
        call_data->tail = AUTORELEASE(make_list());
        CljList *call_tail = as_list(call_data->tail);
        if (call_tail) {
            call_tail->head = binding_list;
            call_tail->tail = AUTORELEASE(make_list());
            CljList *call_tail2 = as_list(call_tail->tail);
            if (call_tail2) {
                call_tail2->head = body;
                call_tail2->tail = NULL;
            }
        }
    }
    
    // Test for evaluation
    CljObject *result = eval_for(for_call, NULL);
    mu_assert("for should return a result", result != NULL);
    
    // Cleanup: Release intermediate objects and nested structures
    RELEASE(vec);
    
    // Release nested list structures
    if (binding_data && binding_data->tail) {
        RELEASE(binding_data->tail);
    }
    RELEASE(binding_list);
    
    // Release nested call structures
    if (call_data && call_data->tail) {
        CljList *call_tail = as_list(call_data->tail);
        if (call_tail && call_tail->tail) {
            RELEASE(call_tail->tail);
        }
        RELEASE(call_data->tail);
    }
    RELEASE(for_call);
    
    if (result) RELEASE(result);
    
    MEMORY_TEST_END("for Basic Functionality");
    
    printf("✓ for basic test passed\n");
    return 0;
}

static char *test_dotimes_with_variable(void) {
    printf("\n=== Testing dotimes with Variable Binding ===\n");
    
    MEMORY_TEST_START("dotimes with Variable Binding");
    
    // Create binding list: [i 5]
    CljObject *binding_list = AUTORELEASE(make_list());
    CljList *binding_data = as_list(binding_list);
    if (binding_data) {
        binding_data->head = intern_symbol_global("i");
        binding_data->tail = AUTORELEASE(make_list());
        CljList *tail_data = as_list(binding_data->tail);
        if (tail_data) {
            tail_data->head = make_int(5);
            tail_data->tail = NULL;
        }
    }
    
    // Create body: i (just return the variable)
    CljObject *body = intern_symbol_global("i");
    
    // Create function call: (dotimes [i 5] i)
    CljObject *dotimes_call = AUTORELEASE(make_list());
    CljList *call_data = as_list(dotimes_call);
    if (call_data) {
        call_data->head = intern_symbol_global("dotimes");
        call_data->tail = AUTORELEASE(make_list());
        CljList *call_tail = as_list(call_data->tail);
        if (call_tail) {
            call_tail->head = binding_list;
            call_tail->tail = AUTORELEASE(make_list());
            CljList *call_tail2 = as_list(call_tail->tail);
            if (call_tail2) {
                call_tail2->head = body;
                call_tail2->tail = NULL;
            }
        }
    }
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(dotimes_call, NULL);
    mu_assert("dotimes should return nil", result == NULL || result->type == CLJ_NIL);
    
    RELEASE(dotimes_call);
    
    MEMORY_TEST_END("dotimes with Variable Binding");
    
    printf("✓ dotimes with variable binding test passed\n");
    return 0;
}

static char *test_for_with_simple_expression(void) {
    printf("\n=== Testing for with Simple Expression ===\n");
    
    MEMORY_TEST_START("for with Simple Expression");
    
    // Create a test vector
    CljObject *vec = AUTORELEASE(make_vector(2, 1));
    CljPersistentVector *vec_data = as_vector(vec);
    if (vec_data) {
        vec_data->data[0] = make_int(1);
        vec_data->data[1] = make_int(2);
        vec_data->count = 2;
    }
    
    // Create binding list: [x [1 2]]
    CljObject *binding_list = AUTORELEASE(make_list());
    CljList *binding_data = as_list(binding_list);
    if (binding_data) {
        binding_data->head = intern_symbol_global("x");
        binding_data->tail = AUTORELEASE(make_list());
        CljList *tail_data = as_list(binding_data->tail);
        if (tail_data) {
            tail_data->head = vec;
            tail_data->tail = NULL;
        }
    }
    
    // Create body: x (simple identity)
    CljObject *body = intern_symbol_global("x");
    
    // Create function call: (for [x [1 2]] x)
    CljObject *for_call = AUTORELEASE(make_list());
    CljList *call_data = as_list(for_call);
    if (call_data) {
        call_data->head = intern_symbol_global("for");
        call_data->tail = AUTORELEASE(make_list());
        CljList *call_tail = as_list(call_data->tail);
        if (call_tail) {
            call_tail->head = binding_list;
            call_tail->tail = AUTORELEASE(make_list());
            CljList *call_tail2 = as_list(call_tail->tail);
            if (call_tail2) {
                call_tail2->head = body;
                call_tail2->tail = NULL;
            }
        }
    }
    
    // Test for evaluation
    CljObject *result = eval_for(for_call, NULL);
    mu_assert("for with simple expression should return a result", result != NULL);
    
    // Cleanup: Release intermediate objects
    RELEASE(vec);
    RELEASE(binding_list);
    RELEASE(for_call);
    if (result) RELEASE(result);
    
    MEMORY_TEST_END("for with Simple Expression");
    
    printf("✓ for with simple expression test passed\n");
    return 0;
}

// ============================================================================
// TEST SUITE REGISTRY
// ============================================================================

static char *all_for_loop_tests(void) {
    mu_run_test(test_dotimes_basic);
    mu_run_test(test_doseq_basic);
    mu_run_test(test_for_basic);
    mu_run_test(test_dotimes_with_variable);
    mu_run_test(test_for_with_simple_expression);
    
    return 0;
}

int main(void) {
    printf("=== Tiny-CLJ For-Loop Tests with Memory Profiling ===\n");
    
    // Initialize memory profiling with hooks
    memory_profiling_init_with_hooks();
    
    // 🧪 TEST HYPOTHESIS: Create autorelease pool
    printf("🧪 Creating autorelease pool for leak testing...\n");
    CljObjectPool *pool = cljvalue_pool_push();
    
    // Initialize symbol table
    init_special_symbols();
    
    int result = run_minunit_tests(all_for_loop_tests, "For-Loop Tests");
    
    // 🧪 TEST HYPOTHESIS: Cleanup autorelease pool
    printf("🧪 Cleaning up autorelease pool...\n");
    cljvalue_pool_pop(pool);
    
    // Cleanup memory profiling
    memory_profiling_cleanup_with_hooks();
    
    return result;
}
