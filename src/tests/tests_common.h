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
void test_registry_add_with_group(const char *name, void (*func)(void), const char *group);

// Helper function to extract group name from filename
static const char* extract_group_from_file(const char* filename) {
    // Find the last '/' in the path
    const char* last_slash = strrchr(filename, '/');
    const char* basename = last_slash ? last_slash + 1 : filename;
    
    // Check if it starts with "test_" and ends with ".c"
    if (strncmp(basename, "test_", 5) == 0) {
        size_t len = strlen(basename);
        if (len > 3 && strcmp(basename + len - 2, ".c") == 0) {
            // Extract the group name (between "test_" and ".c")
            static char group_name[64];
            size_t group_len = len - 7; // "test_" + ".c" = 7 chars
            if (group_len < sizeof(group_name)) {
                strncpy(group_name, basename + 5, group_len);
                group_name[group_len] = '\0';
                return group_name;
            }
        }
    }
    return "unknown";
}

// Registration macro for automatic test discovery
#define REGISTER_TEST(func) \
    static void register_##func(void) __attribute__((constructor)); \
    static void register_##func(void) { \
        test_registry_add_with_group(#func, func, extract_group_from_file(__FILE__)); \
    }

// Simple TEST macro that defines and registers a test function
// Automatically wraps test in WITH_AUTORELEASE_POOL for memory management
#define TEST(name) \
    static void name##_body(void); \
    void name(void) { \
        WITH_AUTORELEASE_POOL({ \
            name##_body(); \
        }); \
    } \
    static void register_##name(void) __attribute__((constructor)); \
    static void register_##name(void) { \
        test_registry_add_with_group(#name, name, extract_group_from_file(__FILE__)); \
    } \
    static void name##_body(void)

#endif // TESTS_COMMON_H
