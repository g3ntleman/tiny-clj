/*
 * Embedded Stubs for Tiny-CLJ Minimal Build
 * 
 * Provides minimal implementations for features disabled in embedded builds.
 * This file is only compiled when *_ENABLED=0 feature flags are set.
 */

#include <subjective-c/object.h>
#include "value.h"
#include "memory.h"

#if defined(ERROR_MESSAGES_ENABLED) && !ERROR_MESSAGES_ENABLED
// Stub implementations for error messages when disabled
const char *ERR_EXPECTED_NUMBER = "Err";
const char *ERR_WRONG_ARITY_ZERO = "Err";
const char *ERR_DIVIDE_BY_ZERO = "Err";
const char *EXCEPTION_EOF_VECTOR = "Err";
const char *EXCEPTION_EOF_MAP = "Err";
const char *EXCEPTION_EOF_LIST = "Err";
const char *EXCEPTION_UNMATCHED_DELIMITER = "Err";
const char *EXCEPTION_DIVISION_BY_ZERO = "Err";
const char *EXCEPTION_INVALID_SYNTAX = "Err";
const char *EXCEPTION_UNDEFINED_VARIABLE = "Err";
const char *EXCEPTION_ARITHMETIC = "Err";
const char *ERR_INTEGER_OVERFLOW_ADDITION = "Err";
const char *ERR_INTEGER_UNDERFLOW_ADDITION = "Err";
const char *ERR_INTEGER_OVERFLOW_SUBTRACTION = "Err";
const char *ERR_INTEGER_UNDERFLOW_SUBTRACTION = "Err";
const char *ERR_INTEGER_OVERFLOW_MULTIPLICATION = "Err";
const char *ERR_FIXED_OVERFLOW_MULTIPLICATION = "Err";
const char *ERR_FIXED_OVERFLOW_ADDITION = "Err";
#endif

// Vectors are required - no stubs needed
// VECTOR_OPERATIONS_ENABLED is always on

// Maps are required - no stubs needed
// MAP_OPERATIONS_ENABLED is always on

#if defined(COMPLEX_PARSING_ENABLED) && !COMPLEX_PARSING_ENABLED
// Stub implementations for complex parsing when disabled
CljObject *parse_vector(CljObject *tokens, int *pos) {
    (void)tokens;
    (void)pos;
    return make_list(); // Fallback to list
}

CljObject *parse_map(CljObject *tokens, int *pos) {
    (void)tokens;
    (void)pos;
    return make_list(); // Fallback to list
}
#endif
