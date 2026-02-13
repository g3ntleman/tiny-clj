/**
 * @file test_registry.c
 * @brief Test registry implementation with dynamic allocation and grouping.
 */

#include "test_registry.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <external/tiny_regex.h>

static SubjectiveCTestEntry *g_entries = NULL;
static size_t g_entry_count = 0;
static size_t g_entry_capacity = 0;

#define INITIAL_CAPACITY 64

/**
 * @brief Create qualified test name (group/name, strips "test_" prefix).
 * @param group Group name
 * @param name Test name
 * @return Allocated qualified name, caller must free
 */
static char* create_qualified_name(const char *group, const char *name) {
    size_t group_len = strlen(group);
    
    // Remove "test_" prefix if present
    const char *name_start = name;
    if (strncmp(name, "test_", 5) == 0) {
        name_start = name + 5;
    }
    
    size_t name_len = strlen(name_start);
    size_t total_len = group_len + 1 + name_len; // group + '/' + name
    
    char *qualified = (char*)CLJ_MALLOC(total_len + 1);
    
    snprintf(qualified, total_len + 1, "%s/%s", group, name_start);
    return qualified;
}

/**
 * @brief Register test with basic info (backward compatible).
 * @param name Test name
 * @param file Source file
 * @param line Source line
 * @param fn Test function pointer
 */
void subjective_c_test_registry_add(const char *name, const char *file, int line, SubjectiveCTestFn fn) {
    if (!name || !fn) {
        fprintf(stderr, "subjective-c test registration error: invalid entry\n");
        abort();
    }
    
    // Use "unknown" as group for backward compatibility
    subjective_c_test_registry_add_with_file_info(name, fn, "unknown", file, line);
}

/**
 * @brief Register test with group info (delegates to _ex).
 * @param name Test name
 * @param fn Test function
 * @param group Test group
 * @param file Source file
 * @param line Source line
 */
void subjective_c_test_registry_add_with_file_info(const char *name, SubjectiveCTestFn fn, const char *group, const char *file, int line) {
    subjective_c_test_registry_add_with_file_info_ex(name, fn, group, file, line, SUBJECTIVE_C_TEST_HEAP_GROWTH_UNSPECIFIED);
}

/**
 * @brief Register test with full metadata including heap limit.
 * @param name Test name
 * @param fn Test function
 * @param group Test group
 * @param file Source file
 * @param line Source line
 * @param heap_growth_limit_bytes Max heap growth allowed (or _UNSPECIFIED/_UNLIMITED)
 */
void subjective_c_test_registry_add_with_file_info_ex(const char *name, SubjectiveCTestFn fn, const char *group, const char *file, int line, size_t heap_growth_limit_bytes) {
    if (!name || !fn) {
        fprintf(stderr, "subjective-c test registration error: invalid entry\n");
        abort();
    }
    
    // Grow registry if needed
    if (g_entry_count >= g_entry_capacity) {
        size_t new_capacity = g_entry_capacity == 0 ? INITIAL_CAPACITY : g_entry_capacity * 2;
        SubjectiveCTestEntry *new_entries = (SubjectiveCTestEntry*)CLJ_REALLOC(g_entries, new_capacity * sizeof(SubjectiveCTestEntry));
        g_entries = new_entries;
        g_entry_capacity = new_capacity;
    }
    
    if (g_entry_count >= SUBJECTIVE_C_TEST_MAX) {
        fprintf(stderr, "subjective-c test registry overflow (max=%d)\n", SUBJECTIVE_C_TEST_MAX);
        abort();
    }
    
    // Copy strings to ensure they're permanently stored
    size_t name_len = strlen(name);
    char *name_copy = (char*)CLJ_MALLOC(name_len + 1);
    strcpy(name_copy, name);
    
    size_t group_len = strlen(group);
    char *group_copy = (char*)CLJ_MALLOC(group_len + 1);
    strcpy(group_copy, group);
    
    char *qualified_name = create_qualified_name(group_copy, name);
    if (!qualified_name) {
        CLJ_FREE(group_copy);
        CLJ_FREE(name_copy);
        fprintf(stderr, "subjective-c test registry: failed to create qualified name\n");
        abort();
    }
    
    char *file_copy = NULL;
    if (file) {
        size_t file_len = strlen(file);
        file_copy = (char*)CLJ_MALLOC(file_len + 1);
        strcpy(file_copy, file);
    }
    
    // Add entry
    g_entries[g_entry_count].name = name_copy;
    g_entries[g_entry_count].qualified_name = qualified_name;
    g_entries[g_entry_count].group = group_copy;
    g_entries[g_entry_count].file = file_copy;
    g_entries[g_entry_count].line = line;
    g_entries[g_entry_count].fn = fn;
    g_entries[g_entry_count].heap_growth_limit_bytes = heap_growth_limit_bytes;
    g_entry_count++;
}

