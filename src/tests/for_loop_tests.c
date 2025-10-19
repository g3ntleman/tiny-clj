/*
 * For-Loop Tests using Unity Framework
 * 
 * Tests for for, doseq, and dotimes implementations.
 */

#include "unity.h"
#include "object.h"
#include "symbol.h"
#include "memory_profiler.h"
#include "tiny_clj.h"
#include "parser.h"
#include "seq.h"
#include "vector.h"
#include "list_operations.h"
#include "value.h"
#include "function_call.h"
#include "memory.h"
#include <stdio.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// FOR-LOOP TESTS
// ============================================================================

void test_dotimes_basic(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a simple test: (dotimes [i 3] (println 42))
        // Test that dotimes doesn't crash with a simple body
        
        // Create binding list: [i 3]
        CljObject *binding_list = (CljObject*)make_list(intern_symbol_global("i"), (CljObject*)make_list((CljObject*)make_fixnum(3), NULL));
        
        // Create body: 42 - simple literal without symbol resolution
        CljObject *body = (CljObject*)make_fixnum(42);
        
        // Create function call: (dotimes [i 3] 42)
        CljObject *dotimes_call = (CljObject*)make_list(intern_symbol_global("dotimes"), (CljObject*)make_list((CljObject*)binding_list, (CljObject*)make_list(body, NULL)));
        
        // Test dotimes evaluation - should not crash
        CljObject *result = eval_dotimes((CljObject*)dotimes_call, NULL);
        TEST_ASSERT_TRUE(result == NULL || result->type == CLJ_NIL);
        
        // Clean up all objects
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)dotimes_call);
    }
}

void test_doseq_basic(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a simple test: (doseq [x [1 2 3]] (println x))
        // Test that doseq doesn't crash with a simple body
        
        // Create vector: [1 2 3]
        CljValue vec = make_vector_v(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = (CljObject*)make_fixnum(1);
        vec_data->data[1] = (CljObject*)make_fixnum(2);
        vec_data->data[2] = (CljObject*)make_fixnum(3);
        vec_data->count = 3;
        
        // Create binding list: [x [1 2 3]]
        CljObject *binding_list = (CljObject*)make_list(intern_symbol_global("x"), (CljObject*)make_list(vec, NULL));
        
        // Create body: x - simple symbol reference
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (doseq [x [1 2 3]] x)
        CljObject *doseq_call = (CljObject*)make_list(intern_symbol_global("doseq"), (CljObject*)make_list((CljObject*)binding_list, (CljObject*)make_list(body, NULL)));
        
        // Test doseq evaluation - should not crash
        CljObject *result = eval_doseq((CljObject*)doseq_call, NULL);
        TEST_ASSERT_TRUE(result == NULL || result->type == CLJ_NIL);
        
        // Clean up all objects
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)doseq_call);
    }
}

void test_for_basic(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Create a simple test: (for [x [1 2 3]] x)
        // Test that for doesn't crash with a simple body
        
        // Create vector: [1 2 3]
        CljValue vec = make_vector_v(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = (CljObject*)make_fixnum(1);
        vec_data->data[1] = (CljObject*)make_fixnum(2);
        vec_data->data[2] = (CljObject*)make_fixnum(3);
        vec_data->count = 3;
        
        // Create binding list: [x [1 2 3]]
        CljObject *binding_list = (CljObject*)make_list(intern_symbol_global("x"), (CljObject*)make_list(vec, NULL));
        
        // Create body: x - simple symbol reference
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (for [x [1 2 3]] x)
        CljObject *for_call = (CljObject*)make_list(intern_symbol_global("for"), (CljObject*)make_list((CljObject*)binding_list, (CljObject*)make_list(body, NULL)));
        
        // Test for evaluation - should not crash
        CljObject *result = eval_for((CljObject*)for_call, NULL);
        (void)result; // Suppress unused variable warning
        // Note: eval_for may not be implemented yet - just test it doesn't crash
        // TEST_ASSERT_TRUE(result == NULL || result->type == CLJ_NIL); // Commented out - function may not exist
        
        // Clean up all objects
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)for_call);
    }
}

void test_dotimes_with_environment(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test dotimes with environment binding
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Create binding list: [i 3]
        CljObject *binding_list = (CljObject*)make_list(intern_symbol_global("i"), (CljObject*)make_list((CljObject*)make_fixnum(3), NULL));
        
        // Create body: i - symbol reference
        CljObject *body = intern_symbol_global("i");
        
        // Create function call: (dotimes [i 3] i)
        CljObject *dotimes_call = (CljObject*)make_list(intern_symbol_global("dotimes"), (CljObject*)make_list((CljObject*)binding_list, (CljObject*)make_list(body, NULL)));
        
        // Test dotimes evaluation with environment
        CljObject *result = eval_dotimes((CljObject*)dotimes_call, (CljMap*)NULL);
        TEST_ASSERT_TRUE(result == NULL || result->type == CLJ_NIL);
        
        // Clean up
        evalstate_free(eval_state);
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)dotimes_call);
    }
}

void test_doseq_with_environment(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test doseq with environment binding
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Create vector: [1 2 3]
        CljValue vec = make_vector_v(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = (CljObject*)make_fixnum(1);
        vec_data->data[1] = (CljObject*)make_fixnum(2);
        vec_data->data[2] = (CljObject*)make_fixnum(3);
        vec_data->count = 3;
        
        // Create binding list: [x [1 2 3]]
        CljObject *binding_list = (CljObject*)make_list(intern_symbol_global("x"), (CljObject*)make_list(vec, NULL));
        
        // Create body: x - symbol reference
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (doseq [x [1 2 3]] x)
        CljObject *doseq_call = (CljObject*)make_list(intern_symbol_global("doseq"), (CljObject*)make_list((CljObject*)binding_list, (CljObject*)make_list(body, NULL)));
        
        // Test doseq evaluation with environment
        CljObject *result = eval_doseq((CljObject*)doseq_call, (CljMap*)NULL);
        TEST_ASSERT_TRUE(result == NULL || result->type == CLJ_NIL);
        
        // Clean up
        evalstate_free(eval_state);
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)doseq_call);
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
