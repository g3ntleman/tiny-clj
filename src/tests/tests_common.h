/*
 * Common Test Headers for Tiny-CLJ
 * 
 * Central header file that includes all standard headers needed for tests.
 * This eliminates the need to include individual headers in each test file.
 */

#ifndef TESTS_COMMON_H
#define TESTS_COMMON_H

// Unity Test Framework
#include "unity/src/unity.h"

// Convenience alias: Clojure-style "nil" in C tests.
// Unity uses "NULL", many tiny-clj tests talk about "nil".
#ifndef TEST_ASSERT_NIL
#define TEST_ASSERT_NIL TEST_ASSERT_NULL
#endif
#ifndef TEST_ASSERT_NIL_MESSAGE
#define TEST_ASSERT_NIL_MESSAGE TEST_ASSERT_NULL_MESSAGE
#endif

// Standard C Library Headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

// Tiny-CLJ Core Headers
#include "../object.h"
#include "exception.h"  // Must be included before memory.h for WITH_AUTORELEASE_POOL
#include "memory.h"
#include "../memory_profiler.h"
#include "value.h"
#include "../builtins.h"
#include "../symbol.h"
#include "map.h"
#include "../list.h"
#include "vector.h"
#include "../function.h"
#include "../eval.h"
#include "byte_array.h"
#include "../meta.h"
#include "../runtime.h"
#include "../parser.h"
#include "../ast_canon.h"
#include "../namespace.h"
#include "../symbol_token.h"
#include "../seq.h"
#include "strings.h"
#include "../tiny_clj.h"
#include "instant.h"
#include "mini_format.h"

// Test Registry - use subjective-c test infrastructure
#include "test_registry.h"

// Compatibility aliases for tiny-clj tests
#define test_registry_add subjective_c_test_registry_add
#define test_registry_add_with_group(name, fn, group) \
    subjective_c_test_registry_add_with_file_info(name, fn, group, __FILE__, __LINE__)
#define test_registry_add_with_file_info subjective_c_test_registry_add_with_file_info
#define test_registry_find subjective_c_test_registry_find
#define test_registry_find_by_qualified_name subjective_c_test_registry_find_by_qualified_name
#define test_registry_find_by_pattern subjective_c_test_registry_find_by_pattern
#define test_registry_get_all subjective_c_test_registry_entries
#define test_registry_get_by_group subjective_c_test_registry_get_by_group
#define test_registry_list_all subjective_c_test_registry_list_all
#define test_registry_list_groups subjective_c_test_registry_list_groups
#define test_extract_filename_from_path subjective_c_test_extract_filename_from_path
#define test_name_matches_pattern subjective_c_test_name_matches_pattern

// Compatibility typedef with field mapping
typedef struct {
    const char *name;
    const char *qualified_name;
    void (*func)(void);  // Maps to fn in SubjectiveCTestEntry
    const char *group;
    const char *file;
    int line;
} Test;
typedef void (*TestFunc)(void);

// Compatibility macro to access func field
#define TEST_GET_FUNC(entry) ((TestFunc)((SubjectiveCTestEntry*)(entry))->fn)

// Global test EvalState (available in all tests)
extern EvalState* g_test_eval_state;

// Function to get global test EvalState (for backwards compatibility)
extern EvalState* test_get_eval_state(void);

// Registration macro for automatic test discovery
// Note: __attribute__((used)) prevents dead-strip from removing these functions
#define REGISTER_TEST(func) \
    static void register_##func(void) __attribute__((constructor, used)); \
    static void register_##func(void) { \
        test_registry_add_with_group(#func, func, "test"); \
    }

// TEST macro uses subjective-c test infrastructure (defined in test_registry.h)
// It automatically extracts filename from __FILE__ to use as group name
// and stores file path and line number for Unity error reporting
// Note: The global variable g_test_eval_state (or st via #define) is available in all tests

// TEST_SHARED macro for tests that can share clojure.core state (read-only tests)
// These tests are batched together with only one setUp/tearDown per batch
// Use for tests that don't define new functions/vars via defn/def
#define TEST_SHARED(name) \
    static void name##_impl(void); \
    static void name(void) { \
        WITH_AUTORELEASE_POOL({ name##_impl(); }); \
    } \
    static void register_##name(void) __attribute__((constructor, used)); \
    static void register_##name(void) { \
        char *filename = test_extract_filename_from_path(__FILE__); \
        if (filename) { \
            char group[128]; \
            (void)mini_snprintf(group, sizeof(group), "shared_%s", filename); \
            test_registry_add_with_file_info(#name, name, group, __FILE__, __LINE__); \
            CLJ_FREE(filename); \
        } \
    } \
    static void name##_impl(void)

// -----------------------------------------------------------------------------
// mini_format helpers for tests (avoid libc printf-family)
// -----------------------------------------------------------------------------
static inline void test_vfprintf(FILE *stream, const char *fmt, va_list ap) {
    char buf[1024];
    (void)mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    fputs(buf, stream ? stream : stdout);
}

static inline void test_fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    test_vfprintf(stream, fmt, ap);
    va_end(ap);
}

static inline void test_snprintf(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    (void)mini_vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
}

// Like "%.*s" but without printf: copy prefix_len bytes, then append suffix.
static inline void test_path_join_prefix(char *out, size_t out_sz, const char *prefix, size_t prefix_len, const char *suffix) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    size_t off = 0;
    if (prefix && prefix_len > 0) {
        size_t n = prefix_len;
        if (n >= out_sz) n = out_sz - 1;
        memcpy(out, prefix, n);
        off = n;
        out[off] = '\0';
    }
    (void)format_append(out, off, out_sz, suffix ? suffix : "");
}

// ============================================================================
// HELPER FUNCTIONS FOR COMMON TEST PATTERNS
// ============================================================================

// Helper: Assert that object is a fixnum with expected value
static inline void assert_fixnum(CljObject *obj, int expected) {
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(obj));
    TEST_ASSERT_EQUAL_INT(expected, as_fixnum((CljValue)obj));
}

// Helper: Assert that object is a fixed-point number with expected value
static inline void assert_fixed(CljObject *obj, float expected, float tolerance) {
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_TRUE(is_fixed((CljValue)obj));
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected, as_fixed((CljValue)obj));
}

// Helper: Assert that object is a string with expected value
static inline void assert_string(CljObject *obj, const char *expected) {
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(obj));
    CljString *str = as_clj_string(obj);
    TEST_ASSERT_EQUAL_STRING(expected, clj_string_data(str));
}

// Helper: Assert that object is a list or AST-backed list
static inline void assert_list(CljObject *obj) {
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_TRUE(is_list_type(TAG(obj)));
}

// Helper: Assert that object is a vector
static inline void assert_vector(CljObject *obj) {
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(obj));
}

// Helper: Assert that object is a map
static inline void assert_map(CljObject *obj) {
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP, TAG(obj));
}

static inline ID parse_canonicalized(const char *input, EvalState *st) {
    ID parsed = parse(input, st);
    if (!parsed) {
        return NULL;
    }
    return canonicalize_ast(parsed, st);
}

#endif // TESTS_COMMON_H
