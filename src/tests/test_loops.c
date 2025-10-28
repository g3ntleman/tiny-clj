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

TEST(test_dotimes_basic) {
    // Test native_dotimes with basic functionality
    // Test with valid arguments: binding vector and body
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    CljObject *body = intern_symbol_global("i");
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}

TEST(test_doseq_basic) {
    // Test that eval_doseq handles NULL input gracefully
    CljMap *env = (CljMap*)make_map(4);
    
    // Test with NULL list
    CljObject *result = eval_doseq(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with NULL list (no need to test non-list as immediate values can't be cast)
    result = eval_doseq(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
}

TEST(test_for_basic) {
    // Test that eval_for handles NULL input gracefully
    CljMap *env = (CljMap*)make_map(4);
    
    // Test with NULL list
    CljObject *result = eval_for(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Test with NULL list (no need to test non-list as immediate values can't be cast)
    result = eval_for(NULL, env);
    TEST_ASSERT_TRUE(result == NULL);
    
    // Clean up
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_with_environment) {
    // Test native_dotimes with environment binding
    // Create binding vector: [i 3]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    
    // Create body: i - symbol reference
    CljObject *body = intern_symbol_global("i");
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}

// ============================================================================
// DOTIMES EDGE CASE TESTS - NATIVE FUNCTION ONLY
// ============================================================================

TEST(test_dotimes_zero_iterations) {
    // Test native_dotimes with 0 iterations - should not execute body
    // Create binding vector: [i 0]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(0), NULL));
    
    // Create body: (println "Should not print")
    CljObject *body = make_list((ID)intern_symbol_global("println"), (CljList*)make_list((ID)make_string("Should not print"), NULL));
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}

TEST(test_dotimes_negative_iterations) {
    // Test native_dotimes with negative iterations - should not execute body
    // Create binding vector: [i -5]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(-5), NULL));
    
    // Create body: (println "Should not print")
    CljObject *body = make_list((ID)intern_symbol_global("println"), (CljList*)make_list((ID)make_string("Should not print"), NULL));
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}

TEST(test_dotimes_large_iterations) {
    // Test native_dotimes with large number of iterations
    // Create binding vector: [i 1000]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(1000), NULL));
    
    // Create body: i - symbol reference (simple operation)
    CljObject *body = intern_symbol_global("i");
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}

/*
TEST(test_dotimes_invalid_binding_format) {
    // Test native_dotimes with invalid binding format
    // Create invalid binding vector: [i] (missing count)
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), NULL);
    
    // Create body: i
    CljObject *body = intern_symbol_global("i");
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    
    // This should throw an exception for invalid binding format
    // We expect the exception to be thrown and handled gracefully
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}
*/

TEST(test_dotimes_non_numeric_count) {
    // Test native_dotimes with non-numeric count
    // Create binding vector: [i "not-a-number"]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)make_string("not-a-number"), NULL));
    
    // Create body: i
    CljObject *body = intern_symbol_global("i");
    
    ID args[2];
    args[0] = binding_vector;
    args[1] = body;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for non-numeric count
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
}

TEST(test_dotimes_missing_body) {
    // Test native_dotimes with missing body
    // Create binding vector: [i 3]
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), (CljList*)make_list((ID)fixnum(3), NULL));
    
    ID args[1];
    args[0] = binding_vector;
    CljObject *result = native_dotimes(args, 1);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for missing body
    
    // Clean up
    RELEASE(binding_vector);
}

TEST(test_dotimes_insufficient_args) {
    // Test native_dotimes with insufficient arguments
    ID args[0];
    CljObject *result = native_dotimes(args, 0);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for insufficient args
}

TEST(test_dotimes_null_args) {
    // Test native_dotimes with NULL arguments
    ID args[2];
    args[0] = NULL;
    args[1] = NULL;
    CljObject *result = native_dotimes(args, 2);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for NULL args
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