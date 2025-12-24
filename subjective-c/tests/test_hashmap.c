#include "test_common.h"
#include "hashmap.h"
#include "strings.h"
#include "value.h"

// Helper: Adopt updated map (handles RELEASE of old map)
static CljHashMap* adopt_hashmap(CljHashMap *current, CljHashMap *updated) {
    if (!updated) {
        return current;
    }
    if (current && current != updated) {
        RELEASE(current);
    }
    return updated;
}

// Helper: Create hashmap or fail test
static CljHashMap* make_hashmap_or_fail(unsigned int capacity) {
    CljHashMap *map = make_hashmap(capacity);
    TEST_ASSERT_NOT_NULL(map);
    return map;
}

// Helper: Create CljString from C string
static CljString* make_test_string(const char *str) {
    CljString *s = make_clj_string(str);
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

TEST(test_hashmap_create) {
    CljHashMap *map = make_hashmap(0);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_UINT(0, hashmap_count(map));
    RELEASE(map);
    
    map = make_hashmap(8);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_UINT(0, hashmap_count(map));
    RELEASE(map);
    
    map = make_hashmap(100);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_UINT(0, hashmap_count(map));
    RELEASE(map);
}

TEST(test_hashmap_put_get_single) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *key = make_test_string("key1");
    CljString *value = make_test_string("test-value");
    
    map = adopt_hashmap(map, hashmap_assoc(map, key, value));
    TEST_ASSERT_EQUAL_UINT(1, hashmap_count(map));
    
    ID result = hashmap_get(map, key, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(value, result);
    
    RELEASE(map);
    RELEASE(key);
    RELEASE(value);
}

TEST(test_hashmap_put_get_multiple) {
    // High-level test: multiple operations combined
    CljHashMap *map = make_hashmap_or_fail(16);
    
    // Insert 10 different keys, test get, contains, and count
    CljString *keys[10];
    CljString *values[10];
    for (int i = 0; i < 10; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "key%d", i);
        keys[i] = make_test_string(key_buf);
        values[i] = make_test_string(key_buf);
        map = adopt_hashmap(map, hashmap_assoc(map, keys[i], values[i]));
        TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, keys[i]));  // Test contains after each insert
    }
    
    TEST_ASSERT_EQUAL_UINT(10, hashmap_count(map));
    
    // Retrieve all keys
    for (int i = 0; i < 10; i++) {
        ID result = hashmap_get(map, keys[i], NULL);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(values[i], result);
    }
    
    RELEASE(map);
    for (int i = 0; i < 10; i++) {
        RELEASE(keys[i]);
        RELEASE(values[i]);
    }
}

TEST(test_hashmap_linear_probing_collision) {
    // High-level test: collision handling + contains + get
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *k2 = make_test_string("key2");
    CljString *v1 = make_test_string("value1");
    CljString *v2 = make_test_string("value2");
    
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    map = adopt_hashmap(map, hashmap_assoc(map, k2, v2));
    
    TEST_ASSERT_EQUAL_UINT(2, hashmap_count(map));
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, k1));
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, k2));
    
    // Both should be retrievable (Linear Probing handles collision)
    TEST_ASSERT_EQUAL_PTR(v1, hashmap_get(map, k1, NULL));
    TEST_ASSERT_EQUAL_PTR(v2, hashmap_get(map, k2, NULL));
    
    RELEASE(map);
    RELEASE(k1);
    RELEASE(k2);
    RELEASE(v1);
    RELEASE(v2);
}

TEST(test_hashmap_overwrite_same_map) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *v1 = make_test_string("value1");
    CljString *v2 = make_test_string("value2");
    
    // RC=1: should return same map
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    CljHashMap *map_before = map;
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v2));
    
    // Should be same pointer (RC=1, in-place mutation)
    TEST_ASSERT_EQUAL_PTR(map_before, map);
    TEST_ASSERT_EQUAL_UINT(1, hashmap_count(map));
    
    ID result = hashmap_get(map, k1, NULL);
    TEST_ASSERT_EQUAL_PTR(v2, result);
    
    RELEASE(map);
    RELEASE(k1);
    RELEASE(v1);
    RELEASE(v2);
}

