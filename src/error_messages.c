#include "error_messages.h"

const char *ERR_EXPECTED_NUMBER = "Expected number";
const char *ERR_WRONG_ARITY_ZERO = "Wrong number of args: 0";
const char *ERR_DIVIDE_BY_ZERO = "Divide by zero";

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

// Exception type constants
/** @brief Exception type: ArithmeticException */
const char *EXCEPTION_ARITHMETIC = "ArithmeticException";

// Integer overflow/underflow error messages
/** @brief Standard error message: Integer overflow in addition */
const char *ERR_INTEGER_OVERFLOW_ADDITION = "Integer overflow in addition: %d + %d would exceed INT_MAX";
/** @brief Standard error message: Integer underflow in addition */
const char *ERR_INTEGER_UNDERFLOW_ADDITION = "Integer underflow in addition: %d + %d would exceed INT_MIN";
/** @brief Standard error message: Integer overflow in subtraction */
const char *ERR_INTEGER_OVERFLOW_SUBTRACTION = "Integer overflow in subtraction: %d - %d would exceed INT_MAX";
/** @brief Standard error message: Integer underflow in subtraction */
const char *ERR_INTEGER_UNDERFLOW_SUBTRACTION = "Integer underflow in subtraction: %d - %d would exceed INT_MIN";
/** @brief Standard error message: Integer overflow in multiplication */
const char *ERR_INTEGER_OVERFLOW_MULTIPLICATION = "Integer overflow in multiplication: %d * %d would exceed INT_MAX";


