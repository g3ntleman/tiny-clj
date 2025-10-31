/*
 * Fixed-Point Arithmetic Tests using Unity Framework
 * 
 * Tests for fixed-point arithmetic operations and precision handling.
 */

#include "tests_common.h"
#include "exception.h"

// ============================================================================
// FIXED-POINT ARITHMETIC TESTS
// ============================================================================

TEST(test_fixed_creation_and_conversion) {
    // Test basic Fixed-Point creation
    CljValue f1 = fixed(1.5f);
    TEST_ASSERT_TRUE(is_fixed(f1));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, as_fixed(f1));
    
    // Test negative values
    CljValue f2 = fixed(-2.25f);
    TEST_ASSERT_TRUE(is_fixed(f2));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.25f, as_fixed(f2));
    
    // Test zero
    CljValue f3 = fixed(0.0f);
    TEST_ASSERT_TRUE(is_fixed(f3));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, as_fixed(f3));
    
    // Test very small values
    CljValue f4 = fixed(0.001f);
    TEST_ASSERT_TRUE(is_fixed(f4));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, as_fixed(f4));
}

TEST(test_fixed_arithmetic_operations) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
        }
    
        // Initialize namespace first
    
        // Test addition: 1.5 + 2.25 = 3.75
    CljObject *result = eval_string("(+ 1.5 2.25)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.75f, val);
        }
        // No RELEASE needed - eval_string returns autoreleased object
    
        // Test subtraction: 5.0 - 1.5 = 3.5
    result = eval_string("(- 5.0 1.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, val);
        }
        // No RELEASE needed - eval_string returns autoreleased object
    
        // Test multiplication: 2.5 * 3.0 = 7.5
    result = eval_string("(* 2.5 3.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, val);
        }
        // No RELEASE needed - eval_string returns autoreleased object
    
        // Test division: 6.0 / 2.0 = 3.0
    result = eval_string("(/ 6.0 2.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, val);
        }
    RELEASE(result);
    
    evalstate_free(st);
}

TEST(test_fixed_mixed_type_operations) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
    }
    
    // Initialize namespace first
    
        // Test int + float: 1 + 1.2 = 2.2 (with Fixed-Point precision)
    CljObject *result = eval_string("(+ 1 1.2)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    // Fixed-Point precision: 2.2 becomes ~2.199
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.2f, val);
        }
    RELEASE(result);
    
        // Test float + int: 2.5 + 3 = 5.5
    result = eval_string("(+ 2.5 3)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, val);
        }
    RELEASE(result);
    
        // Test multiple mixed types: 1 + 2.5 + 3 = 6.5
    result = eval_string("(+ 1 2.5 3)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, val);
        }
    RELEASE(result);
    
    evalstate_free(st);
}

TEST(test_fixed_division_with_remainder) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
    }
    
    // Initialize namespace first
    
        // Test integer division (no remainder): 6 / 2 = 3 (integer)
    CljObject *result = eval_string("(/ 6 2)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixnum(result)) {
    int val = as_fixnum(result);
    TEST_ASSERT_EQUAL_INT(3, val);
        }
    RELEASE(result);
    
        // Test float division (with remainder): 5 / 2 = 2.5 (Fixed-Point)
    result = eval_string("(/ 5 2)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, val);
        }
    RELEASE(result);
    
        // Test mixed division: 7.0 / 2 = 3.5 (Fixed-Point)
    result = eval_string("(/ 7.0 2)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, val);
        }
    RELEASE(result);
    
    evalstate_free(st);
}

TEST(test_fixed_precision_limits) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
    }
    
    // Initialize namespace first
    
        // Test Fixed-Point precision limits
        // Very small number
    CljObject *result = eval_string("0.001", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, val);
        }
    RELEASE(result);
    
        // Test that very precise numbers get rounded
    result = eval_string("1.23456789", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    // Fixed-Point should round to ~4 significant digits
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.235f, val);
        }
    RELEASE(result);
    
        // Test large number
    result = eval_string("1000.5", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.5f, val);
        }
    RELEASE(result);
    
    evalstate_free(st);
}

TEST(test_fixed_variadic_operations) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
    }
    
    // Initialize namespace first
    
        // Test multiple float addition
    CljObject *result = eval_string("(+ 1.0 2.0 3.0 4.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, val);
        }
    RELEASE(result);
    
        // Test mixed variadic: 1 + 2.5 + 3 + 4.5 = 11.0
    result = eval_string("(+ 1 2.5 3 4.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.0f, val);
        }
    RELEASE(result);
    
        // Test multiplication with floats
    result = eval_string("(* 2.0 3.0 4.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    if (result && is_fixed(result)) {
    float val = as_fixed(result);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, val);
        }
    RELEASE(result);
    
    evalstate_free(st);
}

TEST(test_fixed_error_handling) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
        }
    
        // Test division by zero
    CljObject *result = eval_string("(/ 6 2)", st);
    TEST_ASSERT_NOT_NULL(result); // Should work fine
    if (result) RELEASE(result);
    
        // Test division by zero - should throw exception
    bool exception_caught = false;
    TRY {
    result = eval_string("(/ 1.0 0.0)", st);
        } CATCH(ex) {
    exception_caught = true;
    result = NULL;
        } END_TRY
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Division by zero should throw exception");
    TEST_ASSERT_NULL(result);
    
    evalstate_free(st);
}

TEST(test_fixed_comparison_operators) {
    EvalState *st = evalstate_new();
    if (!st) {
    TEST_FAIL_MESSAGE("Failed to create EvalState");
    return;
        }
    
        // Initialize namespace first
    
        // Test < operator
    CljObject *result = eval_string("(< 1.5 2.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
        // Test > operator
    result = eval_string("(> 2.0 1.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
        // Test <= operator
    result = eval_string("(<= 1.5 1.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
        // Test >= operator
    result = eval_string("(>= 2.0 2.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
        // Test = operator
    result = eval_string("(= 1.5 1.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
        // Test mixed int/float comparisons
    result = eval_string("(< 1 1.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
    result = eval_string("(> 1.5 1)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy(result));
    RELEASE(result);
    
        // Test false cases
    result = eval_string("(< 2.0 1.5)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_FALSE(clj_is_truthy(result));
    RELEASE(result);
    
    result = eval_string("(> 1.5 2.0)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_FALSE(clj_is_truthy(result));
    RELEASE(result);
    
    evalstate_free(st);
}
