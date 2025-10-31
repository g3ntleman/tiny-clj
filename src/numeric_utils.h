#ifndef NUMERIC_UTILS_H
#define NUMERIC_UTILS_H

#include "value.h"
#include "object.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Extract numeric values from two CljObjects and promote them to float
 * @param a First object (must be numeric)
 * @param b Second object (must be numeric)
 * @param val_a Output: promoted value of a
 * @param val_b Output: promoted value of b
 * @return true if both objects are numeric, false otherwise
 */
bool extract_numeric_values(CljObject *a, CljObject *b, float *val_a, float *val_b);

/**
 * @brief Comparison result enumeration for numeric comparisons.
 */
typedef enum {
    COMPARE_LESS = -1,
    COMPARE_EQUAL = 0,
    COMPARE_GREATER = 1
} CompareResult;

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
bool compare_numeric_values(CljObject *a, CljObject *b, CompareResult *result);

/**
 * @brief Format Q16.13 fixed-point raw value as decimal string using integer math.
 *
 * The function prints "-<int>.<frac>" with the requested number of fractional
 * digits. If trim_trailing_zeros is true, trailing zeros in the fractional part
 * are removed, and the decimal point is omitted if no fractional digits remain.
 *
 * @param buf Output buffer
 * @param n   Buffer size
 * @param raw Q16.13 raw value (scaled by 8192)
 * @param digits Requested fractional digits
 * @param trim_trailing_zeros Whether to trim trailing zeros
 * @return number of characters written (excluding NUL) or negative on error
 */
int format_fixed_q16_13(char *buf, size_t n, int32_t raw, unsigned digits, bool trim_trailing_zeros);

#endif // NUMERIC_UTILS_H
