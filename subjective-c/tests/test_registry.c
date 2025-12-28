#include "test_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Registry state - dynamic allocation for flexibility
static SubjectiveCTestEntry *g_entries = NULL;
static size_t g_entry_count = 0;
static size_t g_entry_capacity = 0;

#define INITIAL_CAPACITY 64

// Helper function to create qualified name (removes "test_" prefix if present)
static char* create_qualified_name(const char *group, const char *name) {
    size_t group_len = strlen(group);
    
    // Remove "test_" prefix if present
    const char *name_start = name;
    if (strncmp(name, "test_", 5) == 0) {
        name_start = name + 5;
    }
    
    size_t name_len = strlen(name_start);
    size_t total_len = group_len + 1 + name_len; // group + '/' + name
    
    char *qualified = malloc(total_len + 1);
    if (!qualified) {
        return NULL;
    }
    
    snprintf(qualified, total_len + 1, "%s/%s", group, name_start);
    return qualified;
}

// Basic registration (backward compatible)
void subjective_c_test_registry_add(const char *name, const char *file, int line, SubjectiveCTestFn fn) {
    if (!name || !fn) {
        fprintf(stderr, "subjective-c test registration error: invalid entry\n");
        abort();
    }
    
    // Use "unknown" as group for backward compatibility
    subjective_c_test_registry_add_with_file_info(name, fn, "unknown", file, line);
}

// Extended registration with group and file info
void subjective_c_test_registry_add_with_file_info(const char *name, SubjectiveCTestFn fn, const char *group, const char *file, int line) {
    if (!name || !fn) {
        fprintf(stderr, "subjective-c test registration error: invalid entry\n");
        abort();
    }
    
    // Grow registry if needed
    if (g_entry_count >= g_entry_capacity) {
        size_t new_capacity = g_entry_capacity == 0 ? INITIAL_CAPACITY : g_entry_capacity * 2;
        SubjectiveCTestEntry *new_entries = realloc(g_entries, new_capacity * sizeof(SubjectiveCTestEntry));
        if (!new_entries) {
            fprintf(stderr, "subjective-c test registry: failed to allocate memory\n");
            abort();
        }
        g_entries = new_entries;
        g_entry_capacity = new_capacity;
    }
    
    if (g_entry_count >= SUBJECTIVE_C_TEST_MAX) {
        fprintf(stderr, "subjective-c test registry overflow (max=%d)\n", SUBJECTIVE_C_TEST_MAX);
        abort();
    }
    
    // Copy strings to ensure they're permanently stored
    size_t name_len = strlen(name);
    char *name_copy = malloc(name_len + 1);
    if (!name_copy) {
        fprintf(stderr, "subjective-c test registry: failed to allocate name\n");
        abort();
    }
    strcpy(name_copy, name);
    
    size_t group_len = strlen(group);
    char *group_copy = malloc(group_len + 1);
    if (!group_copy) {
        free(name_copy);
        fprintf(stderr, "subjective-c test registry: failed to allocate group\n");
        abort();
    }
    strcpy(group_copy, group);
    
    char *qualified_name = create_qualified_name(group_copy, name);
    if (!qualified_name) {
        free(group_copy);
        free(name_copy);
        fprintf(stderr, "subjective-c test registry: failed to create qualified name\n");
        abort();
    }
    
    char *file_copy = NULL;
    if (file) {
        size_t file_len = strlen(file);
        file_copy = malloc(file_len + 1);
        if (!file_copy) {
            free(qualified_name);
            free(group_copy);
            free(name_copy);
            fprintf(stderr, "subjective-c test registry: failed to allocate file\n");
            abort();
        }
        strcpy(file_copy, file);
    }
    
    // Add entry
    g_entries[g_entry_count].name = name_copy;
    g_entries[g_entry_count].qualified_name = qualified_name;
    g_entries[g_entry_count].group = group_copy;
    g_entries[g_entry_count].file = file_copy;
    g_entries[g_entry_count].line = line;
    g_entries[g_entry_count].fn = fn;
    g_entry_count++;
}

