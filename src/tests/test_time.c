#include "tests_common.h"
#include "object.h"
#include "list.h"
#include "value.h"
#include "clj_strings.h"
#include "types.h"

// Forward declaration
int load_clojure_core(EvalState *st);
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
    
    ID args[1];
    args[0] = expr;
    
    // Call native_time
    CljObject *result = native_time(args, 1);

    // The result should be 3
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    
    // Clean up
    RELEASE(expr);
    RELEASE(result);
}

TEST(test_time_arity_validation) {
    // Test that time function validates arity correctly
    ID args[0];
    
    // This should throw an exception for insufficient arguments
    TRY {
        CljObject *result = native_time(args, 0);
        TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    } CATCH(ex) {
        // Exception is expected - test passes
        TEST_ASSERT_TRUE(true);
    } END_TRY
}

TEST(test_time_with_too_many_arguments) {
    // Test time with too many arguments
    CljObject *expr1 = fixnum(1);
    CljObject *expr2 = fixnum(2);
    
    ID args[2];
    args[0] = expr1;
    args[1] = expr2;
    
    // This should throw an exception for too many arguments
    TRY {
        CljObject *result = native_time(args, 2);
        TEST_ASSERT_TRUE(result == NULL); // Should return NULL after exception
    } CATCH(ex) {
        // Exception is expected - test passes
        TEST_ASSERT_TRUE(true);
    } END_TRY
    
    // Clean up
    RELEASE(expr1);
    RELEASE(expr2);
}

TEST(test_time_with_sleep) {
    // Test time function with sleep to get measurable timing
    // Create a sleep expression: (sleep 1) - sleep for 1 second
    CljObject *sleep_symbol = intern_symbol_global("sleep");
    CljObject *one_second = fixnum(1);
    
    // Create the expression: (sleep 1)
    CljObject *expr = make_list((ID)sleep_symbol, (CljList*)make_list((ID)one_second, NULL));
    
    ID args[1];
    args[0] = expr;
    
    // Call native_time - this should take approximately 1000ms
    CljObject *result = native_time(args, 1);
    
    // The result should be nil (sleep returns nil)
    TEST_ASSERT_TRUE(result == NULL); // nil is NULL in our system
    
    // Clean up
    RELEASE(expr);
    // Don't release result since it's NULL (nil)
}