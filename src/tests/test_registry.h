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
    const char *name;    // Test function name
    TestFunc func;      // Test function pointer
} Test;

// Registry API
void test_registry_add(const char *name, TestFunc func);
Test *test_registry_find(const char *name);
Test *test_registry_find_by_pattern(const char *pattern);
Test *test_registry_get_all(size_t *count);
void test_registry_list_all(void);
void test_registry_clear(void);

// Pattern matching helper
bool test_name_matches_pattern(const char *name, const char *pattern);

#endif // TEST_REGISTRY_H