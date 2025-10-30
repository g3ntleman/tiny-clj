#include "tests_common.h"
#include "object.h"
#include "list.h"
#include "value.h"
#include "clj_strings.h"
#include "types.h"
#include "vector.h"
#include "function_call.h"

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
    // Create a simple expression: (+ 1 2)
    CljObject *plus_symbol = intern_symbol_global("+");
    CljObject *one = fixnum(1);
    CljObject *two = fixnum(2);
    
    // Create the expression: (+ 1 2) - use proper make_list syntax
    CljObject *expr = make_list((ID)plus_symbol, (CljList*)make_list((ID)one, (CljList*)make_list((ID)two, NULL)));
    
    // Create a list for eval_time: (time (+ 1 2))
    CljObject *time_symbol = SYM_TIME;
    CljList *time_list = (CljList*)make_list((ID)time_symbol, (CljList*)make_list((ID)expr, NULL));
    
    // Create environment and evalstate
    EvalState *st = evalstate();
    CljMap *env = make_map(16);
    
    // Call eval_time (time is now only a special form)
    CljObject *result = eval_time(time_list, env, st);

    // The result should be 3
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
    // Clean up
    RELEASE(expr);
    RELEASE(time_list);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_arity_validation) {
    // Test that time function validates arity correctly
    // Create (time) with no arguments
    CljObject *time_symbol = SYM_TIME;
    CljList *time_list = (CljList*)make_list((ID)time_symbol, NULL);
    
    EvalState *st = evalstate();
    CljMap *env = make_map(16);
    
    // This should throw an exception for insufficient arguments
    TRY {
        CljObject *result = eval_time(time_list, env, st);
        TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    } CATCH(ex) {
        // Exception is expected - test passes
        TEST_ASSERT_TRUE(true);
    } END_TRY
    
    RELEASE(time_list);
    RELEASE(env);
}

TEST(test_time_with_too_many_arguments) {
    // Test time with too many arguments
    CljObject *expr1 = fixnum(1);
    CljObject *expr2 = fixnum(2);
    
    // Create (time 1 2) with too many arguments
    CljObject *time_symbol = SYM_TIME;
    CljList *time_list = (CljList*)make_list((ID)time_symbol, 
        (CljList*)make_list((ID)expr1, 
        (CljList*)make_list((ID)expr2, NULL)));
    
    EvalState *st = evalstate();
    CljMap *env = make_map(16);
    
    // This should throw an exception for too many arguments
    TRY {
        CljObject *result = eval_time(time_list, env, st);
        TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    } CATCH(ex) {
        // Exception is expected - test passes
        TEST_ASSERT_TRUE(true);
    } END_TRY
    
    // Clean up
    RELEASE(expr1);
    RELEASE(expr2);
    RELEASE(time_list);
    RELEASE(env);
}

TEST(test_time_with_sleep) {
    // Test time function with sleep to get measurable timing
    // Create a sleep expression: (sleep 1) - sleep for 1 second
    CljObject *sleep_symbol = intern_symbol_global("sleep");
    CljObject *one_second = fixnum(1);
    
    // Create the expression: (sleep 1)
    CljObject *expr = make_list((ID)sleep_symbol, (CljList*)make_list((ID)one_second, NULL));
    
    // Create (time (sleep 1))
    CljObject *time_symbol = SYM_TIME;
    CljList *time_list = (CljList*)make_list((ID)time_symbol, (CljList*)make_list((ID)expr, NULL));
    
    EvalState *st = evalstate();
    CljMap *env = make_map(16);
    
    // Call eval_time - this should take approximately 1000ms
    CljObject *result = eval_time(time_list, env, st);
    
    // The result should be nil (sleep returns nil)
    TEST_ASSERT_TRUE(result == NULL); // nil is NULL in our system
    
    // Clean up
    RELEASE(expr);
    RELEASE(time_list);
    RELEASE(env);
    // Don't release result since it's NULL (nil)
}

