#include "numeric_utils.h"
#include <stddef.h>
#include <stdint.h>

static size_t clj_write_unsigned(uint32_t value, char *buffer) {
    char digits[10];
    size_t index = 0;

    do {
        digits[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && index < sizeof(digits));

    size_t pos = 0;
    while (index > 0) {
        buffer[pos++] = digits[--index];
    }
    return pos;
}

size_t clj_uitoa(uint32_t value, char *buffer) {
    if (!buffer) {
        return 0;
    }
    size_t written = clj_write_unsigned(value, buffer);
    buffer[written] = '\0';
    return written;
}

size_t clj_itoa(int32_t value, char *buffer) {
    if (!buffer) {
        return 0;
    }

    size_t pos = 0;
    uint32_t magnitude;

    if (value < 0) {
        buffer[pos++] = '-';
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    pos += clj_write_unsigned(magnitude, buffer + pos);
    buffer[pos] = '\0';
    return pos;
}

size_t clj_ftoa(float value, char *buffer) {
    if (!buffer) {
        return 0;
    }

    double scaled = (double)value * 100.0;
    double abs_scaled = scaled >= 0.0 ? scaled : -scaled;
    int64_t rounded_abs = (int64_t)(abs_scaled + 0.5);
    int64_t rounded = scaled >= 0.0 ? rounded_abs : -rounded_abs;

    int32_t integer_part = (int32_t)(rounded / 100);
    int32_t fraction_part = (int32_t)(rounded % 100);
    if (fraction_part < 0) {
        fraction_part = -fraction_part;
    }

    size_t pos = clj_itoa(integer_part, buffer);
    buffer[pos++] = '.';
    buffer[pos++] = (char)('0' + (fraction_part / 10));
    buffer[pos++] = (char)('0' + (fraction_part % 10));
    buffer[pos] = '\0';
    return pos;
}

/**
 * @brief Extract numeric values from two CljObjects and promote them to float
 * @param a First object (must be numeric)
 * @param b Second object (must be numeric)
 * @param val_a Output: promoted value of a
 * @param val_b Output: promoted value of b
 * @return true if both objects are numeric, false otherwise
 */
bool extract_numeric_values(ID a, ID b, float *val_a, float *val_b) {
    // Extract value from first object
    if (is_fixnum(a)) {
        *val_a = (float)as_fixnum(a);
    } else if (is_fixed(a)) {
        *val_a = as_fixed(a);
    } else {
        return false; // Invalid type
    }
    
    // Extract value from second object
    if (is_fixnum(b)) {
        *val_b = (float)as_fixnum(b);
    } else if (is_fixed(b)) {
        *val_b = as_fixed(b);
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
bool compare_numeric_values(ID a, ID b, CompareResult *result) {
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
