#include "tests_common.h"
#include "../channel.h"

// Test that map_assoc correctly updates values for interned symbol keys
TEST(test_map_assoc_updates_interned_symbol_key) {
    EvalState *st = evalstate_new();
    CljMap *map = (CljMap*)make_map(2);
    CljObject *kw = intern_symbol(NULL, ":closed");
    
    // Set initial value
    map_assoc((CljObject*)map, kw, (CljValue)clj_false);
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljValue)map, (CljValue)kw)) == SPECIAL_FALSE);
    
    // Update value (should update, not add)
    map_assoc((CljObject*)map, intern_symbol(NULL, ":closed"), (CljValue)clj_true);
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljValue)map, (CljValue)kw)) == SPECIAL_TRUE);
    TEST_ASSERT_EQUAL_INT(1, map->count); // Should update, not add
    
    evalstate_free(st);
    RELEASE((CljObject*)map);
}

// Test that map_assoc works correctly with channel pattern (like result channels)
TEST(test_map_assoc_channel_pattern) {
    EvalState *st = evalstate_new();
    
    // Create channel like make_result_channel
    CljMap *chan = (CljMap*)make_map(2);
    map_assoc((CljObject*)chan, intern_symbol(NULL, ":value"), NULL);
    map_assoc((CljObject*)chan, intern_symbol(NULL, ":closed"), (CljValue)clj_false);
    
    // Update like channel_put_and_close does
    map_assoc((CljObject*)chan, intern_symbol(NULL, ":closed"), (CljValue)clj_true);
    
    // Verify closed is true
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljValue)chan, (CljValue)kw_closed)) == SPECIAL_TRUE);
    
    evalstate_free(st);
    RELEASE((CljObject*)chan);
}

// Test that ASSIGN works with Immediates (clj_true, clj_false, fixnums)
TEST(test_assign_with_immediates) {
    CljObject *var = NULL;
    
    // Test ASSIGN with clj_true
    ASSIGN(var, (CljObject*)clj_true);
    TEST_ASSERT_EQUAL((CljValue)var, clj_true);
    TEST_ASSERT_TRUE(is_special((CljValue)var));
    TEST_ASSERT_TRUE(as_special((CljValue)var) == SPECIAL_TRUE);
    
    // Test ASSIGN with clj_false
    ASSIGN(var, (CljObject*)clj_false);
    TEST_ASSERT_EQUAL((CljValue)var, clj_false);
    TEST_ASSERT_TRUE(is_special((CljValue)var));
    TEST_ASSERT_TRUE(as_special((CljValue)var) == SPECIAL_FALSE);
    
    // Test ASSIGN with fixnum
    CljValue fixnum_val = fixnum(42);
    ASSIGN(var, (CljObject*)fixnum_val);
    TEST_ASSERT_EQUAL((CljValue)var, fixnum_val);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)var));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)var));
    
    // Test ASSIGN with different fixnum
    CljValue fixnum_val2 = fixnum(100);
    ASSIGN(var, (CljObject*)fixnum_val2);
    TEST_ASSERT_EQUAL((CljValue)var, fixnum_val2);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)var));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum((CljValue)var));
}

// Test that ASSIGN with Immediates works in map context
TEST(test_assign_immediates_in_map) {
    EvalState *st = evalstate_new();
    CljMap *map = (CljMap*)make_map(4);
    CljObject *kw = intern_symbol(NULL, ":test");
    
    // Add immediate values using ASSIGN pattern
    map_assoc((CljObject*)map, kw, (CljValue)clj_false);
    CljValue val1 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val1));
    TEST_ASSERT_TRUE(as_special(val1) == SPECIAL_FALSE);
    
    // Update to clj_true
    map_assoc((CljObject*)map, kw, (CljValue)clj_true);
    CljValue val2 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val2));
    TEST_ASSERT_TRUE(as_special(val2) == SPECIAL_TRUE);
    
    // Update to fixnum
    map_assoc((CljObject*)map, kw, fixnum(123));
    CljValue val3 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_fixnum(val3));
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(val3));
    
    // Update back to clj_false
    map_assoc((CljObject*)map, kw, (CljValue)clj_false);
    CljValue val4 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val4));
    TEST_ASSERT_TRUE(as_special(val4) == SPECIAL_FALSE);
    
    TEST_ASSERT_EQUAL_INT(1, map->count); // Should update, not add
    
    evalstate_free(st);
    RELEASE((CljObject*)map);
}
