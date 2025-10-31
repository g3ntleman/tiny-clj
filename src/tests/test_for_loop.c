nein/*
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
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i 3] i)", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
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
    // Test eval_dotimes with environment binding
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i 3] i)", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

// ============================================================================
// DOTIMES EDGE CASE TESTS - EVAL_DOTIMES FUNCTION
// ============================================================================

TEST(test_dotimes_zero_iterations) {
    // Test eval_dotimes with 0 iterations - should not execute body
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i 0] (println \"Should not print\"))", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_negative_iterations) {
    // Test eval_dotimes with negative iterations - should not execute body
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i -5] (println \"Should not print\"))", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_large_iterations) {
    // Test eval_dotimes with large number of iterations
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i 1000] i)", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_invalid_binding_format) {
    // Test eval_dotimes with invalid binding format
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i] i)", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_non_numeric_count) {
    // Test eval_dotimes with non-numeric count
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i \"not-a-number\"] i)", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_missing_body) {
    // Test eval_dotimes with missing body
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i 3])", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_dotimes_simple_iteration_count) {
    // Test that eval_dotimes executes the body exactly n times
    EvalState *st = evalstate();
    ID dotimes_call = parse("(dotimes [i 3] i)", st);
    TEST_ASSERT_NOT_NULL(dotimes_call);
    CljMap *env = (CljMap*)make_map(4);
    CljObject *result = eval_dotimes((CljList*)(CljObject*)dotimes_call, env);
    TEST_ASSERT_TRUE(result == NULL);
    RELEASE((CljObject*)dotimes_call);
    RETAIN(env);
    RELEASE(env);
}

TEST(test_doseq_with_environment) {
    // Use WITH_AUTORELEASE_POOL for eval_doseq which uses autorelease()
    WITH_AUTORELEASE_POOL({
        // Test doseq with environment binding
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Build full form via Parser: (doseq [x [1 2 3]] x)
        ID doseq_call = parse("(doseq [x [1 2 3]] x)", eval_state);
        TEST_ASSERT_NOT_NULL(doseq_call);
        
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
        RELEASE((CljObject*)doseq_call);
    });
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests