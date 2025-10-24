/*
 * Unity Byte Array Tests for Tiny-CLJ
 * 
 * Tests for mutable byte array implementation (Clojure-compatible)
 */

#include "unity/src/unity.h"
#include "../object.h"
#include "../memory.h"
#include "../byte_array.h"
#include "../value.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// TEST CASES
// ============================================================================

void test_byte_array_creation(void) {
    // Test creating byte array with size
    CljValue arr = make_byte_array(10);
    TEST_ASSERT_NOT_NULL(arr);
    
    CljByteArray *ba = as_byte_array(arr);
    TEST_ASSERT_NOT_NULL(ba);
    TEST_ASSERT_EQUAL_INT(10, ba->length);
    TEST_ASSERT_NOT_NULL(ba->data);
    
    // Verify zero-initialization
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, ba->data[i]);
    }
    
    RELEASE((CljObject*)arr);
}

void test_byte_array_from_bytes(void) {
    // Test creating byte array from existing data
    uint8_t data[] = {1, 2, 3, 4, 5};
    CljValue arr = make_byte_array_from_bytes(data, 5);
    TEST_ASSERT_NOT_NULL(arr);
    
    CljByteArray *ba = as_byte_array(arr);
    TEST_ASSERT_EQUAL_INT(5, ba->length);
    
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(data[i], ba->data[i]);
    }
    
    RELEASE((CljObject*)arr);
}

void test_byte_array_get_set(void) {
    // Test reading and writing bytes
    CljValue arr = make_byte_array(5);
    TEST_ASSERT_NOT_NULL(arr);
    
    // Write values
    byte_array_set(arr, 0, 42);
    byte_array_set(arr, 1, 255);
    byte_array_set(arr, 2, 0);
    byte_array_set(arr, 3, 128);
    byte_array_set(arr, 4, 200);
    
    // Read values
    TEST_ASSERT_EQUAL_UINT8(42, byte_array_get(arr, 0));
    TEST_ASSERT_EQUAL_UINT8(255, byte_array_get(arr, 1));
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(arr, 2));
    TEST_ASSERT_EQUAL_UINT8(128, byte_array_get(arr, 3));
    TEST_ASSERT_EQUAL_UINT8(200, byte_array_get(arr, 4));
    
    RELEASE((CljObject*)arr);
}

void test_byte_array_length(void) {
    // Test length function
    CljValue arr1 = make_byte_array(0);
    TEST_ASSERT_EQUAL_INT(0, byte_array_length(arr1));
    RELEASE((CljObject*)arr1);
    
    CljValue arr2 = make_byte_array(10);
    TEST_ASSERT_EQUAL_INT(10, byte_array_length(arr2));
    RELEASE((CljObject*)arr2);
    
    CljValue arr3 = make_byte_array(1000);
    TEST_ASSERT_EQUAL_INT(1000, byte_array_length(arr3));
    RELEASE((CljObject*)arr3);
}

void test_byte_array_clone(void) {
    // Test cloning
    CljValue arr1 = make_byte_array(5);
    byte_array_set(arr1, 0, 10);
    byte_array_set(arr1, 1, 20);
    byte_array_set(arr1, 2, 30);
    byte_array_set(arr1, 3, 40);
    byte_array_set(arr1, 4, 50);
    
    CljValue arr2 = byte_array_clone(arr1);
    TEST_ASSERT_NOT_NULL(arr2);
    TEST_ASSERT_NOT_EQUAL(arr1, arr2); // Different pointers
    
    // Verify data was copied
    TEST_ASSERT_EQUAL_INT(5, byte_array_length(arr2));
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(byte_array_get(arr1, i), byte_array_get(arr2, i));
    }
    
    // Modify clone and verify original unchanged
    byte_array_set(arr2, 0, 99);
    TEST_ASSERT_EQUAL_UINT8(10, byte_array_get(arr1, 0));
    TEST_ASSERT_EQUAL_UINT8(99, byte_array_get(arr2, 0));
    
    RELEASE((CljObject*)arr1);
    RELEASE((CljObject*)arr2);
}

void test_byte_array_copy_from(void) {
    // Test copying from C array to byte array
    CljValue arr = make_byte_array(10);
    uint8_t data[] = {1, 2, 3, 4, 5};
    
    byte_array_copy_from(arr, 2, data, 5);
    
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(arr, 0));
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(arr, 1));
    TEST_ASSERT_EQUAL_UINT8(1, byte_array_get(arr, 2));
    TEST_ASSERT_EQUAL_UINT8(2, byte_array_get(arr, 3));
    TEST_ASSERT_EQUAL_UINT8(3, byte_array_get(arr, 4));
    TEST_ASSERT_EQUAL_UINT8(4, byte_array_get(arr, 5));
    TEST_ASSERT_EQUAL_UINT8(5, byte_array_get(arr, 6));
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(arr, 7));
    
    RELEASE((CljObject*)arr);
}

void test_byte_array_copy_to(void) {
    // Test copying from byte array to C array
    CljValue arr = make_byte_array(5);
    for (int i = 0; i < 5; i++) {
        byte_array_set(arr, i, (uint8_t)(i * 10));
    }
    
    uint8_t dest[5] = {0};
    byte_array_copy_to(arr, 0, dest, 5);
    
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(i * 10, dest[i]);
    }
    
    RELEASE((CljObject*)arr);
}

