#include "test_common.h"
#include <subjective-c/callbacks.h>
#include "strings.h"
#include "value.h"

// Helper: Create CljString from C string
static CljString* make_test_string(const char *str) {
    CljString *s = make_clj_string(str);
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

TEST(test_clj_hash_nil) {
    uint32_t hash1 = clj_hash(NULL);
    uint32_t hash2 = clj_hash(NULL);
    TEST_ASSERT_EQUAL_UINT(0, hash1);
    TEST_ASSERT_EQUAL_UINT(0, hash2);
    TEST_ASSERT_EQUAL_UINT(hash1, hash2);  // Konsistent
}

TEST(test_clj_hash_fixnum) {
    ID val1 = fixnum(42);
    ID val2 = fixnum(42);
    ID val3 = fixnum(100);
    
    uint32_t hash1 = clj_hash(val1);
    uint32_t hash2 = clj_hash(val2);
    uint32_t hash3 = clj_hash(val3);
    
    TEST_ASSERT_EQUAL_UINT(hash1, hash2);  // Gleiche Werte = gleicher Hash
    TEST_ASSERT_NOT_EQUAL(hash1, hash3);   // Verschiedene Werte = verschiedene Hashes
    TEST_ASSERT_EQUAL_UINT(42, hash1);      // Fixnum Hash = Wert
}

TEST(test_clj_hash_string) {
    CljString *str1 = make_test_string("foo");
    CljString *str2 = make_test_string("foo");
    CljString *str3 = make_test_string("bar");
    
    uint32_t hash1 = clj_hash(str1);
    uint32_t hash2 = clj_hash(str2);
    uint32_t hash3 = clj_hash(str3);
    
    TEST_ASSERT_EQUAL_UINT(hash1, hash2);  // Gleiche Strings = gleicher Hash
    TEST_ASSERT_NOT_EQUAL(hash1, hash3);   // Verschiedene Strings = verschiedene Hashes
    
    RELEASE((CljObject*)str1);
    RELEASE((CljObject*)str2);
    RELEASE((CljObject*)str3);
}

TEST(test_clj_hash_string_consistency) {
    // Test dass FNV-1a konsistent ist
    CljString *str = make_test_string("test");
    uint32_t hash1 = clj_hash(str);
    uint32_t hash2 = clj_hash(str);
    uint32_t hash3 = clj_hash(str);
    
    TEST_ASSERT_EQUAL_UINT(hash1, hash2);
    TEST_ASSERT_EQUAL_UINT(hash2, hash3);
    
    RELEASE((CljObject*)str);
}

TEST(test_clj_hash_equal_values) {
    // Test Hash-Konsistenz: gleiche Werte sollten gleichen Hash haben
    CljString *str1 = make_test_string("hello");
    CljString *str2 = make_test_string("hello");
    
    uint32_t hash1 = clj_hash(str1);
    uint32_t hash2 = clj_hash(str2);
    
    // Gleiche String-Inhalte sollten gleichen Hash haben
    TEST_ASSERT_EQUAL_UINT(hash1, hash2);
    
    RELEASE((CljObject*)str1);
    RELEASE((CljObject*)str2);
}

