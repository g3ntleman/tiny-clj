/*
 * For-Loop Tests using Unity Framework
 * 
 * Tests for for, doseq, and dotimes implementations.
 */

#include "tests_common.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// FOR-LOOP TESTS
// ============================================================================

// dotimes tests have been moved to test_dotimes.c
// These empty tests were redundant and have been removed

TEST(test_doseq_basic) {
    // Test that eval_doseq handles NULL input gracefully
    CljMap *env = (CljMap*)make_map(4);
    
    // Test with NULL list
    CljObject *result = eval_doseq(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RELEASE(env);
}

TEST(test_doseq_with_environment) {
    // Test doseq with environment binding
    EvalState *eval_state = evalstate_new();
    TEST_ASSERT_NOT_NULL(eval_state);
        
        // Create vector: [1 2 3]
        CljValue vec = make_vector(3, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->data[2] = fixnum(3);
        vec_data->count = 3;
        
        // Create binding list: [x [1 2 3]]
        CljObject *binding_list = make_list((ID)intern_symbol_global("x"), (CljList*)make_list((ID)vec, NULL));
        
        // Create body: x - symbol reference
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (doseq [x [1 2 3]] x)
        CljObject *doseq_call = make_list((ID)intern_symbol_global("doseq"), (CljList*)make_list((ID)binding_list, (CljList*)make_list((ID)body, NULL)));
        
        // Create a simple environment
        CljMap *env = (CljMap*)make_map(4);
        
        // Test doseq evaluation with environment
        CljObject *result = eval_doseq((CljList*)(CljObject*)doseq_call, env);
        TEST_ASSERT_TRUE(result == NULL);
        
        // Clean up environment
        RETAIN(env);
        RELEASE(env);
        
        // Clean up
        evalstate_free(eval_state);
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)doseq_call);
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests