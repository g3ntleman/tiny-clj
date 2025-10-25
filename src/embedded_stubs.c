/*
 * Embedded Stubs for Tiny-CLJ Minimal Build
 * 
 * Provides minimal implementations for features disabled in embedded builds.
 * This file is only compiled when DISABLE_* flags are set.
 */

#include "object.h"
#include "value.h"
#include "memory.h"

#ifdef DISABLE_ERROR_MESSAGES
// Stub implementations for error messages when disabled
const char *ERR_EXPECTED_NUMBER = "Err";
const char *ERR_WRONG_ARITY_ZERO = "Err";
const char *ERR_DIVIDE_BY_ZERO = "Err";
const char *ERROR_EOF_VECTOR = "Err";
const char *ERROR_EOF_MAP = "Err";
const char *ERROR_EOF_LIST = "Err";
const char *ERROR_UNMATCHED_DELIMITER = "Err";
const char *ERROR_DIVISION_BY_ZERO = "Err";
const char *ERROR_INVALID_SYNTAX = "Err";
const char *ERROR_UNDEFINED_VARIABLE = "Err";
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

#ifdef DISABLE_VECTOR_OPERATIONS
// Stub implementations for vector operations when disabled
CljObject *make_vector(int capacity) {
    (void)capacity;
    return make_list(); // Fallback to list
}

CljObject *vector_conj(CljObject *vec, CljObject *item) {
    (void)vec;
    (void)item;
    return make_list(); // Fallback to list
}

CljObject *vector_get(CljObject *vec, int index) {
    (void)vec;
    (void)index;
    return make_nil();
}

CljObject *vector_assoc(CljObject *vec, int index, CljObject *item) {
    (void)vec;
    (void)index;
    (void)item;
    return make_list(); // Fallback to list
}

int vector_count(CljObject *vec) {
    (void)vec;
    return 0;
}
#endif

#ifdef DISABLE_MAP_OPERATIONS
// Stub implementations for map operations when disabled
CljObject *make_map(void) {
    return make_list(); // Fallback to list
}

CljObject *map_assoc(CljObject *map, CljObject *key, CljObject *value) {
    (void)map;
    (void)key;
    (void)value;
    return make_list(); // Fallback to list
}

CljObject *map_get(CljObject *map, CljObject *key) {
    (void)map;
    (void)key;
    return make_nil();
}

CljObject *map_keys(CljObject *map) {
    (void)map;
    return make_list();
}

CljObject *map_vals(CljObject *map) {
    (void)map;
    return make_list();
}

int map_count(CljObject *map) {
    (void)map;
    return 0;
}

CljObject *map_from_stack(CljObject **stack, int count) {
    (void)stack;
    (void)count;
    return make_list();
}

CljObject *conj_map(CljObject *map, CljObject *item) {
    (void)map;
    (void)item;
    return make_list();
}

CljObject *persistent_map(CljObject *map) {
    (void)map;
    return make_list();
}

CljObject *transient_map(CljObject *map) {
    (void)map;
    return make_list();
}
#endif

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
