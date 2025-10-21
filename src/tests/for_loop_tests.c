/*
 * For-Loop Tests using Unity Framework
 * 
 * Tests for for, doseq, and dotimes implementations.
 */

#include "../../external/unity/src/unity.h"
#include "../object.h"
#include "../vector.h"
#include "../value.h"
#include "../function_call.h"
#include "../memory.h"
#include "../map.h"
#include <stdio.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// FOR-LOOP TESTS
// ============================================================================

void test_dotimes_basic(void) {
    // Test that eval_dotimes handles NULL input gracefully
    CljMap *env = make_map_old(4);
    
    // Test with NULL list
    CljObject *result = eval_dotimes(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with non-list
    CljValue not_list = fixnum(42);
    result = eval_dotimes((CljObject*)not_list, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
    // Note: fixnum(42) is an immediate value, no need to RELEASE
}

void test_doseq_basic(void) {
    // Test that eval_doseq handles NULL input gracefully
    CljMap *env = make_map_old(4);
    
    // Test with NULL list
    CljObject *result = eval_doseq(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with non-list
    CljValue not_list = fixnum(42);
    result = eval_doseq((CljObject*)not_list, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
    // Note: fixnum(42) is an immediate value, no need to RELEASE
}

void test_for_basic(void) {
    // Test that eval_for handles NULL input gracefully
    CljMap *env = make_map_old(4);
    
    // Test with NULL list
    CljObject *result = eval_for(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with non-list
    CljValue not_list = fixnum(42);
    result = eval_for((CljObject*)not_list, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
    // Note: fixnum(42) is an immediate value, no need to RELEASE
}

void test_dotimes_with_environment(void) {
    // Use WITH_AUTORELEASE_POOL for eval_dotimes which uses autorelease()
    WITH_AUTORELEASE_POOL({
        // Test dotimes with environment binding
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Create binding list: [i 3]
        CljObject *binding_list = make_list(intern_symbol_global("i"), make_list(fixnum(3), NULL));
        
        // Create body: i - symbol reference
        CljObject *body = intern_symbol_global("i");
        
        // Create function call: (dotimes [i 3] i)
        CljObject *dotimes_call = make_list(intern_symbol_global("dotimes"), make_list((CljObject*)binding_list, make_list(body, NULL)));
        
        // Create a simple environment
        CljMap *env = make_map_old(4);
        
        // Test dotimes evaluation with environment
        CljObject *result = eval_dotimes((CljObject*)dotimes_call, env);
        TEST_ASSERT_TRUE(result == NULL);
        
        // Clean up environment
        RETAIN(env);
        RELEASE(env);
        
        // Clean up
        evalstate_free(eval_state);
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)dotimes_call);
    });
}

void test_doseq_with_environment(void) {
    // Use WITH_AUTORELEASE_POOL for eval_doseq which uses autorelease()
    WITH_AUTORELEASE_POOL({
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
        CljObject *binding_list = make_list(intern_symbol_global("x"), make_list(vec, NULL));
        
        // Create body: x - symbol reference
        CljObject *body = intern_symbol_global("x");
        
        // Create function call: (doseq [x [1 2 3]] x)
        CljObject *doseq_call = make_list(intern_symbol_global("doseq"), make_list((CljObject*)binding_list, make_list(body, NULL)));
        
        // Create a simple environment
        CljMap *env = make_map_old(4);
        
        // Test doseq evaluation with environment
        CljObject *result = eval_doseq((CljObject*)doseq_call, env);
        TEST_ASSERT_TRUE(result == NULL);
        
        // Clean up environment
        RETAIN(env);
        RELEASE(env);
        
        // Clean up
        evalstate_free(eval_state);
        RELEASE((CljObject*)binding_list);
        RELEASE(body);
        RELEASE((CljObject*)doseq_call);
    });
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
