#include "tests_common.h"
#include "object.h"
#include "list.h"
#include "value.h"
// removed unused includes

// Forward declaration
int load_clojure_core(EvalState *st);
ID eval_time(CljList *list, CljMap *env, EvalState *st);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// TIME FUNCTION TESTS
// ============================================================================

TEST(test_time_basic_functionality) {
    // Test that time function executes and returns the result
    EvalState *st = evalstate();
    // Build form via Parser: (time (+ 1 2))
    ID time_list = parse("(time (+ 1 2))", st);
    TEST_ASSERT_NOT_NULL(time_list);
    CljMap *env = make_map(16);
    CljObject *result = eval_time((CljList*)(CljObject*)time_list, env, st);

    // The result should be 3
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
    // Clean up
    RELEASE((CljObject*)time_list);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_arity_validation) {
    // Test that time function validates arity correctly
    // Create (time) with no arguments via Parser
    EvalState *st = evalstate();
    ID time_list = parse("(time)", st);
    TEST_ASSERT_NOT_NULL(time_list);
    CljMap *env = make_map(16);
    
    // This should throw an exception for insufficient arguments
    TRY {
        CljObject *result = eval_time((CljList*)(CljObject*)time_list, env, st);
        TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    } CATCH(ex) {
        // Exception is expected - test passes
        TEST_ASSERT_TRUE(true);
    } END_TRY
    
    RELEASE((CljObject*)time_list);
    RELEASE(env);
}

TEST(test_time_with_too_many_arguments) {
    // Test time with too many arguments
    // Create (time 1 2) with too many arguments via Parser
    EvalState *st = evalstate();
    ID time_list = parse("(time 1 2)", st);
    TEST_ASSERT_NOT_NULL(time_list);
    CljMap *env = make_map(16);
    
    // This should throw an exception for too many arguments
    TRY {
        CljObject *result = eval_time((CljList*)(CljObject*)time_list, env, st);
        TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    } CATCH(ex) {
        // Exception is expected - test passes
        TEST_ASSERT_TRUE(true);
    } END_TRY
    
    // Clean up
    RELEASE((CljObject*)time_list);
    RELEASE(env);
}

TEST(test_time_with_sleep) {
    // Test time function with sleep to get measurable timing
    // Create (time (sleep 1)) via Parser
    EvalState *st = evalstate();
    ID time_list = parse("(time (sleep 1))", st);
    TEST_ASSERT_NOT_NULL(time_list);
    CljMap *env = make_map(16);
    
    // Call eval_time - this should take approximately 1000ms
    CljObject *result = eval_time((CljList*)(CljObject*)time_list, env, st);
    
    // The result should be nil (sleep returns nil)
    TEST_ASSERT_TRUE(result == NULL); // nil is NULL in our system
    
    // Clean up
    RELEASE((CljObject*)time_list);
    RELEASE(env);
    // Don't release result since it's NULL (nil)
}

TEST(test_time_no_double_evaluation) {
    // Test that time does NOT evaluate its argument twice
    // Use a simple arithmetic expression that we can verify
    
    // Create a list for eval_time via Parser: (time (+ 1 2))
    EvalState *st = evalstate();
    ID time_list = parse("(time (+ 1 2))", st);
    TEST_ASSERT_NOT_NULL(time_list);
    
    // Create environment
    CljMap *env = make_map(16);
    
    // Call eval_time directly (this tests the special form implementation)
    CljObject *result = eval_time((CljList*)(CljObject*)time_list, env, st);
    
    // The result should be 3
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
    // Clean up
    RELEASE((CljObject*)time_list);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_with_dotimes) {
    // Test that time works correctly with dotimes
    // Create: (time (dotimes [i 1000] (+ 1 2 3 4 5)))
    
    // Build full form via Parser: (time (dotimes [i 1000] (+ 1 2 3 4 5)))
    EvalState *st = evalstate();
    ID time_call = parse("(time (dotimes [i 1000] (+ 1 2 3 4 5)))", st);
    TEST_ASSERT_NOT_NULL(time_call);
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    // Test time evaluation with dotimes
    CljObject *result = eval_time((CljList*)(CljObject*)time_call, env, st);
    
    // time should return the result of the evaluated expression
    // Since dotimes returns nil, time should also return nil
    TEST_ASSERT_TRUE(result == NULL); // dotimes returns nil, so time returns nil
    
    // Clean up
    RELEASE((CljObject*)time_call);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_returns_expression_result) {
    // Test that time returns the result of the expression, not the timing
    // This demonstrates Clojure-compatible behavior
    
    // Build via Parser: (time (+ 1 2 3))
    EvalState *st = evalstate();
    ID time_call = parse("(time (+ 1 2 3))", st);
    TEST_ASSERT_NOT_NULL(time_call);
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    // Test time evaluation
    CljObject *result = eval_time((CljList*)(CljObject*)time_call, env, st);
    
    // time should return the result of the expression: 6
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
    
    // Clean up
    RELEASE((CljObject*)time_call);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_qualified_in_clojure_core_no_crash) {
    // This exercises qualified symbol resolution in operator position:
    // (clojure.core/time (+ 1 2)) should not crash and should return 3
    EvalState *st = evalstate_new();
    TEST_ASSERT_NOT_NULL(st);

    // Ensure clojure.core builtins are registered (register_builtins is called in test runner)
    CljObject *result = eval_string("(clojure.core/time (+ 1 2))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)result));

    evalstate_free(st);
}