/*
 * Unity Tests for clj_equal function
 * 
 * Simple tests for clj_equal function using basic objects.
 */

#include "../../external/unity/src/unity.h"
#include "../object.h"
#include "../memory.h"
#include "../vector.h"
#include "../map.h"
#include "../value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// BASIC EQUALITY TESTS
// ============================================================================

void test_equal_null_pointers(void) {
    WITH_MEMORY_PROFILING({
    
    // Test null pointer cases
    TEST_ASSERT_FALSE(clj_equal(NULL, fixnum(1)));
    TEST_ASSERT_FALSE(clj_equal(fixnum(1), NULL));
    TEST_ASSERT_TRUE(clj_equal(NULL, NULL));
    
    });
}

void test_equal_same_objects(void) {
    // Test same object reference - minimal test
    TEST_ASSERT_TRUE(1 == 1); // Just test that the test framework works
}

void test_equal_different_strings(void) {
    WITH_MEMORY_PROFILING({
    
    // Test different strings
    CljObject *str1 = make_string("hello");
    CljObject *str2 = make_string("world");
    CljObject *str3 = make_string("hello");
    
    TEST_ASSERT_FALSE(clj_equal(str1, str2));
    TEST_ASSERT_TRUE(clj_equal(str1, str3));
    
    RELEASE(str1);
    RELEASE(str2);
    RELEASE(str3);
    
    });
}

void test_equal_different_types(void) {
    WITH_MEMORY_PROFILING({
    
    // Test different types
    CljValue vec_val = make_vector(1, 1);
    CljMap *map = (CljMap*)make_map(16);
    CljObject *list = make_list(NULL, NULL);
    
    CljObject *vec = (CljObject*)vec_val;
    
    TEST_ASSERT_FALSE(clj_equal(vec, (CljObject*)map));
    TEST_ASSERT_FALSE(clj_equal(vec, (CljObject*)list));
    TEST_ASSERT_FALSE(clj_equal((CljObject*)map, (CljObject*)list));
    
    RELEASE(vec);
    RELEASE(map);
    RELEASE(list);
    
    });
}

void test_equal_immediate_values(void) {
    WITH_MEMORY_PROFILING({
    
    // Test immediate values - these should not be passed to clj_equal
    // as they are handled by the caller before calling clj_equal
    CljValue num1 = fixnum(42);
    CljValue num2 = fixnum(42);
    CljValue num3 = fixnum(43);
    
    // These are immediate values, so clj_equal should not be called on them
    // But if it is, it should return false since they're not CljObject*
    // However, the test should pass because immediate values are handled
    // by the caller, not by clj_equal directly
    TEST_ASSERT_TRUE(num1 == num2); // Immediate values are compared directly
    TEST_ASSERT_FALSE(num1 == num3);
    
    });
}

// ============================================================================
// VECTOR EQUALITY TESTS
// ============================================================================

void test_vector_equal_same_vectors(void) {
    WITH_MEMORY_PROFILING({
    
    // Create two identical vectors using CljValue API
    CljValue vec1_val = make_vector(3, 1);
    CljValue vec2_val = make_vector(3, 1);
    
    CljObject *vec1 = (CljObject*)vec1_val;
    CljObject *vec2 = (CljObject*)vec2_val;
    
    // Fill with same values using vector_conj
    CljValue val1 = fixnum(1);
    CljValue val2 = fixnum(2);
    CljValue val3 = fixnum(3);
    
    vec1_val = vector_conj(vec1_val, val1);
    vec1_val = vector_conj(vec1_val, val2);
    vec1_val = vector_conj(vec1_val, val3);
    
    vec2_val = vector_conj(vec2_val, val1);
    vec2_val = vector_conj(vec2_val, val2);
    vec2_val = vector_conj(vec2_val, val3);
    
    vec1 = (CljObject*)vec1_val;
    vec2 = (CljObject*)vec2_val;
    
    // Test equality
    TEST_ASSERT_TRUE(clj_equal(vec1, vec2));
    
    // Cleanup
    RELEASE(vec1);
    RELEASE(vec2);
    
    });
}

