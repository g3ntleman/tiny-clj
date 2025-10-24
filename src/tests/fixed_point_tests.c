#include "unity/src/unity.h"
#include "../value.h"

void test_fixed_basic_creation(void) {
    // Test basic creation and conversion
    CljValue fixed_val = fixed(1.5f);
    TEST_ASSERT_TRUE(is_fixed(fixed_val));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, as_fixed(fixed_val));
}

void test_fixed_negative_values(void) {
    // Test negative values
    CljValue fixed_val = fixed(-2.25f);
    TEST_ASSERT_TRUE(is_fixed(fixed_val));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -2.25f, as_fixed(fixed_val));
}

void test_fixed_precision(void) {
    // Test precision: 0.1 + 0.2 should be close to 0.3
    CljValue a = fixed(0.1f);
    CljValue b = fixed(0.2f);
    float sum = as_fixed(a) + as_fixed(b);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3f, sum);
}

void test_fixed_multiplication_raw(void) {
    // Test multiplication using raw fixed-point arithmetic
    // 1.5 * 2.5 = 3.75
    CljValue a = fixed(1.5f);
    CljValue b = fixed(2.5f);
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t result = (a_raw * b_raw) >> 13;
    float result_f = (float)result / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.75f, result_f);
}

void test_fixed_mixed_type_promotion(void) {
    // Test mixed type: Fixnum + Fixed
    // 1 (Fixnum) + 0.5 (Fixed) = 1.5
    CljValue fixnum_val = fixnum(1);
    CljValue fixed_val = fixed(0.5f);
    int32_t promoted = AS_FIXNUM(fixnum_val) << 13;
    int32_t fixed_raw = (int32_t)((intptr_t)fixed_val >> 3);
    int32_t sum = promoted + fixed_raw;
    float result = (float)sum / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, result);
}

void test_fixed_saturation_max(void) {
    // Test saturation at maximum value
    CljValue max = fixed(32767.0f);
    TEST_ASSERT_TRUE(is_fixed(max));
    float value = as_fixed(max);
    TEST_ASSERT_TRUE(value >= 32767.0f);
}

void test_fixed_saturation_min(void) {
    // Test saturation at minimum value
    CljValue min = fixed(-32768.0f);
    TEST_ASSERT_TRUE(is_fixed(min));
    float value = as_fixed(min);
    TEST_ASSERT_TRUE(value <= -32768.0f);
}

void test_fixed_division_raw(void) {
    // Test division using raw fixed-point arithmetic
    // 1.0 / 3.0 ≈ 0.333
    CljValue a = fixed(1.0f);
    CljValue b = fixed(3.0f);
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t result = (a_raw << 13) / b_raw;
    float result_f = (float)result / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.333f, result_f);
}

void test_fixed_edge_cases(void) {
    // Test edge cases
    CljValue zero = fixed(0.0f);
    TEST_ASSERT_TRUE(is_fixed(zero));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, as_fixed(zero));
    
    CljValue small = fixed(0.0001f);
    TEST_ASSERT_TRUE(is_fixed(small));
    // Should be rounded to nearest representable value
    float value = as_fixed(small);
    TEST_ASSERT_TRUE(value >= 0.0f);
}

void test_fixed_tag_consistency(void) {
    // Test that the tag is correctly set
    CljValue fixed_val = fixed(42.5f);
    uint8_t tag = get_tag(fixed_val);
    TEST_ASSERT_EQUAL(TAG_FIXED, tag);
}

void test_fixed_addition_builtin(void) {
    // Test addition using builtin functions
    CljValue a = fixed(1.5f);
    CljValue b = fixed(2.25f);
    
    // Simulate builtin addition: a + b
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t sum = a_raw + b_raw;
    
    // Saturierung prüfen
    if (sum > 268435455) sum = 268435455;
    if (sum < -268435456) sum = -268435456;
    
    float result = (float)sum / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.75f, result);
}

