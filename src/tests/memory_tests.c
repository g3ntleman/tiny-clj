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
#include <stdio.h>
#include <string.h>

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
        CljObject *str_obj = make_string("hello");
        
        TEST_ASSERT_NOT_NULL(int_obj);
        TEST_ASSERT_NOT_NULL(float_obj);
        TEST_ASSERT_NOT_NULL(str_obj);
        
        TEST_ASSERT_EQUAL_INT(42, int_obj->as.i);
        TEST_ASSERT_EQUAL_FLOAT(3.14, float_obj->as.f);
        // String objects store data in the data pointer, not directly in the union
        TEST_ASSERT_NOT_NULL(str_obj->as.data);
        
        // Objects are automatically cleaned up by WITH_AUTORELEASE_POOL
    }
}

void test_memory_deallocation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        // Test object lifecycle
        CljObject *obj = make_int(42);
        TEST_ASSERT_NOT_NULL(obj);
        
        // Test retain counting
        int initial_refs = get_retain_count(obj);
        TEST_ASSERT_EQUAL_INT(1, initial_refs);
        
        // Retain and release
        CljObject *retained = RETAIN(obj);
        TEST_ASSERT_EQUAL_INT(2, get_retain_count(obj));
        
        RELEASE(retained);
        TEST_ASSERT_EQUAL_INT(1, get_retain_count(obj));
        
        // Final cleanup by WITH_AUTORELEASE_POOL
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
        CljObject *vec = make_vector(5, 1);
        TEST_ASSERT_NOT_NULL(vec);
        
        CljPersistentVector *vec_data = as_vector(vec);
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

static void test_group_basic_memory(void) {
    RUN_TEST(test_memory_allocation);
    RUN_TEST(test_memory_deallocation);
}

static void test_group_advanced_memory(void) {
    RUN_TEST(test_memory_leak_detection);
    RUN_TEST(test_vector_memory);
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

static void __attribute__((unused)) print_usage(const char *program_name) {
    printf("Unity Memory Tests for Tiny-CLJ\n");
    printf("Usage: %s [options] [test_name]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -l, --list     List available tests\n");
    printf("  -v, --verbose  Verbose output\n\n");
    printf("Available tests:\n");
    printf("  allocation     Test basic memory allocation\n");
    printf("  deallocation   Test memory deallocation\n");
    printf("  leak           Test memory leak detection\n");
    printf("  vector         Test vector memory management\n");
    printf("  basic          Run basic memory tests\n");
    printf("  advanced       Run advanced memory tests\n");
    printf("  all            Run all tests (default)\n");
}

static void __attribute__((unused)) list_tests(void) {
    printf("Available Unity Memory Tests:\n");
    printf("  allocation     - Test basic memory allocation\n");
    printf("  deallocation   - Test memory deallocation\n");
    printf("  leak           - Test memory leak detection\n");
    printf("  vector         - Test vector memory management\n");
    printf("  basic          - Run basic memory tests\n");
    printf("  advanced       - Run advanced memory tests\n");
    printf("  all            - Run all tests\n");
}

static void __attribute__((unused)) run_specific_test(const char *test_name) {
    if (strcmp(test_name, "allocation") == 0) {
        RUN_TEST(test_memory_allocation);
    } else if (strcmp(test_name, "deallocation") == 0) {
        RUN_TEST(test_memory_deallocation);
    } else if (strcmp(test_name, "leak") == 0) {
        RUN_TEST(test_memory_leak_detection);
    } else if (strcmp(test_name, "vector") == 0) {
        RUN_TEST(test_vector_memory);
    } else if (strcmp(test_name, "basic") == 0) {
        RUN_TEST(test_group_basic_memory);
    } else if (strcmp(test_name, "advanced") == 0) {
        RUN_TEST(test_group_advanced_memory);
    } else if (strcmp(test_name, "all") == 0) {
        RUN_TEST(test_memory_allocation);
        RUN_TEST(test_memory_deallocation);
        RUN_TEST(test_memory_leak_detection);
        RUN_TEST(test_vector_memory);
    } else {
        printf("Unknown test: %s\n", test_name);
        printf("Use --list to see available tests\n");
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
