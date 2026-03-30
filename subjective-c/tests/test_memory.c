#include "test_common.h"
#include "platform_allocated_size.h"
#include "hashmap.h"
#include <stdio.h>

/* Pseudo-test: measure malloc waste (allocated - requested) for FAM types at various sizes.
 * Run with: ./bin/subjective-c-tests -test "test_memory/fam_alloc_waste_measure"
 * Use output to derive round-up formula for capacity optimization. */
static void measure_one(const char *label, size_t requested, size_t allocated) {
    size_t waste = (allocated >= requested) ? (allocated - requested) : 0;
    (void)fprintf(stderr, "FAM_WASTE %s requested=%zu allocated=%zu waste=%zu\n",
                  label, requested, allocated, waste);
}

TEST(test_memory_fam_alloc_waste_measure) {
    /* Check if platform reports allocated size (malloc_size / heap_caps_get_allocated_size). */
    void *probe = malloc(8);
    size_t got = probe ? platform_allocated_size(probe) : 0;
    if (probe) free(probe);
    if (got == 0) {
        /* Platform has no allocated_size API; skip measurement. */
        TEST_PASS();
        return;
    }

    (void)fprintf(stderr, "FAM_WASTE --- Map (capacity -> requested, allocated, waste) ---\n");
    for (int cap = 1; cap <= 32; cap++) {
        CljPersistentMap *m = make_map(cap);
        if (!m || m->capacity == 0) continue; /* singleton */
        size_t req = sizeof(CljPersistentMap) + (size_t)m->capacity * 2 * sizeof(CljObject *);
        size_t alloc = platform_allocated_size(m);
        char buf[64];
        (void)snprintf(buf, sizeof(buf), "map_cap%d", m->capacity);
        measure_one(buf, req, alloc);
        RELEASE(m);
    }

    (void)fprintf(stderr, "FAM_WASTE --- Vector (capacity -> requested, allocated, waste) ---\n");
    for (unsigned int cap = 1; cap <= 32; cap++) {
        CljPersistentVector *v = make_vector(cap, STRONG);
        unsigned int actual_cap = vector_capacity(v);
        if (!v || actual_cap == 0) continue;
        size_t req = vector_requested_allocation_size(actual_cap);
        size_t alloc = platform_allocated_size(v);
        char buf[64];
        (void)snprintf(buf, sizeof(buf), "vec_cap%u", actual_cap);
        measure_one(buf, req, alloc);
        RELEASE(v);
    }

    (void)fprintf(stderr, "FAM_WASTE --- HashSet (requested capacity -> actual cap, requested, allocated, waste) ---\n");
    for (unsigned int req_cap = 1; req_cap <= 32; req_cap++) {
        CljHashSet *s = make_hashset(req_cap);
        if (!s) continue;
        size_t req = sizeof(CljHashSet) + (size_t)s->capacity * sizeof(CljObject *);
        size_t alloc = platform_allocated_size(s);
        char buf[64];
        (void)snprintf(buf, sizeof(buf), "hashset_req%u_cap%u", req_cap, s->capacity);
        measure_one(buf, req, alloc);
        RELEASE(s);
    }

    (void)fprintf(stderr, "FAM_WASTE --- HashMap (requested capacity -> actual cap, requested, allocated, waste) ---\n");
    for (unsigned int req_cap = 1; req_cap <= 32; req_cap++) {
        CljHashMap *h = make_hashmap(req_cap);
        if (!h) continue;
        size_t req = sizeof(CljHashMap) + (size_t)h->capacity * 2 * sizeof(CljObject *);
        size_t alloc = platform_allocated_size(h);
        char buf[64];
        (void)snprintf(buf, sizeof(buf), "hashmap_req%u_cap%u", req_cap, h->capacity);
        measure_one(buf, req, alloc);
        RELEASE(h);
    }

    (void)fprintf(stderr, "FAM_WASTE --- end ---\n");
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

// =============================================================================
// is_pointer_in_data_segment() — heap vs static C string literal (two tests).
// =============================================================================

TEST(test_is_pointer_in_data_segment_heap_false) {
    char *heap_ptr = (char *)malloc(16);
    TEST_ASSERT_NOT_NULL(heap_ptr);
    TEST_ASSERT_FALSE(is_pointer_in_data_segment(heap_ptr));
    free(heap_ptr);
}

TEST(test_is_pointer_in_data_segment_static_literal_true) {
    static const char *literal = "tiny-clj-static-literal";
    TEST_ASSERT_TRUE(is_pointer_in_data_segment(literal));
}
