/*
 * ALLOC Macros Unity Test
 * 
 * Tests the allocation macros of Tiny-Clj:
 * - STACK_ALLOC for stack allocation
 * - ALLOC for heap allocation
 * - ALLOC_ZERO for zero-initialized heap allocation
 * - Memory management and cleanup
 */

#include "../unity.h"
#include "../CljObject.h"
#include "test_helpers.h"
#include "../runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNIT_TEST_RUNNER
void setUp(void) {
    // Initialize symbol table
    symbol_table_cleanup();
}

void tearDown(void) {
    // Cleanup if needed
}
#endif

void test_stack_alloc(void) {
    // Test STACK_ALLOC for stack allocation
    CljObject **stack_array = STACK_ALLOC(CljObject*, 5);
    
    TEST_ASSERT_NOT_NULL(stack_array);
    
    // Initialize array elements
    for (int i = 0; i < 5; i++) {
        stack_array[i] = autorelease(make_int(i));
        ASSERT_OBJ_INT_EQ(stack_array[i], i);
    }
    
    // Array is automatically freed when leaving scope
}

void test_heap_alloc(void) {
    // Test ALLOC for heap allocation
    CljObject **heap_array = ALLOC(CljObject*, 3);
    
    TEST_ASSERT_NOT_NULL(heap_array);
    
    // Initialize array elements
    for (int i = 0; i < 3; i++) {
        heap_array[i] = autorelease(make_string("test"));
        ASSERT_TYPE(heap_array[i], CLJ_STRING);
    }
    
    // Manual cleanup required
    free(heap_array);
}

void test_alloc_zero(void) {
    // Test ALLOC_ZERO for zero-initialized heap allocation
    int *zero_array = ALLOC_ZERO(int, 10);
    
    TEST_ASSERT_NOT_NULL(zero_array);
    
    // Check that array is zero-initialized
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, zero_array[i]);
    }
    
    // Manual cleanup required
    free(zero_array);
}

void test_mixed_allocation(void) {
    // Test mixing different allocation types
    CljObject **stack_objs = STACK_ALLOC(CljObject*, 2);
    CljObject **heap_objs = ALLOC(CljObject*, 2);
    
    TEST_ASSERT_NOT_NULL(stack_objs);
    TEST_ASSERT_NOT_NULL(heap_objs);
    
    // Create objects using different allocation methods
    stack_objs[0] = autorelease(make_int(42));
    stack_objs[1] = autorelease(make_string("stack"));
    
    heap_objs[0] = autorelease(make_int(24));
    heap_objs[1] = autorelease(make_string("heap"));
    
    // Verify objects
    ASSERT_OBJ_INT_EQ(stack_objs[0], 42);
    ASSERT_TYPE(stack_objs[1], CLJ_STRING);
    ASSERT_OBJ_CSTR_EQ(stack_objs[1], "stack");
    
    ASSERT_OBJ_INT_EQ(heap_objs[0], 24);
    ASSERT_TYPE(heap_objs[1], CLJ_STRING);
    ASSERT_OBJ_CSTR_EQ(heap_objs[1], "heap");
    
    // Cleanup heap allocation
    free(heap_objs);
}

void test_allocation_with_autorelease(void) {
    // Test that autorelease works with different allocation types
    CljObject **objs = STACK_ALLOC(CljObject*, 3);
    
    TEST_ASSERT_NOT_NULL(objs);
    
    // Create objects with autorelease
    objs[0] = autorelease(make_int(1));
    objs[1] = autorelease(make_string("test"));
    objs[2] = autorelease(clj_true());
    
    // Verify objects are properly created
    TEST_ASSERT_NOT_NULL(objs[0]);
    TEST_ASSERT_NOT_NULL(objs[1]);
    TEST_ASSERT_NOT_NULL(objs[2]);
    ASSERT_TYPE(objs[0], CLJ_INT);
    ASSERT_TYPE(objs[1], CLJ_STRING);
    ASSERT_TYPE(objs[2], CLJ_BOOL);
}

void test_large_allocation(void) {
    // Test allocation of larger arrays
    const int size = 1000;
    int *large_array = ALLOC_ZERO(int, size);
    
    TEST_ASSERT_NOT_NULL(large_array);
    
    // Initialize array
    for (int i = 0; i < size; i++) {
        large_array[i] = i;
    }
    
    // Verify array
    for (int i = 0; i < size; i++) {
        TEST_ASSERT_EQUAL_INT(i, large_array[i]);
    }
    
    // Cleanup
    free(large_array);
}

// moved to test runner (test_unit_main.c)
