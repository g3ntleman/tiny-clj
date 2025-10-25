/*
 * Test Registry Implementation for Tiny-CLJ
 * 
 * Dynamic test registration system with pattern matching support.
 */

#include "test_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Registry state
static Test *test_registry = NULL;
static size_t test_count = 0;
static size_t test_capacity = 0;

// Initial capacity for registry
#define INITIAL_CAPACITY 64

// Add a test to the registry
void test_registry_add(const char *name, TestFunc func) {
    // Grow registry if needed
    if (test_count >= test_capacity) {
        size_t new_capacity = test_capacity == 0 ? INITIAL_CAPACITY : test_capacity * 2;
        Test *new_registry = realloc(test_registry, new_capacity * sizeof(Test));
        if (!new_registry) {
            fprintf(stderr, "Error: Failed to allocate memory for test registry\n");
            return;
        }
        test_registry = new_registry;
        test_capacity = new_capacity;
    }
    
    // Add test to registry
    test_registry[test_count].name = name;
    test_registry[test_count].func = func;
    test_count++;
}

// Find a test by exact name
Test *test_registry_find(const char *name) {
    for (size_t i = 0; i < test_count; i++) {
        if (strcmp(test_registry[i].name, name) == 0) {
            return &test_registry[i];
        }
    }
    return NULL;
}

// Find a test by pattern (simple wildcard matching)
Test *test_registry_find_by_pattern(const char *pattern) {
    for (size_t i = 0; i < test_count; i++) {
        if (test_name_matches_pattern(test_registry[i].name, pattern)) {
            return &test_registry[i];
        }
    }
    return NULL;
}

// Get all tests in registry
Test *test_registry_get_all(size_t *count) {
    *count = test_count;
    return test_registry;
}

// List all tests in registry
void test_registry_list_all(void) {
    printf("Available tests (%zu total):\n", test_count);
    for (size_t i = 0; i < test_count; i++) {
        printf("  %s\n", test_registry[i].name);
    }
}

// Clear the registry (for cleanup)
void test_registry_clear(void) {
    free(test_registry);
    test_registry = NULL;
    test_count = 0;
    test_capacity = 0;
}

// Simple pattern matching with * wildcard
bool test_name_matches_pattern(const char *name, const char *pattern) {
    const char *name_ptr = name;
    const char *pattern_ptr = pattern;
    
    while (*name_ptr && *pattern_ptr) {
        if (*pattern_ptr == '*') {
            // Skip to next non-wildcard character
            pattern_ptr++;
            if (*pattern_ptr == '\0') {
                return true; // Pattern ends with *, matches rest
            }
            
            // Find next matching character in name
            while (*name_ptr && *name_ptr != *pattern_ptr) {
                name_ptr++;
            }
            if (*name_ptr == '\0') {
                return false; // No match found
            }
        } else if (*name_ptr == *pattern_ptr) {
            name_ptr++;
            pattern_ptr++;
        } else {
            return false; // No match
        }
    }
    
    // Both strings must be exhausted for match
    return (*name_ptr == '\0' && *pattern_ptr == '\0');
}
