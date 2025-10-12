/*
 * Memory Leak Tests for Tiny-CLJ
 * 
 * Tests memory management for all major data structures.
 * Can be run with or without memory profiling enabled.
 */

#include "minunit.h"
#include "object.h"
#include "clj_string.h"
#include "vector.h"
#include "seq.h"
#include "map.h"
#include "function_call.h"
#include "clj_symbols.h"
#include "namespace.h"
#include "memory.h"
#include "memory_profiler.h"

// Memory profiler is now always enabled via memory_hooks.h and memory_profiler.h

#include <stdio.h>

// ============================================================================
// MEMORY LEAK TESTS
// ============================================================================

static char *test_basic_object_creation_memory(void) {
    
    WITH_AUTORELEASE_POOL({
        // Create some basic objects
        CljObject *int_obj = make_int(42);
        CljObject *float_obj = make_float(3.14);
        CljObject *str_obj = make_string("hello");
        
        mu_assert("int object created", int_obj != NULL);
        mu_assert("float object created", float_obj != NULL);
        mu_assert("string object created", str_obj != NULL);
        
        // Release objects
        RELEASE(int_obj);
        RELEASE(float_obj);
        RELEASE(str_obj);
    });
    
    return 0;
}

static char *test_vector_creation_memory(void) {
    
    WITH_AUTORELEASE_POOL({
    
    // Create a vector with multiple elements
    CljObject *vec = make_vector(10, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    mu_assert("vector created", vec != NULL);
    mu_assert("vector data valid", vec_data != NULL);
    
    // Add elements
    for (int i = 0; i < 10; i++) {
        CljObject *elem = make_int(i);
        vec_data->data[i] = elem;
    }
    vec_data->count = 10;
    
    // Test vector operations
    CljObject *first_elem = vec_data->data[0];
    mu_assert("first element accessible", first_elem != NULL);
    
    RELEASE(vec);
    
    });
    
    return 0;
}

static char *test_map_creation_memory(void) {
    
    WITH_AUTORELEASE_POOL({
    
    // Create a map with initial capacity
    CljObject *map = make_map(10);
    mu_assert("map created", map != NULL);
    mu_assert("map is correct type", map->type == CLJ_MAP);
    
    // Create keys and values
    CljObject *k1 = make_string("name");
    CljObject *v1 = make_string("Alice");
    CljObject *k2 = make_string("age");
    CljObject *v2 = make_int(30);
    CljObject *k3 = make_string("city");
    CljObject *v3 = make_string("Berlin");
    
    // Add multiple key-value pairs (map_assoc modifies in-place)
    map_assoc(map, k1, v1);
    mu_assert("map still valid after first assoc", map != NULL);
    
    map_assoc(map, k2, v2);
    mu_assert("map still valid after second assoc", map != NULL);
    
    map_assoc(map, k3, v3);
    mu_assert("map still valid after third assoc", map != NULL);
    
    // Test map retrieval
    CljObject *retrieved = map_get(map, k1);
    mu_assert("retrieved value from map", retrieved != NULL);
    mu_assert("retrieved correct value", retrieved == v1);
    
    // Test map size
    int count = map_count(map);
    mu_assert("map has correct count", count == 3);
    
    // Test map_contains
    mu_assert("map contains k1", map_contains(map, k1));
    mu_assert("map contains k2", map_contains(map, k2));
    
    // Release all objects
    RELEASE(map);
    RELEASE(k1);
    RELEASE(v1);
    RELEASE(k2);
    RELEASE(v2);
    RELEASE(k3);
    RELEASE(v3);
    
    });
    
    return 0;
}

static char *test_seq_iteration_memory(void) {
    
    WITH_AUTORELEASE_POOL({
    
    // Create a test vector
    CljObject *vec = make_vector(5, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    for (int i = 0; i < 5; i++) {
        vec_data->data[i] = make_int(i * 10);
    }
    vec_data->count = 5;
    
    // Iterate using seq
    SeqIterator it;
    seq_iter_init(&it, vec);
    int count = 0;
    while (!seq_iter_empty(&it)) {
        CljObject *elem = seq_iter_first(&it);
        mu_assert("seq element not null", elem != NULL);
        count++;
        seq_iter_next(&it);
    }
    
    mu_assert("seq iterated correct count", count == 5);
    
    RELEASE(vec);
    
    });
    
    return 0;
}

// ============================================================================
// TEST SUITE RUNNER
// ============================================================================

char *run_memory_tests(void) {
    mu_run_test(test_basic_object_creation_memory);
    mu_run_test(test_vector_creation_memory);
    mu_run_test(test_map_creation_memory);
    mu_run_test(test_seq_iteration_memory);
    
    return 0;
}