TEST(test_hashmap_overwrite_cow) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *v1 = make_test_string("value1");
    CljString *v2 = make_test_string("value2");
    
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    
    // RC>1: should return new map (COW)
    RETAIN(map);
    CljHashMap *map_before = map;
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v2));
    
    // Should be different pointer (COW)
    TEST_ASSERT_NOT_EQUAL(map_before, map);
    TEST_ASSERT_EQUAL_UINT(1, hashmap_count(map));
    
    ID result = hashmap_get(map, k1, NULL);
    TEST_ASSERT_EQUAL_PTR(v2, result);
    
    // Original should still have old value
    ID orig_result = hashmap_get(map_before, k1, NULL);
    TEST_ASSERT_EQUAL_PTR(v1, orig_result);
    
    RELEASE(map);
    RELEASE(k1);
    RELEASE(map_before);
    RELEASE(v1);
    RELEASE(v2);
}

TEST(test_hashmap_not_found) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *nonexistent = make_test_string("nonexistent");
    CljString *not_found_sentinel = make_test_string("NOT_FOUND");
    
    ID result = hashmap_get(map, nonexistent, not_found_sentinel);
    TEST_ASSERT_EQUAL_PTR(not_found_sentinel, result);
    
    RELEASE(map);
    RELEASE(nonexistent);
    RELEASE(not_found_sentinel);
}

TEST(test_hashmap_remove_rc1) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *v1 = make_test_string("value1");
    
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    TEST_ASSERT_EQUAL_UINT(1, hashmap_count(map));
    
    // RC=1: should return same map with tombstone
    CljHashMap *map_before = map;
    map = adopt_hashmap(map, hashmap_remove(map, k1));
    
    // Should be same pointer (RC=1, in-place tombstone)
    TEST_ASSERT_EQUAL_PTR(map_before, map);
    TEST_ASSERT_EQUAL_UINT(0, hashmap_count(map));
    
    // Key should not be found
    ID result = hashmap_get(map, k1, NULL);
    TEST_ASSERT_NULL(result);
    
    RELEASE(map);
    RELEASE(k1);
    RELEASE(v1);
}

TEST(test_hashmap_remove_cow) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *v1 = make_test_string("value1");
    
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    
    // RC>1: should return new map without the key
    RETAIN(map);
    CljHashMap *map_before = map;
    map = adopt_hashmap(map, hashmap_remove(map, k1));
    
    // Should be different pointer (COW)
    TEST_ASSERT_NOT_EQUAL(map_before, map);
    TEST_ASSERT_EQUAL_UINT(0, hashmap_count(map));
    
    // Key should not be found in new map
    ID result = hashmap_get(map, k1, NULL);
    TEST_ASSERT_NULL(result);
    
    // Original should still have the key
    ID orig_result = hashmap_get(map_before, k1, NULL);
    TEST_ASSERT_EQUAL_PTR(v1, orig_result);
    
    RELEASE(map);
    RELEASE(map_before);
    RELEASE(k1);
    RELEASE(v1);
}

TEST(test_hashmap_probe_over_tombstone) {
    // High-level test: tombstone handling + Linear Probing + remove
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *k2 = make_test_string("key2");
    CljString *v1 = make_test_string("value1");
    CljString *v2 = make_test_string("value2");
    
    // Insert two keys that might collide
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    map = adopt_hashmap(map, hashmap_assoc(map, k2, v2));
    TEST_ASSERT_EQUAL_UINT(2, hashmap_count(map));
    
    // Remove first key (creates tombstone)
    map = adopt_hashmap(map, hashmap_remove(map, k1));
    TEST_ASSERT_EQUAL_UINT(1, hashmap_count(map));
    TEST_ASSERT_EQUAL_INT(0, hashmap_contains(map, k1));
    
    // Second key should still be retrievable (Linear Probing over tombstone)
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, k2));
    TEST_ASSERT_EQUAL_PTR(v2, hashmap_get(map, k2, NULL));
    
    RELEASE(map);
    RELEASE(k1);
    RELEASE(k2);
    RELEASE(v1);
    RELEASE(v2);
}

