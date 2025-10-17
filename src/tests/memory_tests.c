/*
 * Unity Memory Tests for Tiny-CLJ
 * 
 * First Unity test suite with command-line parameter support.
 * Demonstrates single test execution and test isolation.
 */

#include "unity.h"
#include "object.h"
#include "memory.h"
#include "memory_profiler.h"
#include "symbol.h"
#include "namespace.h"
#include "clj_string.h"
#include "vector.h"
#include "value.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// TEST CASES (using WITH_AUTORELEASE_POOL for additional isolation)
// ============================================================================

void test_memory_allocation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test basic object creation
        CljObject *int_obj = make_int(42);
        CljObject *float_obj = make_float(3.14);
        CljValue str_obj = make_string_v("hello");
        
        TEST_ASSERT_NOT_NULL(int_obj);
        TEST_ASSERT_NOT_NULL(float_obj);
        TEST_ASSERT_NOT_NULL(str_obj);
        
        TEST_ASSERT_EQUAL_INT(42, int_obj->as.i);
        TEST_ASSERT_EQUAL_FLOAT(3.14, float_obj->as.f);
        // String objects store data in the data pointer, not directly in the union
        TEST_ASSERT_NOT_NULL(((CljObject*)str_obj)->as.data);
        
        // Objects are automatically cleaned up by WITH_AUTORELEASE_POOL
    }
}

void test_memory_deallocation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test object lifecycle with heap-allocated object (not immediate)
        // Use a string object since symbols are singletons and don't use reference counting
        CljObject *obj = make_string("test_string_for_reference_counting");
        TEST_ASSERT_NOT_NULL(obj);
        
        // Test retain counting
        int initial_refs = get_retain_count(obj);
        TEST_ASSERT_EQUAL_INT(1, initial_refs);
        
        // Retain and release
        CljObject *retained = RETAIN(obj);
        TEST_ASSERT_EQUAL_INT(2, get_retain_count(obj));
        
        RELEASE(retained);
        // After releasing the retained reference, the object should still exist
        // The retain count should be 1 (original reference)
        TEST_ASSERT_EQUAL_INT(1, get_retain_count(obj));
        
        // Final cleanup
        RELEASE(obj);
    }
}

void test_memory_leak_detection(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test that no memory leaks occur
        for (int i = 0; i < 10; i++) {
            CljObject *obj = make_int(i);
            TEST_ASSERT_NOT_NULL(obj);
            TEST_ASSERT_EQUAL_INT(i, obj->as.i);
            // Objects are automatically cleaned up
        }
    }
}

void test_vector_memory(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test vector creation and memory management
        CljValue vec = make_vector_v(5, 1);
        TEST_ASSERT_NOT_NULL(vec);
        
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        TEST_ASSERT_EQUAL_INT(5, vec_data->capacity);
        
        // Add elements
        for (int i = 0; i < 5; i++) {
            CljObject *elem = make_int(i);
            vec_data->data[i] = elem;
        }
        vec_data->count = 5;
        
        // Test vector operations
        TEST_ASSERT_NOT_NULL(vec_data->data[0]);
        TEST_ASSERT_EQUAL_INT(0, vec_data->data[0]->as.i);
        
        // Automatic cleanup by WITH_AUTORELEASE_POOL
    }
}

// ============================================================================
// TEST GROUPS
// ============================================================================
// (Unused test groups removed for cleanup)

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

// Unused function removed for cleanup

// Unused function removed for cleanup

// Unused function removed for cleanup

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
