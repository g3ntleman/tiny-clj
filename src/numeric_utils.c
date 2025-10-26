#include "numeric_utils.h"

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
