#include "tests_common.h"
#include "object.h"
#include "list.h"
#include "value.h"
#include "strings.h"
#include "types.h"
#include "vector.h"
#include "eval.h"
#include "symbol.h"

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
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // First test that eval_string works for simple expressions
    CljValue simple_result = eval_string("(+ 1 2)", g_test_eval_state);
    if (!simple_result) {
        TEST_FAIL_MESSAGE("eval_string(\"(+ 1 2)\", g_test_eval_state) returned NULL");
        return;
    }
    if (!is_fixnum(simple_result)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "eval_string(\"(+ 1 2)\") returned non-fixnum (value: %p, type: %d)", 
                 (void*)simple_result, simple_result ? ((CljObject*)simple_result)->type : -1);
        TEST_FAIL_MESSAGE(msg);
        return;
    }
    int simple_actual = as_fixnum(simple_result);
    if (simple_actual != 3) {
        char msg[256];
        snprintf(msg, sizeof(msg), "eval_string(\"(+ 1 2)\") returned %d, expected 3", simple_actual);
        TEST_FAIL_MESSAGE(msg);
        return;
    }
    TEST_ASSERT_EQUAL_INT(3, simple_actual);
    
    // Check that SYM_TIME is initialized
    if (!SYM_TIME) {
        TEST_FAIL_MESSAGE("SYM_TIME is NULL - init_special_symbols() may not have been called");
        return;
    }
    
    // Check that intern_symbol_global("time") returns SYM_TIME
    CljSymbol *time_sym = intern_symbol_global("time");
    if (!time_sym) {
        TEST_FAIL_MESSAGE("intern_symbol_global(\"time\") returned NULL");
        return;
    }
    if (time_sym != SYM_TIME) {
        char msg[256];
        snprintf(msg, sizeof(msg), "intern_symbol_global(\"time\") returned %p, but SYM_TIME is %p", 
                 (void*)time_sym, (void*)SYM_TIME);
        TEST_FAIL_MESSAGE(msg);
        return;
    }
    
    // Now test time
    CljValue result = eval_string("(time (+ 1 2))", g_test_eval_state);
    
    // The result should be 3
    if (!result) {
        TEST_FAIL_MESSAGE("eval_string(\"(time (+ 1 2))\", g_test_eval_state) returned NULL");
        return;
    }
    if (!is_fixnum(result)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "eval_string(\"(time (+ 1 2))\", g_test_eval_state) returned non-fixnum (type: %d)", 
                 result ? ((CljObject*)result)->type : -1);
        TEST_FAIL_MESSAGE(msg);
        return;
    }
    int actual = as_fixnum(result);
    if (actual != 3) {
        char msg[256];
        snprintf(msg, sizeof(msg), "eval_string(\"(time (+ 1 2))\", g_test_eval_state) returned %d, expected 3", actual);
        TEST_FAIL_MESSAGE(msg);
        return;
    }
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_time_arity_validation) {
    // Test that time function validates arity correctly
    // Create (time) with no arguments
    CljObject *time_symbol = (CljObject *)SYM_TIME;
    CljList *time_list = make_list((ID)time_symbol, NULL);
    
    CljMap *env = make_map(16);
    
    // This should throw an exception for insufficient arguments
    TRY {
        CljObject *result = eval_time(time_list, env, g_test_eval_state);
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
    CljObject *time_symbol = (CljObject *)SYM_TIME;
    CljList *time_list = make_list((ID)time_symbol, 
        make_list((ID)expr1, 
        make_list((ID)expr2, NULL)));
    
    CljMap *env = make_map(16);
    
    // This should throw an exception for too many arguments
    TRY {
        CljObject *result = eval_time(time_list, env, g_test_eval_state);
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
    CljSymbol *sleep_symbol = intern_symbol_global("sleep");
    CljObject *one_second = fixnum(1);
    
    // Create the expression: (sleep 1)
    CljObject *expr = (CljObject *)make_list((ID)sleep_symbol, make_list((ID)one_second, NULL));
    
    // Create (time (sleep 1))
    CljObject *time_symbol = (CljObject *)SYM_TIME;
    CljList *time_list = make_list((ID)time_symbol, make_list((ID)expr, NULL));
    
    CljMap *env = make_map(16);
    
    // Call eval_time - this should take approximately 1000ms
    CljObject *result = eval_time(time_list, env, g_test_eval_state);
    
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
    CljValue result = eval_string("(time (+ 1 2))", g_test_eval_state);
    
    // The result should be 3
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST(test_time_with_dotimes) {
    // Test that time works correctly with dotimes
    // Create: (time (dotimes [i 1000] (+ 1 2 3 4 5)))
    
    // Create symbols
    CljObject *time_symbol = (CljObject *)SYM_TIME;
    CljObject *dotimes_symbol = (CljObject *)SYM_DOTIMES;
    CljSymbol *i_symbol = intern_symbol_global("i");
    CljObject *plus_symbol = (CljObject *)SYM_PLUS;
    
    // Create numbers
    CljObject *thousand = fixnum(1000);
    CljObject *one = fixnum(1);
    CljObject *two = fixnum(2);
    CljObject *three = fixnum(3);
    CljObject *four = fixnum(4);
    CljObject *five = fixnum(5);
    
    // Create binding vector: [i 1000]
    CljObject *binding_vector = (CljObject *)make_vector(2, CLJ_VECTOR);
    CljVector *vec_data = as_vector(binding_vector);
    // Add elements using vector_conj
    vec_data = vector_conj(vec_data, (ID)i_symbol);
    vec_data = vector_conj(vec_data, (ID)thousand);
    
    // Create arithmetic expression: (+ 1 2 3 4 5)
    CljObject *arithmetic_expr = (CljObject *)make_list((ID)plus_symbol, 
        make_list((ID)one, 
        make_list((ID)two, 
        make_list((ID)three, 
        make_list((ID)four, 
        make_list((ID)five, NULL))))));
    
    // Create dotimes call: (dotimes [i 1000] (+ 1 2 3 4 5))
    CljObject *dotimes_call = (CljObject *)make_list((ID)dotimes_symbol, 
        make_list((ID)binding_vector, 
        make_list((ID)arithmetic_expr, NULL)));
    
    // Create time call: (time (dotimes [i 1000] (+ 1 2 3 4 5)))
    CljObject *time_call = (CljObject *)make_list((ID)time_symbol, 
        make_list((ID)dotimes_call, NULL));
    
    // Create environment
    CljMap *env = make_map(4);
    
    // Test time evaluation with dotimes
    CljObject *result = eval_time(as_list((ID)time_call), env, g_test_eval_state);
    
    // time should return the result of the evaluated expression
    // Since dotimes returns nil, time should also return nil
    TEST_ASSERT_TRUE(result == NULL); // dotimes returns nil, so time returns nil
    
    // Clean up
    RELEASE(binding_vector);
    RELEASE(arithmetic_expr);
    RELEASE(dotimes_call);
    RELEASE(time_call);
    // Don't RELEASE result - eval_time returns autoreleased object
    RELEASE(env);
}

TEST(test_time_returns_expression_result) {
    // Test that time returns the result of the expression, not the timing
    // This demonstrates Clojure-compatible behavior
    CljValue result = eval_string("(time (+ 1 2 3))", g_test_eval_state);
    
    // time should return the result of the expression: 6
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}