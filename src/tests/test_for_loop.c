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

// test_dotimes_basic removed - duplicate of test_dotimes_simple_iteration_count in test_loops.c

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

// test_dotimes_with_environment removed - duplicate of test_dotimes_simple_iteration_count in test_loops.c

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================
// Note: Most dotimes edge case tests are in test_loops.c (using AUTORELEASE)
// Only unique tests are kept here

TEST(test_dotimes_missing_body) {
    // Test eval_dotimes with missing body - unique test not in test_loops.c
    // Create dotimes call: (dotimes [i 3]) - missing body
    CljObject *binding_vector = AUTORELEASE(make_list(intern_symbol_global("i"), (CljList*)make_list(fixnum(3), NULL)));
    CljObject *dotimes_call = AUTORELEASE(make_list(SYM_DOTIMES, (CljList*)make_list(binding_vector, NULL)));
    
    // Create environment
    CljMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for missing body
}

// test_dotimes_simple_iteration_count removed - duplicate in test_loops.c

TEST(test_doseq_with_environment) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new(false);
        TEST_ASSERT_NOT_NULL(eval_state);
        
        CljValue vec = make_vector(3, CLJ_VECTOR_PERSISTENT);
        CljPersistentVector *vec_data = as_vector(vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->data[2] = fixnum(3);
        vec_data->count = 3;
        
        CljObject *binding_list = make_list(intern_symbol_global("x"), make_list(vec, NULL));
        CljSymbol *body = intern_symbol_global("x");
        CljObject *doseq_call = make_list(SYM_DOSEQ, make_list(binding_list, make_list(body, NULL)));
        CljMap *env = make_map(4);
        
        CljObject *result = eval_doseq(as_list(doseq_call), env);
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