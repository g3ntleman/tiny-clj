/*
 * Unity Tests for Map Parent Chaining functionality
 * 
 * Tests for Environment-Chaining implementation where parent is stored
 * as a key-value pair with magic key __parent__ in the data array.
 */

#include "tests_common.h"
#include "map.h"

TEST(test_magic_key_exists) {
    // Test that magic key __parent__ exists and is a unique symbol
    CljObject *magic_key = intern_symbol_global("__parent__");
    TEST_ASSERT_NOT_NULL(magic_key);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TYPE(magic_key));
    
    // Test that calling it multiple times returns the same pointer (interned)
    CljObject *magic_key2 = intern_symbol_global("__parent__");
    TEST_ASSERT_EQUAL_PTR(magic_key, magic_key2);
}

TEST(test_map_set_parent) {
    // Test that parent can be set and accessed via chaining
    CljMap *parent_map = make_map(2);
    TEST_ASSERT_NOT_NULL(parent_map);
    
    CljMap *child_map = make_map(4);
    TEST_ASSERT_NOT_NULL(child_map);
    
    // Add entry to parent
    CljObject *parent_key = intern_symbol_global("p");
    CljValue parent_val = make_fixnum(42);
    map_assoc((CljValue)parent_map, (CljValue)parent_key, parent_val);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)child_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Verify parent was set by accessing parent's key via chaining
    CljValue result = map_get((CljValue)child_map, (CljValue)parent_key);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
    
    RELEASE((CljObject*)child_map);
    RELEASE((CljObject*)parent_map);
}

TEST(test_map_count_without_parent) {
    // Test that normal map has correct count
    CljMap *map = make_map(4);
    TEST_ASSERT_NOT_NULL(map);
    
    // Initially empty
    TEST_ASSERT_EQUAL_INT(0, map_count((CljValue)map));
    
    // Add some entries
    CljObject *key1 = intern_symbol_global("a");
    CljObject *key2 = intern_symbol_global("b");
    CljValue val1 = make_fixnum(1);
    CljValue val2 = make_fixnum(2);
    
    map_assoc((CljValue)map, (CljValue)key1, val1);
    TEST_ASSERT_EQUAL_INT(1, map_count((CljValue)map));
    
    map_assoc((CljValue)map, (CljValue)key2, val2);
    TEST_ASSERT_EQUAL_INT(2, map_count((CljValue)map));
    
    RELEASE((CljObject*)map);
}

TEST(test_map_count_with_parent) {
    // Test that map with parent includes parent in count
    CljMap *parent_map = make_map(2);
    CljMap *child_map = make_map(4);
    
    // Add entry to child
    CljObject *key = intern_symbol_global("x");
    CljValue val = make_fixnum(10);
    map_assoc((CljValue)child_map, (CljValue)key, val);
    
    int count_before = map_count((CljValue)child_map);
    TEST_ASSERT_EQUAL_INT(1, count_before);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)child_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Count should increase by 1 (parent pair is included)
    int count_after = map_count((CljValue)child_map);
    TEST_ASSERT_EQUAL_INT(count_before + 1, count_after);
    
    RELEASE((CljObject*)child_map);
    RELEASE((CljObject*)parent_map);
}

TEST(test_map_get_without_parent) {
    // Test that normal lookups work (no regression)
    CljMap *map = make_map(4);
    CljObject *key1 = intern_symbol_global("a");
    CljObject *key2 = intern_symbol_global("b");
    CljValue val1 = make_fixnum(10);
    CljValue val2 = make_fixnum(20);
    
    map_assoc((CljValue)map, (CljValue)key1, val1);
    map_assoc((CljValue)map, (CljValue)key2, val2);
    
    // Lookup should work normally
    CljValue result1 = map_get((CljValue)map, (CljValue)key1);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result1));
    
    CljValue result2 = map_get((CljValue)map, (CljValue)key2);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(is_fixnum(result2));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result2));
    
    // Non-existent key should return NULL
    CljObject *key3 = intern_symbol_global("c");
    CljValue result3 = map_get((CljValue)map, (CljValue)key3);
    TEST_ASSERT_NULL(result3);
    
    RELEASE((CljObject*)map);
}

TEST(test_map_get_ignores_parent_pair) {
    // Test that parent pair is not found as normal key
    CljMap *parent_map = make_map(2);
    CljMap *child_map = make_map(4);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)child_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Magic key should not be found as normal key
    CljObject *magic_key = intern_symbol_global("__parent__");
    CljValue result = map_get((CljValue)child_map, (CljValue)magic_key);
    TEST_ASSERT_NULL(result);  // Should not find parent pair as normal key
    
    RELEASE((CljObject*)child_map);
    RELEASE((CljObject*)parent_map);
}

