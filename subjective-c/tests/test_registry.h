#ifndef SUBJECTIVE_C_TEST_REGISTRY_H
#define SUBJECTIVE_C_TEST_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>

typedef void (*SubjectiveCTestFn)(void);

typedef struct {
    const char *name;
    const char *file;
    int line;
    SubjectiveCTestFn fn;
} SubjectiveCTestEntry;

void subjective_c_test_registry_add(const char *name, const char *file, int line, SubjectiveCTestFn fn);
const SubjectiveCTestEntry* subjective_c_test_registry_entries(size_t *count);

#ifndef SUBJECTIVE_C_TEST_MAX
#define SUBJECTIVE_C_TEST_MAX 512
#endif

#ifdef WITH_AUTORELEASE_POOL
#define SUBJECTIVE_C_TEST_WITH_POOL(block) WITH_AUTORELEASE_POOL(block)
#else
#define SUBJECTIVE_C_TEST_WITH_POOL(block) do { block; } while(0)
#endif

#define TEST(name) \
    static void name##_impl(void); \
    static void name(void) { \
        SUBJECTIVE_C_TEST_WITH_POOL({ name##_impl(); }); \
    } \
    static void register_##name(void) __attribute__((constructor, used)); \
    static void register_##name(void) { \
        subjective_c_test_registry_add(#name, __FILE__, __LINE__, name); \
    } \
    static void name##_impl(void)

#endif // SUBJECTIVE_C_TEST_REGISTRY_H