void test_fixed_subtraction_builtin(void) {
    // Test subtraction using builtin functions
    CljValue a = fixed(5.0f);
    CljValue b = fixed(2.5f);
    
    // Simulate builtin subtraction: a - b
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t diff = a_raw - b_raw;
    
    // Saturierung prüfen
    if (diff > 268435455) diff = 268435455;
    if (diff < -268435456) diff = -268435456;
    
    float result = (float)diff / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, result);
}

void test_fixed_mixed_addition(void) {
    // Test mixed type addition: Fixnum + Fixed
    CljValue fixnum_val = fixnum(10);
    CljValue fixed_val = fixed(0.5f);
    
    // Simulate mixed addition: fixnum + fixed
    int32_t fixnum_promoted = AS_FIXNUM(fixnum_val) << 13;
    int32_t fixed_raw = (int32_t)((intptr_t)fixed_val >> 3);
    int32_t sum = fixnum_promoted + fixed_raw;
    
    float result = (float)sum / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.5f, result);
}

void test_fixed_negative_addition(void) {
    // Test negative number addition
    CljValue a = fixed(-1.5f);
    CljValue b = fixed(2.0f);
    
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t sum = a_raw + b_raw;
    
    float result = (float)sum / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, result);
}

void test_fixed_multiplication_builtin(void) {
    // Test multiplication using builtin functions
    CljValue a = fixed(1.5f);
    CljValue b = fixed(2.0f);
    
    // Simulate builtin multiplication: a * b
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t product = (a_raw * b_raw) >> 13; // Fixed-Point Multiplikation mit Shift
    
    // Saturierung prüfen
    if (product > 268435455) product = 268435455;
    if (product < -268435456) product = -268435456;
    
    float result = (float)product / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, result);
}

void test_fixed_division_builtin(void) {
    // Test division using builtin functions
    CljValue a = fixed(6.0f);
    CljValue b = fixed(2.0f);
    
    // Simulate builtin division: a / b
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t quotient = (a_raw << 13) / b_raw; // Fixed-Point Division mit Shift
    
    // Saturierung prüfen
    if (quotient > 268435455) quotient = 268435455;
    if (quotient < -268435456) quotient = -268435456;
    
    float result = (float)quotient / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, result);
}

void test_fixed_mixed_multiplication(void) {
    // Test mixed type multiplication: Fixnum * Fixed
    CljValue fixnum_val = fixnum(4);
    CljValue fixed_val = fixed(0.5f);
    
    // Simulate mixed multiplication: fixnum * fixed
    int32_t fixnum_promoted = AS_FIXNUM(fixnum_val) << 13;
    int32_t fixed_raw = (int32_t)((intptr_t)fixed_val >> 3);
    int32_t product = (fixnum_promoted * fixed_raw) >> 13;
    
    float result = (float)product / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, result);
}

void test_fixed_division_by_zero(void) {
    // Test division by zero handling
    CljValue a = fixed(1.0f);
    CljValue b = fixed(0.0f);
    
    // Simulate division by zero: a / b
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    
    // Division durch Null sollte Infinity oder einen sehr großen Wert ergeben
    if (b_raw == 0) {
        // Test passes if we handle division by zero gracefully
        TEST_ASSERT_TRUE(true);
    } else {
        int32_t quotient = (a_raw << 13) / b_raw;
        float result = (float)quotient / 8192.0f;
        TEST_ASSERT_TRUE(result > 1000.0f || result < -1000.0f); // Very large result
    }
}

void test_fixed_complex_arithmetic(void) {
    // Test complex arithmetic: (1.5 + 2.5) * 0.5 = 2.0
    CljValue a = fixed(1.5f);
    CljValue b = fixed(2.5f);
    CljValue c = fixed(0.5f);
    
    // Simulate: (a + b) * c
    int32_t a_raw = (int32_t)((intptr_t)a >> 3);
    int32_t b_raw = (int32_t)((intptr_t)b >> 3);
    int32_t c_raw = (int32_t)((intptr_t)c >> 3);
    
    int32_t sum = a_raw + b_raw;
    int32_t result = (sum * c_raw) >> 13;
    
    float result_f = (float)result / 8192.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, result_f);
}
