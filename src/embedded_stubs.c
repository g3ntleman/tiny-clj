/*
 * Embedded Stubs for Tiny-CLJ Minimal Build
 * 
 * Provides minimal implementations for features disabled in embedded builds.
 * This file is only compiled when DISABLE_* flags are set.
 */

#include <subjective-c/object.h>
#include "value.h"
#include "memory.h"

#ifdef DISABLE_ERROR_MESSAGES
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

#ifdef DISABLE_MEMORY_PROFILER
// Stub implementations for memory profiler when disabled
void memory_profiler_track_retain(CljObject *obj) {
    (void)obj; // Suppress unused parameter warning
}

void memory_profiler_track_release(CljObject *obj) {
    (void)obj; // Suppress unused parameter warning
}

void memory_profiler_track_autorelease(CljObject *obj) {
    (void)obj; // Suppress unused parameter warning
}

void memory_profiler_track_alloc(CljObject *obj) {
    (void)obj; // Suppress unused parameter warning
}

void memory_profiler_track_dealloc(CljObject *obj) {
    (void)obj; // Suppress unused parameter warning
}

void memory_profiler_print_stats(void) {
    // No-op
}

void memory_profiler_reset_stats(void) {
    // No-op
}

void set_memory_verbose_mode(bool verbose) {
    (void)verbose; // Suppress unused parameter warning
}
#endif

// Vectors are required - no stubs needed
// DISABLE_VECTOR_OPERATIONS is no longer supported

// Maps are required - no stubs needed
// DISABLE_MAP_OPERATIONS is no longer supported

#ifdef DISABLE_COMPLEX_PARSING
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
