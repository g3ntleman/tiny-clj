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
    CljPersistentMap *env = make_map(4);
    CljObject *result = eval_doseq(NULL, env, g_test_eval_state, NULL);
    TEST_ASSERT_NULL(result);
    RELEASE(env);
}

// test_for_basic removed - 'for' is now a macro that expands to 'for*'
// Use test_for_basic_list_comprehension in test_loops.c instead

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
    CljPersistentMap *env = AUTORELEASE(make_map(4));
    
    // Test dotimes evaluation
    CljObject *result = eval_dotimes(as_list(dotimes_call), env, g_test_eval_state, NULL);
    TEST_ASSERT_TRUE(result == NULL); // Should return NULL for missing body
}

// test_dotimes_simple_iteration_count removed - duplicate in test_loops.c

TEST(test_doseq_with_environment) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new(false);
        TEST_ASSERT_NOT_NULL(eval_state);
        
        CljPersistentVector *vec = make_vector(0, false);
        TEST_ASSERT_NOT_NULL(vec);
        vec = vector_conj(vec, fixnum(1));
        vec = vector_conj(vec, fixnum(2));
        vec = vector_conj(vec, fixnum(3));
        
        ID sym_x = (ID)intern_symbol_global("x");
        ID binding_list = (ID)make_list(sym_x, make_list((ID)vec, NULL));
        ID doseq_call = (ID)make_list(SYM_DOSEQ, make_list(binding_list, make_list(sym_x, NULL)));
        RELEASE(vec);
        CljPersistentMap *env = make_map(4);
        
        CljObject *result = eval_doseq(as_list(doseq_call), env, eval_state, NULL);
        TEST_ASSERT_NULL(result);
        
        RELEASE(env);
        evalstate_free(eval_state);
        RELEASE(binding_list);
        RELEASE(doseq_call);
    });
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests
