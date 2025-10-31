#include "numeric_utils.h"
#include <stdio.h>

/**
 * @brief Extract numeric values from two CljObjects and promote them to float
 * @param a First object (must be numeric)
 * @param b Second object (must be numeric)
 * @param val_a Output: promoted value of a
 * @param val_b Output: promoted value of b
 * @return true if both objects are numeric, false otherwise
 */
bool extract_numeric_values(CljObject *a, CljObject *b, float *val_a, float *val_b) {
    // Extract value from first object
    if (is_fixnum((CljValue)a)) {
        *val_a = (float)as_fixnum((CljValue)a);
    } else if (is_fixed((CljValue)a)) {
        *val_a = as_fixed((CljValue)a);
    } else {
        return false; // Invalid type
    }
    
    // Extract value from second object
    if (is_fixnum((CljValue)b)) {
        *val_b = (float)as_fixnum((CljValue)b);
    } else if (is_fixed((CljValue)b)) {
        *val_b = as_fixed((CljValue)b);
    } else {
        return false; // Invalid type
    }
    
    return true;
}

/**
 * @brief Compares two numeric values and returns the comparison result.
 *
 * This function extracts numeric values from two CljObject pointers and
 * compares them, returning a standardized comparison result.
 *
 * @param a Input: First CljObject pointer.
 * @param b Input: Second CljObject pointer.
 * @param result Output: Comparison result (-1, 0, or 1).
 * @return true if both objects are numeric, false otherwise
 */
bool compare_numeric_values(CljObject *a, CljObject *b, CompareResult *result) {
    float val_a, val_b;
    
    if (!extract_numeric_values(a, b, &val_a, &val_b)) {
        return false;
    }
    
    if (val_a < val_b) {
        *result = COMPARE_LESS;
    } else if (val_a > val_b) {
        *result = COMPARE_GREATER;
    } else {
        *result = COMPARE_EQUAL;
    }
    
    return true;
}

int format_fixed_q16_13(char *buf, size_t n, int32_t raw, unsigned digits, bool trim_trailing_zeros) {
    if (!buf || n == 0) {
        return -1;
    }

    const int frac_bits = 13;
    const int32_t frac_mask = (1 << frac_bits) - 1;

    bool negative = raw < 0;
    if (negative) {
        // Beware of INT32_MIN; promote to int64_t for safe negation
        int64_t tmp = -(int64_t)raw;
        raw = (int32_t)tmp;
    }

    int32_t int_part = raw >> frac_bits;
    uint32_t frac_raw = (uint32_t)(raw & frac_mask);

    // Scale fractional part to 10^digits with rounding
    uint32_t pow10 = 1;
    for (unsigned i = 0; i < digits; i++) pow10 *= 10u;

    uint32_t frac_scaled = 0;
    if (digits > 0) {
        uint64_t prod = (uint64_t)frac_raw * (uint64_t)pow10;
        // Round to nearest: add half of denominator (1 << (frac_bits-1))
        uint64_t rounded = (prod + (1ull << (frac_bits - 1))) >> frac_bits;
        frac_scaled = (uint32_t)rounded;

        if (trim_trailing_zeros) {
            while (digits > 0 && (frac_scaled % 10u) == 0u) {
                frac_scaled /= 10u;
                digits--;
            }
        }
    }

    if (digits == 0) {
        return snprintf(buf, n, "%s%d", negative ? "-" : "", (int)int_part);
    }
    return snprintf(buf, n, "%s%d.%0*u", negative ? "-" : "", (int)int_part, digits, frac_scaled);
}