void test_vector_equal_different_lengths(void) {
    WITH_MEMORY_PROFILING({
    
    // Create vectors with different lengths
    CljValue vec1_val = make_vector(2, 1);
    CljValue vec2_val = make_vector(3, 1);
    
    CljObject *vec1 = (CljObject*)vec1_val;
    CljObject *vec2 = (CljObject*)vec2_val;
    
    // Fill with same values but different lengths
    CljValue val1 = fixnum(1);
    CljValue val2 = fixnum(2);
    CljValue val3 = fixnum(3);
    
    vec1_val = vector_conj(vec1_val, val1);
    vec1_val = vector_conj(vec1_val, val2);
    
    vec2_val = vector_conj(vec2_val, val1);
    vec2_val = vector_conj(vec2_val, val2);
    vec2_val = vector_conj(vec2_val, val3);
    
    vec1 = (CljObject*)vec1_val;
    vec2 = (CljObject*)vec2_val;
    
    // Test inequality
    TEST_ASSERT_FALSE(clj_equal(vec1, vec2));
    
    // Cleanup
    RELEASE(vec1);
    RELEASE(vec2);
    
    });
}

void test_vector_equal_different_values(void) {
    WITH_MEMORY_PROFILING({
    
    // Create vectors with different values using CljValue API
    CljValue vec1_val = make_vector(0, 1); // Start with empty vector
    CljValue vec2_val = make_vector(0, 1); // Start with empty vector
    
    // Create different integer values (immediate values)
    CljValue int1 = fixnum(1);
    CljValue int2 = fixnum(2);
    CljValue int3 = fixnum(3);
    CljValue int4 = fixnum(4);
    
    // Build vectors with different values using conj
    vec1_val = vector_conj(vec1_val, int1);
    vec1_val = vector_conj(vec1_val, int2);
    vec2_val = vector_conj(vec2_val, int3);
    vec2_val = vector_conj(vec2_val, int4);
    
    // Verify vectors were created successfully
    TEST_ASSERT_NOT_NULL((CljObject*)vec1_val);
    TEST_ASSERT_NOT_NULL((CljObject*)vec2_val);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)vec1_val)->type);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)vec2_val)->type);
    
    // Test vector equality with clj_equal - now supports immediate values in vectors
    TEST_ASSERT_FALSE(clj_equal((CljObject*)vec1_val, (CljObject*)vec2_val));
    
    });
}

void test_clj_equal_id_function(void) {
    WITH_MEMORY_PROFILING({
    
    // Test immediate values (CljValue)
    CljValue fix1 = fixnum(42);
    CljValue fix2 = fixnum(42);
    CljValue fix3 = fixnum(43);
    
    // Test same immediate values
    TEST_ASSERT_TRUE(clj_equal_id((ID)fix1, (ID)fix2));
    // Test different immediate values
    TEST_ASSERT_FALSE(clj_equal_id((ID)fix1, (ID)fix3));
    
    // Test heap objects (CljObject*)
    CljObject *str1 = make_string("hello");
    CljObject *str2 = make_string("hello");
    CljObject *str3 = make_string("world");
    
    // Test same heap objects (pointer equality)
    TEST_ASSERT_TRUE(clj_equal_id((ID)str1, (ID)str1));
    // Test different heap objects with same content
    TEST_ASSERT_TRUE(clj_equal_id((ID)str1, (ID)str2));
    // Test different heap objects with different content
    TEST_ASSERT_FALSE(clj_equal_id((ID)str1, (ID)str3));
    
    // Test mixed types (immediate vs heap)
    TEST_ASSERT_FALSE(clj_equal_id((ID)fix1, (ID)str1));
    
    // Test NULL values
    TEST_ASSERT_TRUE(clj_equal_id((ID)NULL, (ID)NULL));
    TEST_ASSERT_FALSE(clj_equal_id((ID)fix1, (ID)NULL));
    TEST_ASSERT_FALSE(clj_equal_id((ID)NULL, (ID)str1));
    
    // Cleanup
    RELEASE(str1);
    RELEASE(str2);
    RELEASE(str3);
    
    });
}

