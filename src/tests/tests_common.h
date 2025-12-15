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

// Standard C Library Headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Tiny-CLJ Core Headers
#include "../object.h"
#include "../exception.h"  // Must be included before memory.h for WITH_AUTORELEASE_POOL
#include "../memory.h"
#include "../memory_profiler.h"
#include "../value.h"
#include "../builtins.h"
#include "../symbol.h"
#include "../map.h"
#include "../list.h"
#include "../vector.h"
#include "../function.h"
#include "../eval.h"
#include "../byte_array.h"
#include "../meta.h"
#include "../runtime.h"
#include "../parser.h"
#include "../ast_canon.h"
#include "../namespace.h"
#include "../symbol_token.h"
#include "../seq.h"
#include "../strings.h"
#include "../tiny_clj.h"

// Test Registry
#include "test_registry.h"

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

// Simple TEST macro that defines and registers a test function
// Automatically wraps test in WITH_AUTORELEASE_POOL for memory management
// Extracts filename from __FILE__ to use as group name
// Stores file path and line number for Unity error reporting
// Note: The global variable g_test_eval_state (or st via #define) is available in all tests
// Note: __attribute__((used)) prevents dead-strip from removing these functions
#define TEST(name) \
    static void name##_body(void); \
    void name(void) { \
        WITH_AUTORELEASE_POOL({ \
            name##_body(); \
        }); \
    } \
    static void register_##name(void) __attribute__((constructor, used)); \
    static void register_##name(void) { \
        char *filename = test_extract_filename_from_path(__FILE__); \
        if (filename) { \
            test_registry_add_with_file_info(#name, name, filename, __FILE__, __LINE__); \
            free(filename); \
        } \
    } \
    static void name##_body(void)

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
    TEST_ASSERT_TRUE(list_type_matches(TAG(obj)));
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