/**
 * @brief Get all registered test entries.
 * @param count Output parameter for entry count (may be NULL)
 * @return Array of test entries
 */
const SubjectiveCTestEntry* subjective_c_test_registry_entries(size_t *count) {
    if (count) {
        *count = g_entry_count;
    }
    return g_entries;
}

/**
 * @brief Find test by simple name.
 * @param name Test name to find
 * @return Test entry or NULL if not found
 */
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

/**
 * @brief Find first test matching pattern (supports * wildcard).
 * @param pattern Pattern to match
 * @return First matching test entry or NULL
 */
SubjectiveCTestEntry* subjective_c_test_registry_find_by_pattern(const char *pattern) {
    for (size_t i = 0; i < g_entry_count; i++) {
        if (subjective_c_test_name_matches_pattern(g_entries[i].qualified_name, pattern)) {
            return &g_entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Get all tests in a group (returns allocated array, caller frees).
 * @param group Group name
 * @param count Output parameter for array size
 * @return Allocated array of test entries or NULL
 */
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
    
    SubjectiveCTestEntry *filtered = (SubjectiveCTestEntry*)CLJ_MALLOC(filtered_count * sizeof(SubjectiveCTestEntry));
    
    size_t idx = 0;
    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].group, group) == 0) {
            filtered[idx++] = g_entries[i];
        }
    }
    
    *count = filtered_count;
    return filtered;
}

/** @brief Print all registered tests to stdout. */
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

/**
 * @brief Extract filename stem from path (strips directory and extension).
 * @param file_path File path to parse
 * @return Allocated filename stem or "unknown", caller must free
 */
char *subjective_c_test_extract_filename_from_path(const char *file_path) {
    if (!file_path) {
        char *result = (char*)CLJ_MALLOC(8);
        strcpy(result, "unknown");
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
        char *result = (char*)CLJ_MALLOC(len + 1);
        strncpy(result, filename, len);
        result[len] = '\0';
        return result;
    }
    
    size_t len = strlen(filename);
    char *result = (char*)CLJ_MALLOC(len + 1);
    strcpy(result, filename);
    return result;
}

/**
 * @brief Match name against pattern with * wildcard support.
 * @param name Name to match
 * @param pattern Pattern (* matches any sequence)
 * @return true if match, false otherwise
 */
bool subjective_c_test_name_matches_pattern(const char *name, const char *pattern) {
    if (!name || !pattern) return false;

    size_t pattern_len = strlen(pattern);
    /* Worst case: every input char needs escaping (+1), plus ^, $, and terminator. */
    size_t max_regex_len = (pattern_len * 2) + 3;
    char *regex_pattern = (char*)CLJ_MALLOC(max_regex_len);
    if (!regex_pattern) return false;

    size_t out = 0;
    regex_pattern[out++] = '^';

    for (size_t i = 0; i < pattern_len; i++) {
        char c = pattern[i];
        if (c == '*') {
            regex_pattern[out++] = '.';
            regex_pattern[out++] = '*';
            continue;
        }

        switch (c) {
            case '.': case '^': case '$': case '+': case '?':
            case '[': case ']': case '\\':
                regex_pattern[out++] = '\\';
                break;
            default:
                break;
        }
        regex_pattern[out++] = c;
    }

    regex_pattern[out++] = '$';
    regex_pattern[out] = '\0';

    int match_len = 0;
    bool matched = (re_match(regex_pattern, name, &match_len) == 0);

    CLJ_FREE(regex_pattern);
    return matched;
}