TEST(test_hashmap_rehash_on_load) {
    // High-level test: rehashing + many entries + all operations
    CljHashMap *map = make_hashmap_or_fail(8);
    
    // Fill map to trigger rehash (load > 0.75)
    // 8 * 0.75 = 6, so 7 entries should trigger rehash
    CljString *keys[10];
    CljString *values[10];
    for (int i = 0; i < 10; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "key%d", i);
        keys[i] = make_test_string(key_buf);
        values[i] = make_test_string(key_buf);
        map = adopt_hashmap(map, hashmap_assoc(map, keys[i], values[i]));
        TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, keys[i]));  // Test contains
    }
    
    TEST_ASSERT_EQUAL_UINT(10, hashmap_count(map));
    
    // All keys should still be retrievable after rehash
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_PTR(values[i], hashmap_get(map, keys[i], NULL));
    }
    
    RELEASE(map);
    for (int i = 0; i < 10; i++) {
        RELEASE(keys[i]);
        RELEASE(values[i]);
    }
}

TEST(test_hashmap_contains) {
    // High-level test: contains + basic operations
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *k2 = make_test_string("key2");
    CljString *v1 = make_test_string("value1");
    
    TEST_ASSERT_EQUAL_INT(0, hashmap_contains(map, k1));
    
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, k1));
    TEST_ASSERT_EQUAL_INT(0, hashmap_contains(map, k2));
    
    RELEASE(map);
    RELEASE(k1);
    RELEASE(k2);
    RELEASE(v1);
}

// ============================================================================
// HIGH-LEVEL TESTS (Clojure Source Code)
// ============================================================================

// Note: These tests require eval_string from src/tests, but subjective-c tests
// don't have access to g_test_eval_state. These would be better placed in
// src/tests/test_hashmap_highlevel.c once HashMap is integrated into symbol table.
//
// Example high-level tests (for future integration):
//
// TEST(test_hashmap_symbol_table_via_clojure) {
//     // Test HashMap indirectly via symbol table operations
//     // Create many symbols to stress HashMap
//     CljObject *result = eval_string("(do (def a 1) (def b 2) (def c 3) (+ a b c))", g_test_eval_state);
//     assert_fixnum(result, 6);
// }
//
// TEST(test_hashmap_collision_via_clojure) {
//     // Test HashMap collision handling via symbols that hash to same index
//     // This would require knowledge of hash function to create collisions
//     CljObject *result = eval_string("(do (def x1 1) (def x2 2) (def x3 3) (+ x1 x2 x3))", g_test_eval_state);
//     assert_fixnum(result, 6);
// }

TEST(test_hashmap_cow_independence) {
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *k1 = make_test_string("key1");
    CljString *k2 = make_test_string("key2");
    CljString *v1 = make_test_string("value1");
    CljString *v2 = make_test_string("value2");
    
    map = adopt_hashmap(map, hashmap_assoc(map, k1, v1));
    
    // Create copy via retain
    RETAIN(map);
    CljHashMap *copy = map;
    
    // Modify original
    map = adopt_hashmap(map, hashmap_assoc(map, k2, v2));
    
    // Copy should not have key2
    TEST_ASSERT_EQUAL_INT(0, hashmap_contains(copy, k2));
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(copy, k1));
    
    // Original should have both
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, k1));
    TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, k2));
    
    RELEASE(map);
    RELEASE(copy);
    RELEASE(k1);
    RELEASE(k2);
    RELEASE(v1);
    RELEASE(v2);
}

TEST(test_hashmap_many_entries) {
    // High-level test: performance + rehashing + all operations
    CljHashMap *map = make_hashmap_or_fail(512);
    
    // Insert 1000 entries (tests rehashing, Linear Probing performance)
    CljString *keys[1000];
    CljString *values[1000];
    for (int i = 0; i < 1000; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "key%d", i);
        keys[i] = make_test_string(key_buf);
        values[i] = make_test_string(key_buf);
        map = adopt_hashmap(map, hashmap_assoc(map, keys[i], values[i]));
    }
    
    TEST_ASSERT_EQUAL_UINT(1000, hashmap_count(map));
    
    // Verify all entries are retrievable (tests Linear Probing with many entries)
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT_EQUAL_INT(1, hashmap_contains(map, keys[i]));
        TEST_ASSERT_EQUAL_PTR(values[i], hashmap_get(map, keys[i], NULL));
    }
    
    RELEASE(map);
    for (int i = 0; i < 1000; i++) {
        RELEASE(keys[i]);
        RELEASE(values[i]);
    }
}