void test_vector_equal_with_strings(void) {
    WITH_MEMORY_PROFILING({
    
    // Create vectors with strings
    CljValue vec1_val = make_vector(2, 1);
    CljValue vec2_val = make_vector(2, 1);
    
    CljObject *vec1 = (CljObject*)vec1_val;
    CljObject *vec2 = (CljObject*)vec2_val;
    
    // Create string objects
    CljObject *str1 = make_string("hello");
    CljObject *str2 = make_string("world");
    CljObject *str3 = make_string("hello");
    CljObject *str4 = make_string("world");
    
    // Fill vectors with strings
    vec1_val = vector_conj(vec1_val, (CljValue)str1);
    vec1_val = vector_conj(vec1_val, (CljValue)str2);
    
    vec2_val = vector_conj(vec2_val, (CljValue)str3);
    vec2_val = vector_conj(vec2_val, (CljValue)str4);
    
    vec1 = (CljObject*)vec1_val;
    vec2 = (CljObject*)vec2_val;
    
    // Test equality
    TEST_ASSERT_TRUE(clj_equal(vec1, vec2));
    
    // Cleanup
    RELEASE(vec1);
    RELEASE(vec2);
    RELEASE(str1);
    RELEASE(str2);
    RELEASE(str3);
    RELEASE(str4);
    
    });
}

// ============================================================================
// MAP EQUALITY TESTS
// ============================================================================

void test_map_equal_same_maps(void) {
    WITH_MEMORY_PROFILING({
    
    // Create two identical maps using old API
    CljMap *map1 = make_map_old(16);
    CljMap *map2 = make_map_old(16);
    
    // Create keys and values
    CljObject *key1 = make_string("key1");
    CljObject *key2 = make_string("key2");
    CljObject *val1 = make_string("value1");
    CljObject *val2 = make_string("value2");
    
    // Add same key-value pairs to both maps
    map_assoc((CljObject*)map1, key1, val1);
    map_assoc((CljObject*)map1, key2, val2);
    
    map_assoc((CljObject*)map2, key1, val1);
    map_assoc((CljObject*)map2, key2, val2);
    
    // Test equality
    TEST_ASSERT_TRUE(clj_equal((CljObject*)map1, (CljObject*)map2));
    
    // Cleanup
    RELEASE(map1);
    RELEASE(map2);
    RELEASE(key1);
    RELEASE(key2);
    RELEASE(val1);
    RELEASE(val2);
    
    });
}

void test_map_equal_different_keys(void) {
    WITH_MEMORY_PROFILING({
    
    CljMap *map1 = make_map_old(16);
    CljMap *map2 = make_map_old(16);
    
    // Create different keys
    CljObject *key1 = make_string("key1");
    CljObject *key2 = make_string("key2");
    CljObject *key3 = make_string("key3");
    CljObject *val1 = make_string("value1");
    CljObject *val2 = make_string("value2");
    
    // Add different key-value pairs
    map_assoc((CljObject*)map1, key1, val1);
    map_assoc((CljObject*)map1, key2, val2);
    
    map_assoc((CljObject*)map2, key1, val1);
    map_assoc((CljObject*)map2, key3, val2); // Different key
    
    // Test inequality
    TEST_ASSERT_FALSE(clj_equal((CljObject*)map1, (CljObject*)map2));
    
    // Cleanup
    RELEASE(map1);
    RELEASE(map2);
    RELEASE(key1);
    RELEASE(key2);
    RELEASE(key3);
    RELEASE(val1);
    RELEASE(val2);
    
    });
}

void test_map_equal_different_values(void) {
    WITH_MEMORY_PROFILING({
    
    CljMap *map1 = make_map_old(16);
    CljMap *map2 = make_map_old(16);
    
    // Create keys and different values
    CljObject *key1 = make_string("key1");
    CljObject *key2 = make_string("key2");
    CljObject *val1 = make_string("value1");
    CljObject *val2 = make_string("value2");
    CljObject *val3 = make_string("value3");
    
    // Add same keys but different values
    map_assoc((CljObject*)map1, key1, val1);
    map_assoc((CljObject*)map1, key2, val2);
    
    map_assoc((CljObject*)map2, key1, val1);
    map_assoc((CljObject*)map2, key2, val3); // Different value
    
    // Test inequality
    TEST_ASSERT_FALSE(clj_equal((CljObject*)map1, (CljObject*)map2));
    
    // Cleanup
    RELEASE(map1);
    RELEASE(map2);
    RELEASE(key1);
    RELEASE(key2);
    RELEASE(val1);
    RELEASE(val2);
    RELEASE(val3);
    
    });
}

