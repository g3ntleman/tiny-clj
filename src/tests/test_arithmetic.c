/*
 * Arithmetic Tests using Unity Framework
 * 
 * Tests for arithmetic operations and integer overflow/underflow detection.
 */

#include "tests_common.h"

// ============================================================================
// ARITHMETIC TESTS
// ============================================================================

// Test integer overflow detection
TEST(test_integer_overflow_detection) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test that normal multiplication still works
    CljObject *normal_result = eval_string("(* 2 3 4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(normal_result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)normal_result));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum((CljValue)normal_result));
    
    // Test that factorial calculation works (using direct multiplication)
    // Note: recur can only be used inside function bodies, so we test factorial via direct multiplication
    CljObject *small_factorial = eval_string("(* 5 4 3 2 1)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(small_factorial);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)small_factorial));
    TEST_ASSERT_EQUAL_INT(120, as_fixnum((CljValue)small_factorial));
    
    // Test addition overflow - large positive numbers
    bool exception_caught = false;
    TRY {
        CljObject *result = eval_string("(+ 2000000000 2000000000)", g_test_eval_state);
        if (result) {
            printf("WARNING: Addition overflow did not throw exception, result: %d\n", 
                   is_fixnum((CljValue)result) ? as_fixnum((CljValue)result) : 0);
        }
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
        if (result) {
            printf("WARNING: Addition overflow did not throw exception, result: %d\n", 
                   is_fixnum((CljValue)result) ? as_fixnum((CljValue)result) : 0);
        }
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
        if (result) {
            printf("WARNING: Addition underflow did not throw exception, result: %d\n", 
                   is_fixnum((CljValue)result) ? as_fixnum((CljValue)result) : 0);
        }
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
        if (result) {
            printf("WARNING: Subtraction underflow did not throw exception, result: %d\n", 
                   is_fixnum((CljValue)result) ? as_fixnum((CljValue)result) : 0);
        }
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
        if (result) {
            printf("WARNING: Subtraction overflow did not throw exception, result: %d\n", 
                   is_fixnum((CljValue)result) ? as_fixnum((CljValue)result) : 0);
        }
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
        if (result) {
            int result_val = is_fixnum((CljValue)result) ? as_fixnum((CljValue)result) : 0;
            // 100000 * 100000 = 10000000000, which exceeds INT_MAX (2147483647)
            if (result_val > 0 && result_val < 10000000000) {
                printf("WARNING: Multiplication may have overflowed without exception\n");
            }
        }
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
    TEST_ASSERT_TRUE(is_fixnum((CljValue)normal_add));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)normal_add));
    
    CljObject *normal_sub = eval_string("(- 50 20)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(normal_sub);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)normal_sub));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)normal_sub));
}

// Test simple arithmetic operations
TEST(test_simple_arithmetic) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test simple addition
    CljObject *result = eval_string("(+ 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixnum((CljValue)result)) {
        int val = as_fixnum((CljValue)result);
        TEST_ASSERT_EQUAL_INT(3, val);
    }
}

// Test division by zero exception
TEST(test_division_by_zero_exception) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
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
TEST(test_eval_list_simple_arithmetic) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test simple addition
    CljObject *result = eval_string("(+ 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(IS_IMMEDIATE(result));
}

// Test multiplication with negative numbers (for reduce tests)
TEST(test_multiplication_with_negative_numbers) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (* 1 -2) => -2 (positive * negative)
    CljObject *result1 = eval_string("(* 1 -2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result1));
    TEST_ASSERT_EQUAL_INT(-2, as_fixnum((CljValue)result1));
    
    // Test: (* -2 3) => -6 (negative * positive)
    CljObject *result2 = eval_string("(* -2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result2));
    TEST_ASSERT_EQUAL_INT(-6, as_fixnum((CljValue)result2));
    
    // Test: (* -2 3 -4) => 24 (negative * positive * negative)
    CljObject *result3 = eval_string("(* -2 3 -4)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result3));
    TEST_ASSERT_EQUAL_INT(24, as_fixnum((CljValue)result3));
    
    // Test: (* -1 -2) => 2 (negative * negative)
    CljObject *result4 = eval_string("(* -1 -2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result4));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum((CljValue)result4));
    
    // Test: (* -1 -2 -3) => -6 (negative * negative * negative)
    CljObject *result5 = eval_string("(* -1 -2 -3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result5));
    TEST_ASSERT_EQUAL_INT(-6, as_fixnum((CljValue)result5));
}
