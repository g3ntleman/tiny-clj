#ifndef VALIDATION_H
#define VALIDATION_H

#include "object.h"
#include "value.h"
#include <stdbool.h>

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
bool validate_arity(unsigned int argc, unsigned int expected_arity, const char *function_name);

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
bool validate_min_arity(unsigned int argc, unsigned int min_arity, const char *function_name);

/**
 * @brief Throws IndexOutOfBoundsException with a descriptive message.
 *
 * This helper function throws an IndexOutOfBoundsException for index validation errors.
 * It formats a message indicating the function name, index, count, and collection type.
 *
 * @param function_name Name of the function for error message
 * @param index The invalid index value
 * @param count The valid count/size of the collection
 * @param collection_type Type of collection (e.g., "vector", "weak vector", "transient vector")
 */
void throw_index_out_of_bounds(const char *function_name, unsigned int index, unsigned int count, const char *collection_type);

#endif // VALIDATION_H