void test_byte_array_copy_between(void) {
    // Test copying between two byte arrays
    CljValue src = make_byte_array(10);
    CljValue dest = make_byte_array(10);
    
    for (int i = 0; i < 10; i++) {
        byte_array_set(src, i, (uint8_t)(i + 100));
    }
    
    byte_array_copy(dest, 3, src, 2, 5);
    
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(dest, 0));
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(dest, 1));
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(dest, 2));
    TEST_ASSERT_EQUAL_UINT8(102, byte_array_get(dest, 3));
    TEST_ASSERT_EQUAL_UINT8(103, byte_array_get(dest, 4));
    TEST_ASSERT_EQUAL_UINT8(104, byte_array_get(dest, 5));
    TEST_ASSERT_EQUAL_UINT8(105, byte_array_get(dest, 6));
    TEST_ASSERT_EQUAL_UINT8(106, byte_array_get(dest, 7));
    TEST_ASSERT_EQUAL_UINT8(0, byte_array_get(dest, 8));
    
    RELEASE((CljObject*)src);
    RELEASE((CljObject*)dest);
}

void test_byte_array_slice(void) {
    // Test slicing
    CljValue arr = make_byte_array(10);
    for (int i = 0; i < 10; i++) {
        byte_array_set(arr, i, (uint8_t)(i * 5));
    }
    
    CljValue slice = byte_array_slice(arr, 3, 4);
    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_EQUAL_INT(4, byte_array_length(slice));
    
    TEST_ASSERT_EQUAL_UINT8(15, byte_array_get(slice, 0));
    TEST_ASSERT_EQUAL_UINT8(20, byte_array_get(slice, 1));
    TEST_ASSERT_EQUAL_UINT8(25, byte_array_get(slice, 2));
    TEST_ASSERT_EQUAL_UINT8(30, byte_array_get(slice, 3));
    
    RELEASE((CljObject*)arr);
    RELEASE((CljObject*)slice);
}

void test_byte_array_id_operations(void) {
    // Test reading and writing 32-bit/64-bit IDs (raw pointer values)
    // Note: ID is a void* pointer, so size depends on platform (4 bytes on 32-bit, 8 bytes on 64-bit)
    CljValue arr = make_byte_array(32); // Ensure enough space for 64-bit pointers
    
    // Test with simple immediate values
    ID id1 = fixnum(42);
    ID id2 = fixnum(999);
    
    // Write IDs at different positions (use sizeof(ID) for offset)
    byte_array_set_id(arr, 0, id1);
    byte_array_set_id(arr, sizeof(ID), id2);
    
    // Read them back
    ID read1 = byte_array_get_id(arr, 0);
    ID read2 = byte_array_get_id(arr, sizeof(ID));
    
    // Test raw pointer equality
    TEST_ASSERT_EQUAL_PTR(id1, read1);
    TEST_ASSERT_EQUAL_PTR(id2, read2);
    
    // Verify the values decode correctly
    TEST_ASSERT_TRUE(IS_FIXNUM(read1));
    TEST_ASSERT_EQUAL_INT(42, AS_FIXNUM(read1));
    TEST_ASSERT_TRUE(IS_FIXNUM(read2));
    TEST_ASSERT_EQUAL_INT(999, AS_FIXNUM(read2));
    
    RELEASE((CljObject*)arr);
}

void test_byte_array_memory_management(void) {
    // Test reference counting
    CljValue arr = make_byte_array(10);
    TEST_ASSERT_NOT_NULL(arr);
    
    CljObject *obj = (CljObject*)arr;
    TEST_ASSERT_EQUAL_INT(1, get_retain_count(obj));
    
    RETAIN(obj);
    TEST_ASSERT_EQUAL_INT(2, get_retain_count(obj));
    
    RELEASE(obj);
    TEST_ASSERT_EQUAL_INT(1, get_retain_count(obj));
    
    RELEASE(obj); // Final release
}

void test_byte_array_empty(void) {
    // Test edge case: zero-length array
    CljValue arr = make_byte_array(0);
    TEST_ASSERT_NOT_NULL(arr);
    
    CljByteArray *ba = as_byte_array(arr);
    TEST_ASSERT_EQUAL_INT(0, ba->length);
    TEST_ASSERT_NULL(ba->data);
    
    RELEASE((CljObject*)arr);
}

// ============================================================================
// TEST RUNNER
// ============================================================================

void run_byte_array_tests(void) {
    RUN_TEST(test_byte_array_creation);
    RUN_TEST(test_byte_array_from_bytes);
    RUN_TEST(test_byte_array_get_set);
    RUN_TEST(test_byte_array_length);
    RUN_TEST(test_byte_array_clone);
    RUN_TEST(test_byte_array_copy_from);
    RUN_TEST(test_byte_array_copy_to);
    RUN_TEST(test_byte_array_copy_between);
    RUN_TEST(test_byte_array_slice);
    RUN_TEST(test_byte_array_id_operations);
    RUN_TEST(test_byte_array_memory_management);
    RUN_TEST(test_byte_array_empty);
}

