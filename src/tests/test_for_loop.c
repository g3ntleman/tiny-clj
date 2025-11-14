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
    // Test eval_dotimes with basic functionality
    // Create dotimes call: (dotimes [i 3] i)
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(3), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // dotimes always returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_doseq_basic) {
    CljMap *env = make_map(4);
    CljObject *result = eval_doseq(NULL, env);
    TEST_ASSERT_NULL(result);
    RELEASE(env);
}

TEST(test_for_basic) {
    CljMap *env = make_map(4);
    CljObject *result = eval_for(NULL, env);
    TEST_ASSERT_NULL(result);
    RELEASE(env);
}

TEST(test_dotimes_with_environment) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(3), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================

TEST(test_dotimes_zero_iterations) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(0), NULL));
    CljObject *body = make_list((ID)SYM_PRINTLN, make_list((ID)make_string("Should not print"), NULL));
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_negative_iterations) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(-5), NULL));
    CljObject *body = make_list((ID)SYM_PRINTLN, make_list((ID)make_string("Should not print"), NULL));
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_large_iterations) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(1000), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_invalid_binding_format) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), NULL);
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_non_numeric_count) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)make_string("not-a-number"), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_dotimes_missing_body) {
    // Test eval_dotimes with missing body
    // Create dotimes call: (dotimes [i 3]) - missing body
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(3), NULL));
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, make_list((ID)binding_vector, NULL));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for missing body
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_simple_iteration_count) {
    CljObject *binding_vector = make_list((ID)intern_symbol_global("i"), make_list((ID)fixnum(3), NULL));
    CljSymbol *body = intern_symbol_global("i");
    CljObject *dotimes_call = make_list((ID)SYM_DOTIMES, 
                                       make_list((ID)binding_vector, 
                                                make_list((ID)body, NULL)));
    CljMap *env = make_map(4);
    
    CljObject *result = eval_dotimes(as_list((ID)dotimes_call), env);
    TEST_ASSERT_NULL(result);
    
    RELEASE(binding_vector);
    RELEASE(body);
    RELEASE(dotimes_call);
    RELEASE(env);
}

TEST(test_doseq_with_environment) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new(false);
        TEST_ASSERT_NOT_NULL(eval_state);
        
        CljValue vec = make_vector(3, CLJ_VECTOR);
        CljPersistentVector *vec_data = as_vector(vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->data[2] = fixnum(3);
        vec_data->count = 3;
        
        CljObject *binding_list = make_list((ID)intern_symbol_global("x"), make_list((ID)vec, NULL));
        CljSymbol *body = intern_symbol_global("x");
        CljObject *doseq_call = make_list((ID)SYM_DOSEQ, make_list((ID)binding_list, make_list((ID)body, NULL)));
        CljMap *env = make_map(4);
        
        CljObject *result = eval_doseq(as_list((ID)doseq_call), env);
        TEST_ASSERT_NULL(result);
        
        RELEASE(env);
        evalstate_free(eval_state);
        RELEASE(binding_list);
        RELEASE(body);
        RELEASE(doseq_call);
    });
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests