#include "validation.h"
#include "exception.h"
#include <stdio.h>

/**
 * @brief Validates function arity and throws appropriate exception if invalid.
 *
 * This function checks if the provided argument count matches the expected arity.
 * If not, it throws an IllegalArgumentException with a descriptive message.
 *
 * @param argc Actual argument count
 * @param expected_arity Expected argument count
 * @param function_name Name of the function for error message
 * @return true if arity is valid, false if exception was thrown
 */
bool validate_arity(unsigned int argc, unsigned int expected_arity, const char *function_name) {
    if (argc != expected_arity) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "%s requires exactly %u argument%s, got %u",
                function_name, expected_arity, 
                expected_arity == 1 ? "" : "s", argc);
        
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return false;
    }
    return true;
}

/**
 * @brief Validates function arity for variadic functions (minimum arity).
 *
 * This function checks if the provided argument count is at least the minimum required.
 * If not, it throws an IllegalArgumentException with a descriptive message.
 *
 * @param argc Actual argument count
 * @param min_arity Minimum required argument count
 * @param function_name Name of the function for error message
 * @return true if arity is valid, false if exception was thrown
 */
bool validate_min_arity(unsigned int argc, unsigned int min_arity, const char *function_name) {
    if (argc < min_arity) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "%s requires at least %u argument%s, got %u",
                function_name, min_arity, 
                min_arity == 1 ? "" : "s", argc);
        
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return false;
    }
    return true;
}