// Get all entries
const SubjectiveCTestEntry* subjective_c_test_registry_entries(size_t *count) {
    if (count) {
        *count = g_entry_count;
    }
    return g_entries;
}

// Find by name
SubjectiveCTestEntry* subjective_c_test_registry_find(const char *name) {
    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].name, name) == 0) {
            return &g_entries[i];
        }
    }
    return NULL;
}

// Find by qualified name
SubjectiveCTestEntry* subjective_c_test_registry_find_by_qualified_name(const char *qualified_name) {
    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].qualified_name, qualified_name) == 0) {
            return &g_entries[i];
        }
    }
    return NULL;
}

// Find by pattern
SubjectiveCTestEntry* subjective_c_test_registry_find_by_pattern(const char *pattern) {
    for (size_t i = 0; i < g_entry_count; i++) {
        if (subjective_c_test_name_matches_pattern(g_entries[i].qualified_name, pattern)) {
            return &g_entries[i];
        }
    }
    return NULL;
}

// Get by group
SubjectiveCTestEntry* subjective_c_test_registry_get_by_group(const char *group, size_t *count) {
    size_t filtered_count = 0;
    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].group, group) == 0) {
            filtered_count++;
        }
    }
    
    if (filtered_count == 0) {
        *count = 0;
        return NULL;
    }
    
    SubjectiveCTestEntry *filtered = malloc(filtered_count * sizeof(SubjectiveCTestEntry));
    if (!filtered) {
        *count = 0;
        return NULL;
    }
    
    size_t idx = 0;
    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].group, group) == 0) {
            filtered[idx++] = g_entries[i];
        }
    }
    
    *count = filtered_count;
    return filtered;
}

// List all tests
void subjective_c_test_registry_list_all(void) {
    printf("Available tests (%zu total):\n", g_entry_count);
    for (size_t i = 0; i < g_entry_count; i++) {
        printf("  %s\n", g_entries[i].qualified_name);
    }
}

// List all groups
void subjective_c_test_registry_list_groups(void) {
    printf("Available test groups:\n");
    const char *groups[64];
    size_t group_count = 0;
    
    for (size_t i = 0; i < g_entry_count && group_count < 64; i++) {
        bool found = false;
        for (size_t j = 0; j < group_count; j++) {
            if (strcmp(groups[j], g_entries[i].group) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            groups[group_count++] = g_entries[i].group;
        }
    }
    
    for (size_t i = 0; i < group_count; i++) {
        printf("  %s\n", groups[i]);
    }
}

// Extract filename from path
char *subjective_c_test_extract_filename_from_path(const char *file_path) {
    if (!file_path) {
        char *result = malloc(8);
        if (result) {
            strcpy(result, "unknown");
        }
        return result;
    }
    
    const char *last_sep = strrchr(file_path, '/');
    const char *last_sep_win = strrchr(file_path, '\\');
    if (last_sep_win && (!last_sep || last_sep_win > last_sep)) {
        last_sep = last_sep_win;
    }
    
    const char *filename = last_sep ? last_sep + 1 : file_path;
    const char *last_dot = strrchr(filename, '.');
    
    if (last_dot) {
        size_t len = last_dot - filename;
        char *result = malloc(len + 1);
        if (!result) {
            return NULL;
        }
        strncpy(result, filename, len);
        result[len] = '\0';
        return result;
    }
    
    size_t len = strlen(filename);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    strcpy(result, filename);
    return result;
}

// Pattern matching with * wildcard
bool subjective_c_test_name_matches_pattern(const char *name, const char *pattern) {
    const char *name_ptr = name;
    const char *pattern_ptr = pattern;
    
    while (*name_ptr && *pattern_ptr) {
        if (*pattern_ptr == '*') {
            pattern_ptr++;
            if (*pattern_ptr == '\0') {
                return true;
            }
            while (*name_ptr && *name_ptr != *pattern_ptr) {
                name_ptr++;
            }
            if (*name_ptr == '\0') {
                return false;
            }
        } else if (*name_ptr == *pattern_ptr) {
            name_ptr++;
            pattern_ptr++;
        } else {
            return false;
        }
    }
    
    return (*name_ptr == '\0' && *pattern_ptr == '\0');
}
