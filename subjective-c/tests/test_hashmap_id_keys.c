#include "test_common.h"
#include "hashmap.h"
#include "strings.h"
#include "value.h"
#include <subjective-c/callbacks.h>  // For clj_hash, clj_equal_default

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

// Tests verwenden die neue API mit ID-Keys

TEST(test_hashmap_string_key) {
    // Test: CljString* als Key verwenden
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *key = make_test_string("my-key");
    CljString *value = make_test_string("my-value");
    
    map = adopt_hashmap(map, hashmap_assoc(map, key, value));
    TEST_ASSERT_NOT_NULL(map);
    
    ID result = hashmap_get(map, key, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(value, result);
    
    int contains = hashmap_contains(map, key);
    TEST_ASSERT_EQUAL_INT(1, contains);
    
    RELEASE(key);
    RELEASE(value);
    RELEASE(map);
}

TEST(test_hashmap_fixnum_key) {
    // Test: Fixnum als Key verwenden
    CljHashMap *map = make_hashmap_or_fail(8);
    ID key = fixnum(42);
    CljString *value = make_test_string("value-for-42");
    
    map = adopt_hashmap(map, hashmap_assoc(map, key, value));
    TEST_ASSERT_NOT_NULL(map);
    
    ID result = hashmap_get(map, key, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(value, result);
    
    RELEASE(value);
    RELEASE(map);
}

TEST(test_hashmap_nil_key) {
    // Test: nil als Key verwenden
    CljHashMap *map = make_hashmap_or_fail(8);
    CljString *value = make_test_string("value-for-nil");
    
    map = adopt_hashmap(map, hashmap_assoc(map, NULL, value));
    TEST_ASSERT_NOT_NULL(map);
    
    ID result = hashmap_get(map, NULL, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(value, result);
    
    RELEASE(value);
    RELEASE(map);
}

TEST(test_hashmap_mixed_keys) {
    // Test: Verschiedene Key-Typen gemischt
    CljHashMap *map = make_hashmap_or_fail(16);
    
    // String key
    CljString *str_key = make_test_string("string-key");
    CljString *str_val = make_test_string("string-value");
    
    // Fixnum key
    ID int_key = fixnum(100);
    CljString *int_val = make_test_string("int-value");
    
    // nil key
    CljString *nil_val = make_test_string("nil-value");
    
    map = adopt_hashmap(map, hashmap_assoc(map, str_key, str_val));
    map = adopt_hashmap(map, hashmap_assoc(map, int_key, int_val));
    map = adopt_hashmap(map, hashmap_assoc(map, NULL, nil_val));
    
    TEST_ASSERT_EQUAL_UINT(3, hashmap_count(map));
    
    // Prüfe alle Keys
    ID result1 = hashmap_get(map, str_key, NULL);
    TEST_ASSERT_EQUAL_PTR(str_val, result1);
    
    ID result2 = hashmap_get(map, int_key, NULL);
    TEST_ASSERT_EQUAL_PTR(int_val, result2);
    
    ID result3 = hashmap_get(map, NULL, NULL);
    TEST_ASSERT_EQUAL_PTR(nil_val, result3);
    
    RELEASE(str_key);
    RELEASE(str_val);
    RELEASE(int_val);
    RELEASE(nil_val);
    RELEASE(map);
}