TEST(test_map_get_chains_to_parent) {
    // Test that lookup finds keys in parent
    CljMap *parent_map = make_map(4);
    CljMap *child_map = make_map(4);
    
    // Add entry to parent
    CljObject *parent_key = intern_symbol_global("x");
    CljValue parent_val = make_fixnum(100);
    map_assoc((CljValue)parent_map, (CljValue)parent_key, parent_val);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)child_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Lookup in child should find parent's key
    CljValue result = map_get((CljValue)child_map, (CljValue)parent_key);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(result));
    
    RELEASE((CljObject*)child_map);
    RELEASE((CljObject*)parent_map);
}

TEST(test_map_get_prefers_local) {
    // Test that local keys are preferred over parent keys
    CljMap *parent_map = make_map(4);
    CljMap *child_map = make_map(4);
    
    // Add same key to both parent and child with different values
    CljObject *key = intern_symbol_global("x");
    CljValue parent_val = make_fixnum(100);
    CljValue child_val = make_fixnum(200);
    
    map_assoc((CljValue)parent_map, (CljValue)key, parent_val);
    map_assoc((CljValue)child_map, (CljValue)key, child_val);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)child_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Lookup should return child's value (local preferred)
    CljValue result = map_get((CljValue)child_map, (CljValue)key);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(200, as_fixnum(result));  // Child's value, not parent's
    
    RELEASE((CljObject*)child_map);
    RELEASE((CljObject*)parent_map);
}

TEST(test_map_get_chain_multiple_levels) {
    // Test that multi-level chaining works
    CljMap *grandparent_map = make_map(4);
    CljMap *parent_map = make_map(4);
    CljMap *child_map = make_map(4);
    
    // Add entry to grandparent
    CljObject *key = intern_symbol_global("y");
    CljValue val = make_fixnum(300);
    map_assoc((CljValue)grandparent_map, (CljValue)key, val);
    
    // Chain: grandparent -> parent -> child
    map_assoc((CljValue)parent_map, g_magic_parent_key, (CljValue)grandparent_map);
    map_assoc((CljValue)child_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Lookup in child should find grandparent's key
    CljValue result = map_get((CljValue)child_map, (CljValue)key);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(300, as_fixnum(result));
    
    RELEASE((CljObject*)child_map);
    RELEASE((CljObject*)parent_map);
    RELEASE((CljObject*)grandparent_map);
}

TEST(test_map_assoc_cow_preserves_parent) {
    // Test that parent is preserved during Copy-on-Write
    CljMap *parent_map = make_map(4);
    CljMap *original_map = make_map(4);
    
    // Add entry to parent
    CljObject *parent_key = intern_symbol_global("x");
    CljValue parent_val = make_fixnum(100);
    map_assoc((CljValue)parent_map, (CljValue)parent_key, parent_val);
    
    // Add entry to original map
    CljObject *original_key = intern_symbol_global("a");
    CljValue original_val = make_fixnum(10);
    map_assoc((CljValue)original_map, (CljValue)original_key, original_val);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)original_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Create a second reference to force Copy-on-Write (RC > 1)
    RETAIN((CljObject*)original_map);
    
    // Modify via map_assoc_cow (should create copy)
    CljObject *new_key = intern_symbol_global("b");
    CljValue new_val = make_fixnum(20);
    CljValue new_map = map_assoc_cow((CljValue)original_map, (CljValue)new_key, new_val);
    
    // New map should be different from original
    TEST_ASSERT_NOT_NULL(new_map);
    TEST_ASSERT_NOT_EQUAL_PTR(original_map, new_map);
    
    // Parent should be preserved in new map (verified by accessing parent key)
    CljValue parent_result = map_get(new_map, (CljValue)parent_key);
    TEST_ASSERT_NOT_NULL(parent_result);
    TEST_ASSERT_TRUE(is_fixnum(parent_result));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(parent_result));
    
    RELEASE((CljObject*)new_map);
    RELEASE((CljObject*)original_map);
    RELEASE((CljObject*)parent_map);
}

TEST(test_map_assoc_cow_parent_retained) {
    // Test that parent is retained during Copy-on-Write
    CljMap *parent_map = make_map(4);
    CljMap *original_map = make_map(4);
    
    // Set parent using map_assoc with magic key
    map_assoc((CljValue)original_map, g_magic_parent_key, (CljValue)parent_map);
    
    // Store original reference count
    uint16_t parent_rc_before = parent_map->base.rc;
    
    // Create second reference to force Copy-on-Write
    RETAIN((CljObject*)original_map);
    
    // Modify via map_assoc_cow
    CljObject *new_key = intern_symbol_global("x");
    CljValue new_val = make_fixnum(42);
    CljValue new_map = map_assoc_cow((CljValue)original_map, (CljValue)new_key, new_val);
    
    // Parent should be retained (reference count increased)
    TEST_ASSERT_NOT_NULL(new_map);
    uint16_t parent_rc_after = parent_map->base.rc;
    TEST_ASSERT_EQUAL_INT(parent_rc_before + 1, parent_rc_after);
    
    RELEASE((CljObject*)new_map);
    RELEASE((CljObject*)original_map);
    RELEASE((CljObject*)parent_map);
}

