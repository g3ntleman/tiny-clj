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
    
    // Remove .c extension and "test_" prefix to get the group name
    size_t len = strlen(basename);
    if (len > 2 && strcmp(basename + len - 2, ".c") == 0) {
        static char group_name[64];
        size_t group_len = len - 2; // Remove .c
        
        // Remove "test_" prefix if present
        const char* name_start = basename;
        if (group_len > 5 && strncmp(basename, "test_", 5) == 0) {
            name_start = basename + 5; // Skip "test_" prefix
            group_len -= 5;
        }
        
        if (group_len < sizeof(group_name)) {
            strncpy(group_name, name_start, group_len);
            group_name[group_len] = '\0';
            return group_name;
        }
    }
    
    // Fallback: return filename without extension and "test_" prefix
    const char* name_start = basename;
    if (strncmp(basename, "test_", 5) == 0) {
        name_start = basename + 5;
    }
    return name_start;
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
