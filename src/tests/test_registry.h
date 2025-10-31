/*
 * Test Registry for Unity Tests
 * 
 * Central registry for all Unity tests to enable single executable runner.
 * Pure C - no POSIX dependencies for STM32 compatibility.
 */

#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>

// Test function pointer type
typedef void (*TestFunc)(void);

// Test entry structure
typedef struct {
    const char *name;          // Test function name (e.g., "test_memory_allocation")
    char *qualified_name;      // Qualified name (e.g., "memory/test_memory_allocation")
    TestFunc func;             // Test function pointer
    const char *group;         // Test group (e.g., "memory", "parser", "exception")
} Test;

// Registry functions
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

// Pattern matching
bool test_name_matches_pattern(const char *name, const char *pattern);

#endif // TEST_REGISTRY_H

