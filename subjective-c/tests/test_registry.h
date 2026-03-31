#ifndef SUBJECTIVE_C_TEST_REGISTRY_H
#define SUBJECTIVE_C_TEST_REGISTRY_H

#include "memory.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef void (*SubjectiveCTestFn)(void);

#define SUBJECTIVE_C_TEST_HEAP_GROWTH_UNSPECIFIED ((size_t)-1)
#define SUBJECTIVE_C_TEST_HEAP_GROWTH_UNLIMITED ((size_t)-2)

// Test entry structure with groups and qualified names
typedef struct {
    const char *name;           // Test function name
    const char *qualified_name; // Fully qualified name (group/name)
    const char *group;          // Test group (derived from filename)
    const char *file;           // Source file path
    int line;                   // Source line number
    SubjectiveCTestFn fn;      // Test function pointer
    size_t heap_growth_limit_bytes; // Max allowed heap growth bytes
} SubjectiveCTestEntry;

// Basic registration (backward compatible)
void subjective_c_test_registry_add(const char *name, const char *file, int line, SubjectiveCTestFn fn);

// Extended registration with group and file info
void subjective_c_test_registry_add_with_file_info(const char *name, SubjectiveCTestFn fn, const char *group, const char *file, int line);
void subjective_c_test_registry_add_with_file_info_ex(const char *name, SubjectiveCTestFn fn, const char *group, const char *file, int line, size_t heap_growth_limit_bytes);

// Registry query functions
const SubjectiveCTestEntry* subjective_c_test_registry_entries(size_t *count);
SubjectiveCTestEntry* subjective_c_test_registry_find(const char *name);
SubjectiveCTestEntry* subjective_c_test_registry_find_by_qualified_name(const char *qualified_name);
SubjectiveCTestEntry* subjective_c_test_registry_find_by_pattern(const char *pattern);
SubjectiveCTestEntry* subjective_c_test_registry_get_by_group(const char *group, size_t *count);
void subjective_c_test_registry_list_all(void);
void subjective_c_test_registry_list_groups(void);

// Helper function to extract filename from __FILE__ (without path and extension)
char *subjective_c_test_extract_filename_from_path(const char *file_path);

// Pattern matching helper
bool subjective_c_test_name_matches_pattern(const char *name, const char *pattern);

#ifndef SUBJECTIVE_C_TEST_MAX
#define SUBJECTIVE_C_TEST_MAX 4096  // Allows full tiny-clj + startup/panel test registration.
#endif

// Always use WITH_AUTORELEASE_POOL for tests (it's defined in memory.h)
#define SUBJECTIVE_C_TEST_WITH_POOL(block) WITH_AUTORELEASE_POOL(block)

// TEST macro with automatic group extraction from filename
#define SUBJECTIVE_C_TEST_IMPL(name, heap_growth_limit_bytes) \
    static void name##_impl(void); \
    static void name(void) { \
        SUBJECTIVE_C_TEST_WITH_POOL({ name##_impl(); }); \
    } \
    static void register_##name(void) __attribute__((constructor, used)); \
    static void register_##name(void) { \
        char *filename = subjective_c_test_extract_filename_from_path(__FILE__); \
        if (filename) { \
            subjective_c_test_registry_add_with_file_info_ex(#name, name, filename, __FILE__, __LINE__, heap_growth_limit_bytes); \
            CLJ_FREE(filename); \
        } else { \
            subjective_c_test_registry_add(#name, __FILE__, __LINE__, name); \
        } \
    } \
    static void name##_impl(void)

#ifndef TEST_DEFAULT_HEAP_GROWTH_LIMIT
#define TEST_DEFAULT_HEAP_GROWTH_LIMIT SUBJECTIVE_C_TEST_HEAP_GROWTH_UNSPECIFIED
#endif

#define SUBJECTIVE_C_TEST_1(name) SUBJECTIVE_C_TEST_IMPL(name, TEST_DEFAULT_HEAP_GROWTH_LIMIT)
#define SUBJECTIVE_C_TEST_2(name, heap_growth_limit_bytes) SUBJECTIVE_C_TEST_IMPL(name, heap_growth_limit_bytes)
#define SUBJECTIVE_C_TEST_GET_MACRO(_1, _2, NAME, ...) NAME
#define TEST(...) SUBJECTIVE_C_TEST_GET_MACRO(__VA_ARGS__, SUBJECTIVE_C_TEST_2, SUBJECTIVE_C_TEST_1)(__VA_ARGS__)

#endif // SUBJECTIVE_C_TEST_REGISTRY_H
