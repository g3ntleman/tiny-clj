/**
 * @file exception.c
 * @brief Implementation of exception handling system with standard error messages.
 */

#include <stdlib.h>
#include <string.h>
#include "namespace.h"  // Must be before exception.h for EvalState definition
#include "exception.h"
#include "runtime.h"

// Global storage for current exception
CLJException *g_current_exception = NULL;

// ============================================================================
// STANDARD ERROR MESSAGES
// ============================================================================

/** @brief Standard error message: EOF while reading vector */
const char *ERROR_EOF_VECTOR = "EOF while reading vector";
/** @brief Standard error message: EOF while reading map */
const char *ERROR_EOF_MAP = "EOF while reading map";
/** @brief Standard error message: EOF while reading list */
const char *ERROR_EOF_LIST = "EOF while reading list";
/** @brief Standard error message: Unmatched delimiter */
const char *ERROR_UNMATCHED_DELIMITER = "Unmatched delimiter";
/** @brief Standard error message: Division by zero */
const char *ERROR_DIVISION_BY_ZERO = "Division by zero";
/** @brief Standard error message: Invalid syntax */
const char *ERROR_INVALID_SYNTAX = "Invalid syntax";
/** @brief Standard error message: Undefined variable */
const char *ERROR_UNDEFINED_VARIABLE = "Undefined variable";
/** @brief Standard error message: Type mismatch */
const char *ERROR_TYPE_MISMATCH = "Type mismatch";
/** @brief Standard error message: Stack overflow */
const char *ERROR_STACK_OVERFLOW = "Stack overflow";
/** @brief Standard error message: Memory allocation failed */
const char *ERROR_MEMORY_ALLOCATION = "Memory allocation failed";

/**
 * @brief Create exception with standard error message.
 * @param msg Error message string
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 * @return New exception object or NULL on failure
 */
CLJException* exception(const char *msg, const char *file, int line, int col) {
    return make_exception("Error", msg, file, line, col);
}

/**
 * @brief Create exception with dynamic error message.
 * @param msg Error message string (will be duplicated)
 * @param file Source file name
 * @param line Line number
 * @param col Column number
 * @return New exception object or NULL on failure
 */
CLJException* exception_dynamic(const char *msg, const char *file, int line, int col) {
    return make_exception("Error", msg, file, line, col);
}
