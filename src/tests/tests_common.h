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
#include "../function_call.h"
#include "../byte_array.h"
#include "../meta.h"
#include "../runtime.h"
#include "../parser.h"
#include "../namespace.h"
#include "../seq.h"
#include "../strings.h"
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

#endif // TESTS_COMMON_H
