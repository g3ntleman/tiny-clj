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

// Add a test to the registry (legacy function for backward compatibility)
void test_registry_add(const char *name, TestFunc func) {
    test_registry_add_with_group(name, func, "unknown");
}

// Helper function to create qualified name (removes "test_" prefix if present)
static char* create_qualified_name(const char *group, const char *name) {
    size_t group_len = strlen(group);
    
    // Remove "test_" prefix if present
    const char *display_name = name;
    if (strncmp(name, "test_", 5) == 0) {
        display_name = name + 5; // Skip "test_" prefix
    }
    
    size_t display_name_len = strlen(display_name);
    size_t total_len = group_len + 1 + display_name_len + 1; // group + "/" + display_name + "\0"
    
    char *qualified_name = malloc(total_len);
    if (!qualified_name) {
        fprintf(stderr, "Error: Failed to allocate memory for qualified name\n");
        return NULL;
    }
    
    snprintf(qualified_name, total_len, "%s/%s", group, display_name);
    return qualified_name;
}

// Add a test to the registry with group information
void test_registry_add_with_group(const char *name, TestFunc func, const char *group) {
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
    
    // Create qualified name
    char *qualified_name = create_qualified_name(group, name);
    if (!qualified_name) {
        return;
    }
    
    // Add test to registry
    test_registry[test_count].name = name;
    test_registry[test_count].qualified_name = qualified_name;
    test_registry[test_count].func = func;
    test_registry[test_count].group = group;
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

// Find a test by qualified name
Test *test_registry_find_by_qualified_name(const char *qualified_name) {
    for (size_t i = 0; i < test_count; i++) {
        if (strcmp(test_registry[i].qualified_name, qualified_name) == 0) {
            return &test_registry[i];
        }
    }
    return NULL;
}

// Find a test by pattern (supports both simple names and qualified names)
Test *test_registry_find_by_pattern(const char *pattern) {
    for (size_t i = 0; i < test_count; i++) {
        // Try matching against qualified name first
        if (test_name_matches_pattern(test_registry[i].qualified_name, pattern)) {
            return &test_registry[i];
        }
        // Fallback to simple name matching
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
        printf("  %s\n", test_registry[i].qualified_name);
    }
}

// Get tests by group
Test *test_registry_get_by_group(const char *group, size_t *count) {
    static Test *filtered_tests = NULL;
    static size_t filtered_capacity = 0;
    size_t filtered_count = 0;
    
    // Count tests in group
    for (size_t i = 0; i < test_count; i++) {
        if (strcmp(test_registry[i].group, group) == 0) {
            filtered_count++;
        }
    }
    
    if (filtered_count == 0) {
        *count = 0;
        return NULL;
    }
    
    // Grow filtered array if needed
    if (filtered_count > filtered_capacity) {
        Test *new_filtered = realloc(filtered_tests, filtered_count * sizeof(Test));
        if (!new_filtered) {
            fprintf(stderr, "Error: Failed to allocate memory for filtered tests\n");
            *count = 0;
            return NULL;
        }
        filtered_tests = new_filtered;
        filtered_capacity = filtered_count;
    }
    
    // Copy tests from group
    size_t idx = 0;
    for (size_t i = 0; i < test_count; i++) {
        if (strcmp(test_registry[i].group, group) == 0) {
            filtered_tests[idx] = test_registry[i];
            idx++;
        }
    }
    
    *count = filtered_count;
    return filtered_tests;
}

// List all groups in registry
void test_registry_list_groups(void) {
    printf("Available test groups:\n");
    
    // Simple approach: collect unique groups
    const char *groups[64];  // Max 64 groups
    size_t group_count = 0;
    
    for (size_t i = 0; i < test_count && group_count < 64; i++) {
        bool found = false;
        for (size_t j = 0; j < group_count; j++) {
            if (strcmp(groups[j], test_registry[i].group) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            groups[group_count] = test_registry[i].group;
            group_count++;
        }
    }
    
    for (size_t i = 0; i < group_count; i++) {
        printf("  %s\n", groups[i]);
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
