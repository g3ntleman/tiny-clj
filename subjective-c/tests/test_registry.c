#include "test_registry.h"

#include <stdio.h>
#include <stdlib.h>

static SubjectiveCTestEntry g_entries[SUBJECTIVE_C_TEST_MAX];
static size_t g_entry_count = 0;

void subjective_c_test_registry_add(const char *name, const char *file, int line, SubjectiveCTestFn fn) {
    if (!name || !fn) {
        fprintf(stderr, "subjective-c test registration error: invalid entry\n");
        abort();
    }
    if (g_entry_count >= SUBJECTIVE_C_TEST_MAX) {
        fprintf(stderr, "subjective-c test registry overflow (max=%d)\n", SUBJECTIVE_C_TEST_MAX);
        abort();
    }
    g_entries[g_entry_count].name = name;
    g_entries[g_entry_count].file = file;
    g_entries[g_entry_count].line = line;
    g_entries[g_entry_count].fn = fn;
    g_entry_count++;
}

const SubjectiveCTestEntry* subjective_c_test_registry_entries(size_t *count) {
    if (count) {
        *count = g_entry_count;
    }
    return g_entries;
}