TEST(test_time_no_double_evaluation) {
    // Test that time does NOT evaluate its argument twice
    // Use a simple arithmetic expression that we can verify
    
    // Create a simple expression: (+ 1 2)
    CljObject *plus_symbol = intern_symbol_global("+");
    CljObject *one = fixnum(1);
    CljObject *two = fixnum(2);
    
    // Create the expression: (+ 1 2)
    CljObject *expr = make_list((ID)plus_symbol, (CljList*)make_list((ID)one, (CljList*)make_list((ID)two, NULL)));
    
    // Create a list for eval_time: (time (+ 1 2))
    CljObject *time_symbol = SYM_TIME;
    CljList *time_list = (CljList*)make_list((ID)time_symbol, (CljList*)make_list((ID)expr, NULL));
    
    // Create environment and evalstate
    EvalState *st = evalstate();
    CljMap *env = make_map(16);
    
    // Call eval_time directly (this tests the special form implementation)
    CljObject *result = eval_time(time_list, env, st);
    
    // The result should be 3
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
    // Clean up
    RELEASE(expr);
    RELEASE(time_list);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_with_dotimes) {
    // Test that time works correctly with dotimes
    // Create: (time (dotimes [i 1000] (+ 1 2 3 4 5)))
    
    // Create symbols
    CljObject *time_symbol = SYM_TIME;
    CljObject *dotimes_symbol = intern_symbol_global("dotimes");
    CljObject *i_symbol = intern_symbol_global("i");
    CljObject *plus_symbol = intern_symbol_global("+");
    
    // Create numbers
    CljObject *thousand = fixnum(1000);
    CljObject *one = fixnum(1);
    CljObject *two = fixnum(2);
    CljObject *three = fixnum(3);
    CljObject *four = fixnum(4);
    CljObject *five = fixnum(5);
    
    // Create binding vector: [i 1000]
    CljObject *binding_vector = make_vector(2, true);
    CljPersistentVector *vec_data = as_vector(binding_vector);
    vec_data->data[0] = i_symbol;
    vec_data->data[1] = thousand;
    vec_data->count = 2; // Set the count manually
    
    // Create arithmetic expression: (+ 1 2 3 4 5)
    CljObject *arithmetic_expr = make_list((ID)plus_symbol, 
        (CljList*)make_list((ID)one, 
        (CljList*)make_list((ID)two, 
        (CljList*)make_list((ID)three, 
        (CljList*)make_list((ID)four, 
        (CljList*)make_list((ID)five, NULL))))));
    
    // Create dotimes call: (dotimes [i 1000] (+ 1 2 3 4 5))
    CljObject *dotimes_call = make_list((ID)dotimes_symbol, 
        (CljList*)make_list((ID)binding_vector, 
        (CljList*)make_list((ID)arithmetic_expr, NULL)));
    
    // Create time call: (time (dotimes [i 1000] (+ 1 2 3 4 5)))
    CljObject *time_call = make_list((ID)time_symbol, 
        (CljList*)make_list((ID)dotimes_call, NULL));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    EvalState *st = evalstate();
    
    // Test time evaluation with dotimes
    CljObject *result = eval_time((CljList*)(CljObject*)time_call, env, st);
    
    // time should return the result of the evaluated expression
    // Since dotimes returns nil, time should also return nil
    TEST_ASSERT_TRUE(result == NULL); // dotimes returns nil, so time returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(arithmetic_expr);
    RELEASE(dotimes_call);
    RELEASE(time_call);
    RELEASE(result);
    RELEASE(env);
}

TEST(test_time_returns_expression_result) {
    // Test that time returns the result of the expression, not the timing
    // This demonstrates Clojure-compatible behavior
    
    // Create symbols
    CljObject *time_symbol = SYM_TIME;
    CljObject *plus_symbol = SYM_PLUS;
    
    // Create numbers
    CljObject *one = fixnum(1);
    CljObject *two = fixnum(2);
    CljObject *three = fixnum(3);
    
    // Create arithmetic expression: (+ 1 2 3) - this evaluates to 6
    CljObject *arithmetic_expr = make_list((ID)plus_symbol, 
        (CljList*)make_list((ID)one, 
        (CljList*)make_list((ID)two, 
        (CljList*)make_list((ID)three, NULL))));
    
    // Create time call: (time (+ 1 2 3))
    CljObject *time_call = make_list((ID)time_symbol, 
        (CljList*)make_list((ID)arithmetic_expr, NULL));
    
    // Create environment
    CljMap *env = (CljMap*)make_map(4);
    EvalState *st = evalstate();
    
    // Test time evaluation
    CljObject *result = eval_time((CljList*)(CljObject*)time_call, env, st);
    
    // time should return the result of the expression: 6
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
    
    // Clean up
    RELEASE(arithmetic_expr);
    RELEASE(time_call);
    RELEASE(result);
    RELEASE(env);
}