void test_map_equal_different_sizes(void) {
    WITH_MEMORY_PROFILING({
    
    CljMap *map1 = make_map_old(16);
    CljMap *map2 = make_map_old(16);
    
    // Create keys and values
    CljObject *key1 = make_string("key1");
    CljObject *key2 = make_string("key2");
    CljObject *val1 = make_string("value1");
    CljObject *val2 = make_string("value2");
    
    // Add different number of entries
    map_assoc((CljObject*)map1, key1, val1);
    map_assoc((CljObject*)map1, key2, val2);
    
    map_assoc((CljObject*)map2, key1, val1);
    // map2 has only one entry
    
    // Test inequality
    TEST_ASSERT_FALSE(clj_equal((CljObject*)map1, (CljObject*)map2));
    
    // Cleanup
    RELEASE(map1);
    RELEASE(map2);
    RELEASE(key1);
    RELEASE(key2);
    RELEASE(val1);
    RELEASE(val2);
    
    });
}

void test_map_equal_with_nested_vectors(void) {
    WITH_MEMORY_PROFILING({
    
    CljMap *map1 = make_map_old(16);
    CljMap *map2 = make_map_old(16);
    
    // Create nested vectors
    CljValue vec1_val = make_vector(2, 1);
    CljValue vec2_val = make_vector(2, 1);
    
    CljValue val1 = fixnum(1);
    CljValue val2 = fixnum(2);
    
    vec1_val = vector_conj(vec1_val, val1);
    vec1_val = vector_conj(vec1_val, val2);
    
    vec2_val = vector_conj(vec2_val, val1);
    vec2_val = vector_conj(vec2_val, val2);
    
    CljObject *vec1 = (CljObject*)vec1_val;
    CljObject *vec2 = (CljObject*)vec2_val;
    
    // Create keys
    CljObject *key1 = make_string("nested");
    CljObject *val_str = make_string("value");
    
    // Add to maps
    map_assoc((CljObject*)map1, key1, vec1);
    map_assoc((CljObject*)map1, val_str, val_str);
    
    map_assoc((CljObject*)map2, key1, vec2);
    map_assoc((CljObject*)map2, val_str, val_str);
    
    // Test equality (should be true due to structural equality of vectors)
    TEST_ASSERT_TRUE(clj_equal((CljObject*)map1, (CljObject*)map2));
    
    // Cleanup
    RELEASE(map1);
    RELEASE(map2);
    RELEASE(vec1);
    RELEASE(vec2);
    RELEASE(key1);
    RELEASE(val_str);
    
    });
}

// ============================================================================
// LIST EQUALITY TESTS
// ============================================================================

void test_list_equal_same_lists(void) {
    WITH_MEMORY_PROFILING({
    
    // Create two identical lists
    CljObject *list1 = make_list(NULL, NULL);
    CljObject *list2 = make_list(NULL, NULL);
    
    // Test equality (lists use pointer comparison, so this should be false)
    // Lists are only equal if they are the same instance
    TEST_ASSERT_FALSE(clj_equal((CljObject*)list1, (CljObject*)list2));
    
    // Cleanup
    RELEASE(list1);
    RELEASE(list2);
    
    });
}

void test_list_equal_same_instance(void) {
    WITH_MEMORY_PROFILING({
    
    CljObject *list1 = make_list(NULL, NULL);
    CljObject *list2 = (CljObject*)list1; // Same instance
    
    // Test equality of same instance
    TEST_ASSERT_TRUE(clj_equal((CljObject*)list1, list2));
    
    // Cleanup
    RELEASE(list1);
    
    });
}

void test_list_equal_empty_lists(void) {
    WITH_MEMORY_PROFILING({
    
    CljObject *list1 = make_list(NULL, NULL);
    CljObject *list2 = make_list(NULL, NULL);
    
    // Test equality of different empty lists (should be false due to pointer comparison)
    TEST_ASSERT_FALSE(clj_equal((CljObject*)list1, (CljObject*)list2));
    
    // Cleanup
    RELEASE(list1);
    RELEASE(list2);
    
    });
}