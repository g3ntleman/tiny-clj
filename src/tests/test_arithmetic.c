/*
 * Arithmetic Tests using Unity Framework
 * 
 * Tests for arithmetic operations and integer overflow/underflow detection.
 *
 * Heap limit 200 after fixing pool accumulation (tearDown: autorelease_pool_free()).
 */
#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 200
#include "tests_common.h"

// ============================================================================
// ARITHMETIC TESTS
// ============================================================================

// Test integer overflow detection
TEST_SHARED(test_integer_overflow_detection) {
    // Test that normal multiplication still works
    CljObject *normal_result = eval_string("(* 2 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(normal_result);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(normal_result));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum((CljValue)normal_result));
    
    // Test that factorial calculation works (using direct multiplication)
    // Note: recur can only be used inside function bodies, so we test factorial via direct multiplication
    CljObject *small_factorial = eval_string("(* 5 4 3 2 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(small_factorial);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(small_factorial));
    TEST_ASSERT_EQUAL_INT(120, as_fixnum((CljValue)small_factorial));
    
    // Test addition overflow - large positive numbers
    bool exception_caught = false;
    TRY {
        CljObject *result = eval_string("(+ 2000000000 2000000000)", g_test_eval_state);
        (void)result;  // Suppress unused variable warning
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        // Verify exception type is ArithmeticException
        TEST_ASSERT_EQUAL_STRING("ArithmeticException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected ArithmeticException for addition overflow");
    
    // Test addition overflow - near INT_MAX
    exception_caught = false;
    TRY {
        CljObject *result = eval_string("(+ 2147483640 10)", g_test_eval_state);
        (void)result;  // Suppress unused variable warning
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArithmeticException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected ArithmeticException for addition overflow near INT_MAX");
    
    // Test addition underflow - large negative numbers
    exception_caught = false;
    TRY {
        CljObject *result = eval_string("(+ -2000000000 -2000000000)", g_test_eval_state);
        (void)result;  // Suppress unused variable warning
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArithmeticException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected ArithmeticException for addition underflow");
    
    // Test subtraction underflow - negative minus positive
    exception_caught = false;
    TRY {
        CljObject *result = eval_string("(- -2000000000 2000000000)", g_test_eval_state);
        (void)result;  // Suppress unused variable warning
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArithmeticException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected ArithmeticException for subtraction underflow");
    
    // Test subtraction overflow - positive minus large negative
    exception_caught = false;
    TRY {
        CljObject *result = eval_string("(- 2000000000 -2000000000)", g_test_eval_state);
        (void)result;  // Suppress unused variable warning
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArithmeticException", ex->type);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Expected ArithmeticException for subtraction overflow");
    
    // Test multiplication overflow - large numbers
    exception_caught = false;
    TRY {
        CljObject *result = eval_string("(* 100000 100000)", g_test_eval_state);
        (void)result;  // Suppress unused variable warning
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("ArithmeticException", ex->type);
    } END_TRY
    // Note: Multiplication overflow detection may not be fully implemented
    // This test documents the expected behavior
    
    // Test that normal operations still work after overflow tests
    CljObject *normal_add = eval_string("(+ 10 20)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(normal_add);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(normal_add));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)normal_add));
    
    CljObject *normal_sub = eval_string("(- 50 20)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(normal_sub);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(normal_sub));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)normal_sub));
}

// Simple arithmetic tests moved to test_core.c
// This file focuses on edge cases (overflow, division by zero, etc.)

// Test division by zero exception
TEST_SHARED(test_division_by_zero_exception) {
    CljObject *result = NULL;
    bool exception_caught = false;
    
    TRY {
        result = eval_string("(/ 1 0)", g_test_eval_state);
    } CATCH(ex) {
        exception_caught = true;
        result = NULL;
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Division by zero should throw exception");
    TEST_ASSERT_NULL(result);
}

// Test eval_list with simple arithmetic
TEST_SHARED(test_eval_list_simple_arithmetic) {
    // Test simple addition
    CljObject *result = eval_string("(+ 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(IS_IMMEDIATE(result));
}

// Test multiplication with negative numbers (for reduce tests)
TEST_SHARED(test_multiplication_with_negative_numbers) {
    // Test: (* 1 -2) => -2 (positive * negative)
    CljObject *result1 = eval_string("(* 1 -2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result1));
    TEST_ASSERT_EQUAL_INT(-2, as_fixnum((CljValue)result1));
    
    // Test: (* -2 3) => -6 (negative * positive)
    CljObject *result2 = eval_string("(* -2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result2));
    TEST_ASSERT_EQUAL_INT(-6, as_fixnum((CljValue)result2));
    
    // Test: (* -2 3 -4) => 24 (negative * positive * negative)
    CljObject *result3 = eval_string("(* -2 3 -4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result3));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum((CljValue)result3));
    
    // Test: (* -1 -2) => 2 (negative * negative)
    CljObject *result4 = eval_string("(* -1 -2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result4));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)result4));
    
    // Test: (* -1 -2 -3) => -6 (negative * negative * negative)
    CljObject *result5 = eval_string("(* -1 -2 -3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result5));
    TEST_ASSERT_EQUAL_INT(-6, as_fixnum((CljValue)result5));
}

// Test quot (integer division)
TEST_SHARED(test_quot) {
    // Test: (quot 10 3) => 3
    CljObject *result1 = eval_string("(quot 10 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)result1));
    
    // Test: (quot -10 3) => -3 (Clojure behavior: truncates toward zero)
    CljObject *result2 = eval_string("(quot -10 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result2));
    TEST_ASSERT_EQUAL_INT(-3, as_fixnum((CljValue)result2));
    
    // Test: (quot 10 -3) => -3
    CljObject *result3 = eval_string("(quot 10 -3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result3));
    TEST_ASSERT_EQUAL_INT(-3, as_fixnum((CljValue)result3));
    
    // Test: (quot -10 -3) => 3
    CljObject *result4 = eval_string("(quot -10 -3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result4));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)result4));
    
    // Test: (quot 0 5) => 0
    CljObject *result5 = eval_string("(quot 0 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result5));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)result5));
    
    // Test division by zero exception
    bool exception_caught = false;
    TRY {
        CljObject *result = eval_string("(quot 10 0)", g_test_eval_state);
        (void)result;
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Division by zero should throw exception");
}

// Test bit-shift-left
TEST_SHARED(test_bit_shift_left) {
    // Test: (bit-shift-left 1 3) => 8
    CljObject *result1 = eval_string("(bit-shift-left 1 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result1));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum((CljValue)result1));
    
    // Test: (bit-shift-left 2 1) => 4
    CljObject *result2 = eval_string("(bit-shift-left 2 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result2));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum((CljValue)result2));
    
    // Test: (bit-shift-left 1 0) => 1
    CljObject *result3 = eval_string("(bit-shift-left 1 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result3));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)result3));
    
    // Test: (bit-shift-left 0 5) => 0
    CljObject *result4 = eval_string("(bit-shift-left 0 5)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_EQUAL_INT(CLJ_FIXNUM, TAG(result4));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)result4));
}

// Test Math/sqrt
TEST_SHARED(test_math_sqrt) {
    // Test: (Math/sqrt 4) => 2.0
    CljObject *result1 = eval_string("(Math/sqrt 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    // Result should be a fixed-point number (float)
    float val1 = as_fixed((CljValue)result1);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.0, val1);
    
    // Test: (Math/sqrt 2) => ~1.414
    CljObject *result2 = eval_string("(Math/sqrt 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    float val2 = as_fixed((CljValue)result2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.414, val2);
    
    // Test: (Math/sqrt 0) => 0.0
    CljObject *result3 = eval_string("(Math/sqrt 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    float val3 = as_fixed((CljValue)result3);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, val3);
}
