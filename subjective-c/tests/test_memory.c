#include "test_common.h"
#include "platform_allocated_size.h"
#include "memory.h"
#include "map.h"
#include "vector.h"
#include "hashmap.h"
#include "hashset.h"
#include <stdio.h>

/* On ESP32: assert that FAM allocators use round_up_to_fam_granularity so
 * requested size equals allocated size. Off ESP32 or when platform has no
 * allocated_size API: skip assertions. */
TEST(test_memory_fam_alloc_waste_measure) {
    void *probe = malloc(8);
    size_t got = probe ? platform_allocated_size(probe) : 0;
    if (probe) free(probe);
    if (got == 0) {
        TEST_PASS();
        return;
    }

#if defined(ESP32_BUILD)
    /* ESP32: allocated must equal the size we request (round_up_to_fam_granularity). */
    for (int cap = 1; cap <= 32; cap++) {
        CljPersistentMap *m = make_map(cap);
        if (!m || m->capacity == 0) continue;
        size_t expected = round_up_to_fam_granularity(
            sizeof(CljPersistentMap) + (size_t)m->capacity * 2 * sizeof(CljObject *));
        size_t allocated = platform_allocated_size(m);
        TEST_ASSERT_TRUE(expected == allocated);
        RELEASE(m);
    }

    for (unsigned int cap = 1; cap <= 32; cap++) {
        CljPersistentVector *v = make_vector(cap, STRONG);
        unsigned int actual_cap = vector_capacity(v);
        if (!v || actual_cap == 0) continue;
        size_t expected = round_up_to_fam_granularity(
            sizeof(CljPersistentVector) + (size_t)actual_cap * sizeof(ID));
        size_t allocated = platform_allocated_size(v);
        TEST_ASSERT_TRUE(expected == allocated);
        RELEASE(v);
    }

    for (unsigned int req_cap = 1; req_cap <= 32; req_cap++) {
        CljHashSet *s = make_hashset(req_cap);
        if (!s) continue;
        size_t expected = round_up_to_fam_granularity(
            sizeof(CljHashSet) + (size_t)s->capacity * sizeof(CljObject *));
        size_t allocated = platform_allocated_size(s);
        TEST_ASSERT_TRUE(expected == allocated);
        RELEASE(s);
    }

    for (unsigned int req_cap = 1; req_cap <= 32; req_cap++) {
        CljHashMap *h = make_hashmap(req_cap);
        if (!h) continue;
        size_t expected = round_up_to_fam_granularity(
            sizeof(CljHashMap) + (size_t)h->capacity * 2 * sizeof(CljObject *));
        size_t allocated = platform_allocated_size(h);
        TEST_ASSERT_TRUE(expected == allocated);
        RELEASE(h);
    }
#endif

    TEST_PASS();
}

TEST(test_nested_autorelease_pools) {
    CljString *outer = NULL, *inner = NULL;
    WITH_AUTORELEASE_POOL({
        outer = make_string("outer");
        RETAIN(outer);
        AUTORELEASE(outer);
        TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(outer));
        inner = make_string("inner");
        RETAIN(inner);
        AUTORELEASE(inner);
        TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(inner));
    });
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(outer));
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(inner));
    RELEASE(inner);
    RELEASE(outer);
}

TEST(test_autorelease_pool_many_objects) {
    CljString *handles[100];
    WITH_AUTORELEASE_POOL({
        for (int i = 0; i < 100; i++) {
            handles[i] = make_string("test");
            RETAIN(handles[i]);
            AUTORELEASE(handles[i]);
        }
    });
    for (int i = 0; i < 100; i++) RELEASE(handles[i]);
}

TEST(test_retain_release_reference_count) {
    CljString *s = make_string("retain-release");
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(s));

    RETAIN(s);
    TEST_ASSERT_EQUAL_INT(2, REFERENCE_COUNT(s));

    RELEASE(s);
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(s));

    RELEASE(s); // final release
}

TEST(test_autorelease_pool_drains_objects) {
    CljString *s = NULL;
    WITH_AUTORELEASE_POOL({
        s = make_string("autorelease");
        RETAIN(s);
        AUTORELEASE(s);
    });
    TEST_ASSERT_EQUAL_INT(1, REFERENCE_COUNT(s));
    RELEASE(s);
}

// =============================================================================
// is_pointer_on_stack() Tests
// =============================================================================

TEST(test_is_pointer_on_stack_local_var) {
    int local_var = 42;
    TEST_ASSERT_TRUE(is_pointer_on_stack(&local_var));
}

TEST(test_is_pointer_on_stack_heap_object) {
    CljString *heap_obj = make_string("heap");
    TEST_ASSERT_FALSE(is_pointer_on_stack(heap_obj));
    RELEASE(heap_obj);
}

TEST(test_is_pointer_on_stack_null) {
    TEST_ASSERT_FALSE(is_pointer_on_stack(NULL));
}

TEST(test_is_pointer_on_stack_static_var) {
    static int static_var = 42;
    TEST_ASSERT_FALSE(is_pointer_on_stack(&static_var));
}

TEST(test_is_pointer_on_stack_caller_frame) {
    // Test that we can detect a variable from a caller's stack frame
    int caller_local = 123;
    
    // Simulate what happens in a nested function call
    // The caller's local should still be detected as on-stack
    volatile int *ptr = &caller_local;
    TEST_ASSERT_TRUE(is_pointer_on_stack((void*)ptr));
}
