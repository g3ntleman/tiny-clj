#ifndef SUBJECTIVE_C_TEST_REGISTRY_H
#define SUBJECTIVE_C_TEST_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>

typedef void (*SubjectiveCTestFn)(void);

// Test entry structure with groups and qualified names
typedef struct {
    const char *name;           // Test function name
    const char *qualified_name; // Fully qualified name (group/name)
    const char *group;          // Test group (derived from filename)
    const char *file;           // Source file path
    int line;                   // Source line number
    SubjectiveCTestFn fn;      // Test function pointer
} SubjectiveCTestEntry;

// Basic registration (backward compatible)
void subjective_c_test_registry_add(const char *name, const char *file, int line, SubjectiveCTestFn fn);

// Extended registration with group and file info
void subjective_c_test_registry_add_with_file_info(const char *name, SubjectiveCTestFn fn, const char *group, const char *file, int line);

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
#define SUBJECTIVE_C_TEST_MAX 2048  // Increased for tiny-clj (has many tests)
#endif

// Always use WITH_AUTORELEASE_POOL for tests (it's defined in memory.h)
#define SUBJECTIVE_C_TEST_WITH_POOL(block) WITH_AUTORELEASE_POOL(block)

// TEST macro with automatic group extraction from filename
#define TEST(name) \
    static void name##_impl(void); \
    static void name(void) { \
        SUBJECTIVE_C_TEST_WITH_POOL({ name##_impl(); }); \
    } \
    static void register_##name(void) __attribute__((constructor, used)); \
    static void register_##name(void) { \
        char *filename = subjective_c_test_extract_filename_from_path(__FILE__); \
        if (filename) { \
            subjective_c_test_registry_add_with_file_info(#name, name, filename, __FILE__, __LINE__); \
            free(filename); \
        } else { \
            subjective_c_test_registry_add(#name, __FILE__, __LINE__, name); \
        } \
    } \
    static void name##_impl(void)

#endif // SUBJECTIVE_C_TEST_REGISTRY_H
