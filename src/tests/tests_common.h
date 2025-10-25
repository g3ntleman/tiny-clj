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
#include "../exception.h"
#include "../meta.h"
#include "../runtime.h"
#include "../parser.h"
#include "../namespace.h"
#include "../seq.h"
#include "../clj_strings.h"
#include "../tiny_clj.h"

// Test Registry (will be implemented in next step)
// Forward declaration for now
void test_registry_add(const char *name, void (*func)(void));

// Registration macro for automatic test discovery
#define REGISTER_TEST(func) \
    static void register_##func(void) __attribute__((constructor)); \
    static void register_##func(void) { \
        test_registry_add(#func, func); \
    }

// Simple TEST macro that defines and registers a test function
// Automatically wraps test in WITH_AUTORELEASE_POOL for memory management
#define TEST(name) \
    static void name##_impl(void); \
    void name##_wrapper(void) { \
        WITH_AUTORELEASE_POOL({ \
            name##_impl(); \
        }); \
    } \
    static void register_##name(void) __attribute__((constructor)); \
    static void register_##name(void) { \
        test_registry_add(#name, name##_wrapper); \
    } \
    static void name##_impl(void)

#endif // TESTS_COMMON_H
