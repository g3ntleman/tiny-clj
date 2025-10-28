/*
 * Test Registry for Tiny-CLJ
 * 
 * Dynamic test registration system that allows tests to register themselves
 * at program startup using GCC constructor attributes.
 */

#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>

// Test function pointer type
typedef void (*TestFunc)(void);

// Test structure
typedef struct {
    const char *name;        // Test function name (e.g., "test_cljvalue_immediate_helpers")
    const char *qualified_name; // Fully qualified name (e.g., "values/test_cljvalue_immediate_helpers")
    TestFunc func;          // Test function pointer
    const char *group;      // Test group (derived from filename)
} Test;

// Registry API
void test_registry_add(const char *name, TestFunc func);
void test_registry_add_with_group(const char *name, TestFunc func, const char *group);
Test *test_registry_find(const char *name);
Test *test_registry_find_by_qualified_name(const char *qualified_name);
Test *test_registry_find_by_pattern(const char *pattern);
Test *test_registry_get_all(size_t *count);
Test *test_registry_get_by_group(const char *group, size_t *count);
void test_registry_list_all(void);
void test_registry_list_groups(void);
void test_registry_clear(void);

// Pattern matching helper
bool test_name_matches_pattern(const char *name, const char *pattern);

#endif // TEST_REGISTRY_H