#include "test_common.h"

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    // Initialize autorelease pool before running tests
    autorelease_pool_init();
    
    size_t count = 0;
    const SubjectiveCTestEntry *entries = subjective_c_test_registry_entries(&count);

    UNITY_BEGIN();
    for (size_t i = 0; i < count; ++i) {
        const SubjectiveCTestEntry *entry = &entries[i];
        UnityDefaultTestRun(entry->fn, entry->name, entry->line);
    }
    return UNITY_END();
}

