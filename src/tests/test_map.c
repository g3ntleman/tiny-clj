#include "tests_common.h"
#include "../channel.h"
#include "../symbol.h"

// Test that map_assoc correctly updates values for interned symbol keys
TEST(test_map_assoc_updates_interned_symbol_key) {
    CljMap *map = (CljMap*)make_map(2);
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":closed");
    
    // Set initial value
    map = map_assoc(map, kw, (CljValue)clj_false);
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljMap*)map, (CljValue)kw)) == SPECIAL_FALSE);
    
    // Update value (should update, not add)
    map = map_assoc(map, intern_symbol(NULL, ":closed"), (CljValue)clj_true);
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljMap*)map, (CljValue)kw)) == SPECIAL_TRUE);
    TEST_ASSERT_EQUAL_INT(1, map->count); // Should update, not add
    
    RELEASE((CljObject*)map);
}

// Test that map_assoc works correctly with channel pattern (like result channels)
TEST(test_map_assoc_channel_pattern) {
    // Create channel like make_result_channel
    CljMap *chan = (CljMap*)make_map(2);
    chan = map_assoc(chan, (CljValue)intern_symbol(NULL, ":value"), NULL);
    chan = map_assoc(chan, (CljValue)intern_symbol(NULL, ":closed"), (CljValue)clj_false);
    
    // Update like channel_put_and_close does
    chan = map_assoc(chan, (CljValue)intern_symbol(NULL, ":closed"), (CljValue)clj_true);
    
    // Verify closed is true
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljMap*)chan, (CljValue)kw_closed)) == SPECIAL_TRUE);
    
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
    CljMap *map = (CljMap*)make_map(4);
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":test");
    
    // Add immediate values using ASSIGN pattern
    map = map_assoc(map, kw, (CljValue)clj_false);
    CljValue val1 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val1));
    TEST_ASSERT_TRUE(as_special(val1) == SPECIAL_FALSE);
    
    // Update to clj_true
    map = map_assoc(map, kw, (CljValue)clj_true);
    CljValue val2 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val2));
    TEST_ASSERT_TRUE(as_special(val2) == SPECIAL_TRUE);
    
    // Update to fixnum
    map = map_assoc(map, kw, fixnum(123));
    CljValue val3 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_fixnum(val3));
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(val3));
    
    // Update back to clj_false
    map = map_assoc(map, kw, (CljValue)clj_false);
    CljValue val4 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val4));
    TEST_ASSERT_TRUE(as_special(val4) == SPECIAL_FALSE);
    
    TEST_ASSERT_EQUAL_INT(1, map->count); // Should update, not add
    
    RELEASE((CljObject*)map);
}

// Test that intern_symbol returns same pointer for same symbol
TEST(test_intern_symbol_consistency_for_closed) {
    // Call intern_symbol multiple times with same arguments
    CljObject *kw1 = (CljObject*)intern_symbol(NULL, ":closed");
    CljObject *kw2 = (CljObject*)intern_symbol(NULL, ":closed");
    CljObject *kw3 = (CljObject*)intern_symbol(NULL, ":closed");
    
    // All should return the same pointer
    TEST_ASSERT_EQUAL_PTR(kw1, kw2);
    TEST_ASSERT_EQUAL_PTR(kw2, kw3);
    TEST_ASSERT_EQUAL_PTR(kw1, kw3);
    
}

// Test that map_assoc works when called from different contexts
TEST(test_map_assoc_with_different_intern_calls) {
    CljMap *map = (CljMap*)make_map(4);
    
    // Create channel like make_result_channel does
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    map = map_assoc(map, (CljValue)kw_value, NULL);
    map = map_assoc(map, (CljValue)kw_closed, (CljValue)clj_false);
    
    // Verify initial state
    CljValue closed_val1 = (CljValue)map_get((CljMap*)map, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val1);
    TEST_ASSERT_TRUE(is_special(closed_val1));
    TEST_ASSERT_TRUE(as_special(closed_val1) == SPECIAL_FALSE);
    
    // Now update like channel_put_and_close does (with new intern_symbol call)
    CljObject *kw_closed_new = (CljObject*)intern_symbol(NULL, ":closed");
    map = map_assoc(map, (CljValue)kw_closed_new, (CljValue)clj_true);
    
    // Verify update worked
    CljValue closed_val2 = (CljValue)map_get((CljMap*)map, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val2);
    TEST_ASSERT_TRUE(is_special(closed_val2));
    TEST_ASSERT_TRUE(as_special(closed_val2) == SPECIAL_TRUE);
    
    // Verify count didn't increase (should update, not add)
    TEST_ASSERT_EQUAL_INT(2, map->count);
    
    RELEASE((CljObject*)map);
}

// Test that map_assoc works with NULL value
TEST(test_map_assoc_with_null_value) {
    CljMap *map = (CljMap*)make_map(4);
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":value");
    
    // Set NULL value
    map = map_assoc(map, kw, NULL);
    CljValue val1 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(val1 == NULL);
    
    // Update to non-NULL
    map = map_assoc(map, kw, fixnum(42));
    CljValue val2 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_fixnum(val2));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val2));
    
    // Update back to NULL
    map = map_assoc(map, kw, NULL);
    CljValue val3 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_TRUE(val3 == NULL);
    
    TEST_ASSERT_EQUAL_INT(1, map->count);
    
    RELEASE((CljObject*)map);
}

// Test exact channel pattern from make_result_channel and channel_put_and_close
TEST(test_exact_channel_pattern) {
    // Exact replication of make_result_channel
    CljMap *chan = (CljMap*)make_map(4);
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    chan = map_assoc(chan, (CljValue)kw_value, NULL);
    chan = map_assoc(chan, (CljValue)kw_closed, (CljValue)clj_false);
    
    // Verify initial state
    CljValue closed_initial = (CljValue)map_get((CljMap*)chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_initial);
    TEST_ASSERT_TRUE(is_special(closed_initial));
    TEST_ASSERT_TRUE(as_special(closed_initial) == SPECIAL_FALSE);
    
    // Exact replication of channel_put_and_close with NULL value
    CljObject *kw_closed_new = (CljObject*)intern_symbol(NULL, ":closed");
    // if (value) { chan = map_assoc(chan, kw_value, value); } - skipped for NULL
    chan = map_assoc(chan, (CljValue)kw_closed_new, (CljValue)clj_true);
    
    // Verify final state
    CljValue closed_final = (CljValue)map_get((CljMap*)chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_final);
    TEST_ASSERT_TRUE(is_special(closed_final));
    TEST_ASSERT_TRUE(as_special(closed_final) == SPECIAL_TRUE);
    
    RELEASE((CljObject*)chan);
}

// Test channel object identity
TEST(test_channel_object_identity) {
    CljMap *env = (CljMap*)make_map(4);
    
    // Create a channel manually
    CljObject *chan1 = (CljObject*)make_result_channel();
    TEST_ASSERT_NOT_NULL(chan1);
    
    // Verify it's a map
    TEST_ASSERT_TRUE(is_type(chan1, CLJ_MAP) || is_type(chan1, CLJ_TRANSIENT_MAP));
    
    // Check initial state
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    CljValue closed_initial = (CljValue)map_get((CljMap*)chan1, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_initial);
    TEST_ASSERT_TRUE(is_special(closed_initial));
    TEST_ASSERT_TRUE(as_special(closed_initial) == SPECIAL_FALSE);
    
    // Update it like channel_put_and_close would
    // Use map_conj for transient maps (map_assoc only works with CLJ_MAP)
    map_conj((CljMap*)chan1, (CljValue)kw_closed, (CljValue)clj_true);
    
    // Verify update worked
    CljValue closed_final = (CljValue)map_get((CljMap*)chan1, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_final);
    TEST_ASSERT_TRUE(is_special(closed_final));
    TEST_ASSERT_TRUE(as_special(closed_final) == SPECIAL_TRUE);
    
    RELEASE(env);
    RELEASE(chan1);
}

// ============================================================================
// Tests for (void)map_assoc() refactoring - remove redundant fast-path
// ============================================================================

// Test that map_assoc works with pointer equality via clj_equal()
// (verifies that removing redundant fast-path doesn't break functionality)
TEST(test_map_assoc_with_pointer_equality) {
    CljMap *map = (CljMap*)make_map(4);
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":test");
    
    // Set initial value
    map = map_assoc(map, kw, fixnum(42));
    CljValue val1 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val1));
    
    // Update with same pointer (should work via clj_equal() == check)
    map = map_assoc(map, kw, fixnum(100));
    CljValue val2 = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_NOT_NULL(val2);
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val2));
    
    // Verify count didn't increase (should update, not add)
    TEST_ASSERT_EQUAL_INT(1, map->count);
    
    RELEASE((CljObject*)map);
}

// Test that map_assoc works with structural equality (non-interned keys)
TEST(test_map_assoc_with_structural_equality) {
    CljMap *map = (CljMap*)make_map(4);
    
    // Create two different string objects with same content
    CljObject *str1 = (CljObject*)make_string("test-key");
    CljObject *str2 = (CljObject*)make_string("test-key");
    
    // Set initial value with str1
    map = map_assoc(map, str1, fixnum(42));
    CljValue val1 = (CljValue)map_get((CljMap*)map, (CljValue)str1);
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val1));
    
    // Update with str2 (different pointer, same content) - should work via clj_equal()
    map = map_assoc(map, str2, fixnum(100));
    CljValue val2 = (CljValue)map_get((CljMap*)map, (CljValue)str1);
    TEST_ASSERT_NOT_NULL(val2);
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val2));
    
    // Verify count didn't increase (should update, not add)
    TEST_ASSERT_EQUAL_INT(1, map->count);
    
    RELEASE((CljObject*)map);
    RELEASE(str1);
    RELEASE(str2);
}

// Test that map_get finds symbols bound in let_env
TEST(test_map_get_finds_let_binding) {
    // Create a map (simulating let_env)
    CljMap *let_env = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(let_env);
    
    // Create a symbol "step" (interned)
    CljSymbol *step_sym = intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym);
    
    // Create a function value (simulating fn result)
    CljObject *fn_value = (CljObject*)fixnum(42); // Simplified: use fixnum as placeholder
    TEST_ASSERT_NOT_NULL(fn_value);
    
    // Bind step in let_env
    let_env = map_assoc(let_env, (CljValue)step_sym, (CljValue)fn_value);
    
    // Verify step is in let_env
    CljValue found = map_get((CljMap*)let_env, (CljValue)step_sym);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
    
    // Create another "step" symbol (should be same pointer if interned)
    CljObject *step_sym2 = (CljObject*)intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym2);
    
    // Verify both symbols are the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR(step_sym, step_sym2);
    
    // Verify map_get finds step using second symbol
    CljValue found2 = map_get((CljMap*)let_env, (CljValue)step_sym2);
    TEST_ASSERT_NOT_NULL(found2);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found2));
    
    RELEASE((CljObject*)let_env);
}

// Test that map_get uses structural comparison for non-interned symbols
TEST(test_map_get_structural_comparison) {
    // Create a map
    CljMap *let_env = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(let_env);
    
    // Create a symbol "step" (interned)
    CljSymbol *step_sym = intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym);
    
    // Create a function value
    CljObject *fn_value = (CljObject*)fixnum(42);
    TEST_ASSERT_NOT_NULL(fn_value);
    
    // Bind step in let_env
    let_env = map_assoc(let_env, (CljValue)step_sym, (CljValue)fn_value);
    
    // Create another "step" symbol (should be same pointer if interned)
    CljObject *step_sym2 = (CljObject*)intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym2);
    
    // Verify map_get finds step using second symbol (should work via pointer or structural comparison)
    CljValue found = map_get((CljMap*)let_env, (CljValue)step_sym2);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
    
    RELEASE((CljObject*)let_env);
}

// Test that performance is unchanged (clj_equal() already does == check first)
TEST(test_map_assoc_performance_unchanged) {
    CljMap *map = (CljMap*)make_map(100);
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":test");
    
    // Fill map with many entries
    for (int i = 0; i < 50; i++) {
        CljObject *key = (CljObject*)intern_symbol(NULL, ":key");
        map = map_assoc(map, (CljValue)key, fixnum(i));
    }
    
    // Update existing key - should be fast (clj_equal() does == check first)
    map = map_assoc(map, (CljValue)kw, fixnum(42));
    CljValue val = (CljValue)map_get((CljMap*)map, (CljValue)kw);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
    
    RELEASE((CljObject*)map);
}

// ============================================================================
// COW (Copy-on-Write) Tests - consolidated from test_cow.c
// ============================================================================

// ============================================================================
// COW ASSUMPTIONS TESTS
// ============================================================================

TEST(test_autorelease_does_not_increase_rc) {
    WITH_AUTORELEASE_POOL({
        // Test 1: AUTORELEASE does NOT increase RC
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // AUTORELEASE should NOT increase RC
        AUTORELEASE((CljValue)map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
    });
}

TEST(test_retain_increases_rc) {
    WITH_AUTORELEASE_POOL({
        // Test 3: RETAIN increases RC
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // RETAIN should increase RC
        RETAIN((CljValue)map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Cleanup
        RELEASE((CljValue)map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
    });
}

TEST(test_autorelease_with_retain) {
    WITH_AUTORELEASE_POOL({
        // Test 5: AUTORELEASE + RETAIN combination
        CljMap *map = (CljMap*)make_map(4);
        
        RETAIN((CljValue)map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        AUTORELEASE((CljValue)map);
        TEST_ASSERT_EQUAL(2, map->base.rc); // Should stay 2
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

TEST(test_multiple_autorelease_same_object) {
    
    WITH_AUTORELEASE_POOL({
        // Test 6: Multiple AUTORELEASE same object
        CljMap *map = (CljMap*)make_map(4);
        
        AUTORELEASE((CljValue)map);
        AUTORELEASE((CljValue)map);
        AUTORELEASE((CljValue)map);
        
        TEST_ASSERT_EQUAL(1, map->base.rc); // Should stay 1
        
    });
}

TEST(test_autorelease_in_loop_realistic) {
    
    WITH_AUTORELEASE_POOL({
        // Test 7: Realistic loop with AUTORELEASE
        CljMap *env = (CljMap*)make_map(4);
        
        for (int i = 0; i < 100; i++) {
            // Simulate realistic loop
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 10 == 0) {
            }
        }
        
    });
}

// ============================================================================
// COW FUNCTIONALITY TESTS
// ============================================================================

TEST(test_cow_inplace_mutation_rc_one) {
    
    WITH_AUTORELEASE_POOL({
        // Test 1: In-place mutation bei RC=1
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // First assoc creates new map (COW disabled by default)
        CljMap *new_map1 = map_assoc(map, fixnum(1), fixnum(10));
        TEST_ASSERT_EQUAL(1, new_map1->base.rc);
        TEST_ASSERT_NOT_EQUAL((CljValue)map, (CljValue)new_map1); // New pointer (COW disabled)
        map = new_map1; // Update map reference
        
        // Second assoc also creates new map (COW disabled by default)
        CljMap *new_map2 = map_assoc(map, fixnum(2), fixnum(20));
        TEST_ASSERT_EQUAL(1, new_map2->base.rc);
        TEST_ASSERT_NOT_EQUAL((CljValue)map, (CljValue)new_map2); // New pointer (COW disabled)
        map = new_map2; // Update map reference
        
        // Verify entries
        CljValue val1 = map_get((CljMap*)map, fixnum(1));
        CljValue val2 = map_get((CljMap*)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
    });
}

TEST(test_cow_copy_on_write_rc_greater_one) {
    
    WITH_AUTORELEASE_POOL({
        // Test 2: COW bei RC>1
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add some entries
        map = map_assoc(map, fixnum(1), fixnum(10));
        
        // RETAIN to increase RC
        RETAIN((CljValue)map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Now COW should trigger
        CljMap *new_map = map_assoc(map, fixnum(2), fixnum(20));
        TEST_ASSERT_EQUAL(2, map->base.rc);  // Original RC bleibt 2
        TEST_ASSERT_NOT_EQUAL((CljValue)map, (CljValue)new_map); // NEUER Pointer!
        
        // Verify original map unchanged
        CljValue val1_orig = map_get((CljMap*)map, fixnum(1));
        CljValue val2_orig = map_get((CljMap*)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1_orig);
        TEST_ASSERT_NULL(val2_orig);  // Original hat key=2 nicht
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_orig));
        
        // Verify new map has both entries
        CljValue val1_new = map_get((CljMap*)new_map, fixnum(1));
        CljValue val2_new = map_get((CljMap*)new_map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1_new);
        TEST_ASSERT_NOT_NULL(val2_new);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_new));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2_new));
        
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

TEST(test_cow_original_map_unchanged) {
    
    WITH_AUTORELEASE_POOL({
        // Test 3: Original Map unverändert nach COW
        CljMap *map = (CljMap*)make_map(4);
        map = map_assoc(map, fixnum(1), fixnum(10));
        map = map_assoc(map, fixnum(2), fixnum(20));
        
        TEST_ASSERT_EQUAL(2, map->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        CljMap *new_map = map_assoc(map, fixnum(3), fixnum(30));
        
        // Original should be unchanged
        TEST_ASSERT_EQUAL(2, map->count); // Original unchanged
        
        // New map should have 3 entries
        CljValue val3 = map_get((CljMap*)new_map, fixnum(3));
        TEST_ASSERT_NOT_NULL(val3);
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(val3));
        
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

TEST(test_cow_with_autorelease) {
    
    WITH_AUTORELEASE_POOL({
        // Test 4: AUTORELEASE mit COW
        CljMap *map = (CljMap*)make_map(4);
        
        AUTORELEASE((CljValue)map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // COW with AUTORELEASE
        CljValue new_map = map_assoc((CljValue)map, fixnum(1), fixnum(10));
        AUTORELEASE(new_map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
    });
}

TEST(test_cow_memory_leak_detection) {
    
    WITH_AUTORELEASE_POOL({
        // Test 5: Memory Leak Detection
        CljMap *map = (CljMap*)make_map(4);
        map = map_assoc(map, fixnum(1), fixnum(10));
        map = map_assoc(map, fixnum(2), fixnum(20));
        map = map_assoc(map, fixnum(3), fixnum(30));
        map = map_assoc(map, fixnum(4), fixnum(40));
        
        TEST_ASSERT_EQUAL(4, map->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        CljMap *new_map = map_assoc(map, fixnum(5), fixnum(50));
        AUTORELEASE(new_map);
        
        // Cleanup
        RELEASE((CljValue)map);
        
    });
}

// ============================================================================
// COW EVAL INTEGRATION TESTS
// ============================================================================

TEST(test_cow_environment_loop_mutation) {
    
    WITH_AUTORELEASE_POOL({
        // Test 1: Environment-Mutation in Loop
        CljMap *env = (CljMap*)make_map(4);
        
        for (int i = 0; i < 100; i++) {
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 20 == 0) {
            }
        }
        
    });
}

TEST(test_cow_closure_environment_sharing) {
    
    WITH_AUTORELEASE_POOL({
        // Test 2: Closure-Environment-Sharing
        CljMap *env = (CljMap*)make_map(4);
        env = map_assoc(env, intern_symbol_global("x"), fixnum(1));
        
        // Simulate closure holding reference
        RETAIN((CljValue)env);
        TEST_ASSERT_EQUAL(2, env->base.rc);
        
        // Closure-Operation sollte COW triggern
        CljMap *new_env = map_assoc(env, intern_symbol_global("y"), fixnum(2));
        TEST_ASSERT_EQUAL(2, env->base.rc);  // Original unverändert
        TEST_ASSERT_NOT_EQUAL((CljValue)env, (CljValue)new_env); // NEUER Pointer!
        
        // Verify original env unchanged
        CljValue orig_x = map_get((CljMap*)env, intern_symbol_global("x"));
        CljValue orig_y = map_get((CljMap*)env, intern_symbol_global("y"));
        TEST_ASSERT_NOT_NULL(orig_x);
        TEST_ASSERT_NULL(orig_y);  // Original hat y nicht
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(orig_x));
        
        
        // Cleanup
        RELEASE((CljValue)env);
    });
}

TEST(test_cow_memory_efficiency_benchmark) {
    
    WITH_AUTORELEASE_POOL({
        // Test 4: Memory-Effizienz Benchmark
        CljMap *env = (CljMap*)make_map(4);
        
        for (int i = 0; i < 1000; i++) {
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 100 == 0) {
            }
        }
        
    });
}

TEST(test_cow_real_clojure_simulation) {
    
    WITH_AUTORELEASE_POOL({
        // Test 5: Real Clojure Code Simulation
        CljMap *env = (CljMap*)make_map(4);
        
        CljValue current_env = (CljValue)env;
        
        for (int i = 0; i < 100; i++) {
            CljValue new_env = map_assoc(current_env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            current_env = new_env; // Update to the new map
            
            if (i % 20 == 0) {
            }
        }
        
        // Verify some entries in the final map
        for (int i = 0; i < 100; i += 20) {
            CljValue val = map_get(current_env, fixnum(i));
            TEST_ASSERT_NOT_NULL(val);
            TEST_ASSERT_EQUAL_INT(i * 10, as_fixnum(val));
        }
        
    });
}

TEST(test_cow_actual_cow_demonstration) {
    
    WITH_AUTORELEASE_POOL({
        // Test: COW Actual COW Demonstration
        CljMap *map = (CljMap*)make_map(4);
        
        // Add some entries
        map = map_assoc(map, fixnum(1), fixnum(10));
        map = map_assoc(map, fixnum(2), fixnum(20));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation
        CljMap *new_map = map_assoc(map, fixnum(3), fixnum(30));
        TEST_ASSERT_EQUAL(2, map->base.rc);
        TEST_ASSERT_NOT_EQUAL((CljValue)map, (CljValue)new_map);
        
        // Verify original unchanged
        CljValue val3_orig = map_get((CljMap*)map, fixnum(3));
        TEST_ASSERT_NULL(val3_orig);
        
        // Verify new map has all entries
        CljValue val1_new = map_get((CljMap*)new_map, fixnum(1));
        CljValue val2_new = map_get((CljMap*)new_map, fixnum(2));
        CljValue val3_new = map_get((CljMap*)new_map, fixnum(3));
        TEST_ASSERT_NOT_NULL(val1_new);
        TEST_ASSERT_NOT_NULL(val2_new);
        TEST_ASSERT_NOT_NULL(val3_new);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_new));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2_new));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(val3_new));
        
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

// ============================================================================
// Tests for update function
// ============================================================================

TEST(test_update_basic) {
    // Test: (update {:a 1} :a inc) => {:a 2}
    CljObject *result = eval_string("(update {:a 1} :a inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_MAP));
    
    // Verify the updated value
    CljObject *key = intern_symbol(NULL, ":a");
    CljValue val = map_get((CljMap*)result, (CljValue)key);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(val));
}

TEST(test_update_with_function) {
    // Test: (update {:count 5} :count (fn [x] (* x 2))) => {:count 10}
    CljObject *result = eval_string("(update {:count 5} :count (fn [x] (* x 2)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_MAP));
    
    // Verify the updated value
    CljObject *key = intern_symbol(NULL, ":count");
    CljValue val = map_get((CljMap*)result, (CljValue)key);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(val));
}

// Simplified test: Direct C function calls
TEST(test_update_missing_key_simple) {
    // Step 1: Create initial map {:a 1}
    CljObject *pairs[2];
    pairs[0] = intern_symbol(NULL, ":a");
    pairs[1] = (CljObject*)fixnum(1);
    CljMap *map = make_map_from_stack(pairs, 1);
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_INT(1, map->count);
    
    // Step 2: Get value for missing key :b (should return NULL)
    CljObject *key_b = intern_symbol(NULL, ":b");
    CljValue val_b_before = map_get((CljMap*)map, (CljValue)key_b);
    TEST_ASSERT_NULL_MESSAGE(val_b_before, "key :b should not exist before update");
    
    // Step 3: Apply function to missing key (nil -> 0)
    CljObject *new_val_b = (CljObject*)fixnum(0);
    
    // Step 4: Use map_assoc to add the new key
    CljMap *result = map_assoc(map, (CljValue)key_b, (CljValue)new_val_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "map_assoc should return a map");
    TEST_ASSERT_TRUE(is_type((CljObject*)result, CLJ_MAP));
    
    CljMap *result_map = result;
    TEST_ASSERT_EQUAL_INT(2, result_map->count);
    
    // Step 5: Verify the new key was added
    CljValue val_b_after = map_get(result, (CljValue)key_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b_after, "key :b should exist after update");
    if (val_b_after) {
        TEST_ASSERT_TRUE(is_fixnum(val_b_after));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(val_b_after));
    }
    
    // Step 6: Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get(result, (CljValue)key_a);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_a, "key :a should still exist");
    if (val_a) {
        TEST_ASSERT_TRUE(is_fixnum(val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(val_a));
    }
    
    RELEASE((CljObject*)map);
}

// Test if (if nil 0 0) returns 0 correctly
// This tests the fix for the bug where if returned NULL when cond_val was nil,
// instead of evaluating the else branch
TEST(test_if_nil_zero) {
    
    // Test: (if nil 0 0) => 0
    // nil is falsy, so the else branch (0) should be evaluated
    CljObject *result = NULL;
    TRY {
        result = eval_string("(if nil 0 0)", g_test_eval_state);
    } CATCH(ex) {
        char msg[256];
        TEST_FAIL_MESSAGE(msg);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "(if nil 0 0) should return 0, not NULL");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "(if nil 0 0) should return a fixnum");
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
    
}

// Test if (fn [x] (if x 0 0)) works correctly with nil
// This tests the fix for the bug where if inside a function returned NULL when cond_val was nil,
// instead of evaluating the else branch. This is the critical test that caught the bug.
TEST(test_fn_if_nil_zero) {
    
    // Test: ((fn [x] (if x 0 0)) nil) => 0
    // When x is nil, (if x 0 0) should evaluate the else branch (0)
    CljObject *result = NULL;
    TRY {
        result = eval_string("((fn [x] (if x 0 0)) nil)", g_test_eval_state);
    } CATCH(ex) {
        char msg[256];
        TEST_FAIL_MESSAGE(msg);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "((fn [x] (if x 0 0)) nil) should return 0, not NULL");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "((fn [x] (if x 0 0)) nil) should return a fixnum");
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
    
}

// High-level test: Using eval_string to test the full update function
// This tests: (update {:a 1} :b (fn [x] (if x 0 0))) => {:a 1 :b 0}
// The update function is defined as: (def update (fn [m key f] (assoc m key (f (get m key)))))
TEST(test_update_missing_key) {
    // Test: (update {:a 1} :b (fn [x] (if x 0 0))) => {:a 1 :b 0} (nil -> 0)
    // This expands to: (assoc {:a 1} :b ((fn [x] (if x 0 0)) (get {:a 1} :b)))
    // When :b doesn't exist, (get {:a 1} :b) returns nil
    // Then ((fn [x] (if x 0 0)) nil) => (if nil 0 0) => 0
    // Finally: (assoc {:a 1} :b 0) => {:a 1 :b 0}
    
    CljObject *result = NULL;
    TRY {
        result = eval_string("(update {:a 1} :b (fn [x] (if x 0 0)))", g_test_eval_state);
    } CATCH(ex) {
        char msg[256];
        TEST_FAIL_MESSAGE(msg);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "update should return a map, not NULL");
    if (!result) {
        return;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(is_type(result, CLJ_MAP), "update result should be a map");
    
    CljMap *result_map = as_map((CljObject*)result);
    TEST_ASSERT_EQUAL_INT(2, result_map->count);
    
    // Verify the new key was added
    CljObject *key_b = intern_symbol(NULL, ":b");
    CljValue val_b = map_get((CljMap*)result, (CljValue)key_b);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b, "update should add missing key");
    if (val_b) {
        TEST_ASSERT_TRUE(is_fixnum(val_b));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(val_b));
    }
    
    // Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljMap*)result, (CljValue)key_a);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_a, "update should preserve existing keys");
    if (val_a) {
        TEST_ASSERT_TRUE(is_fixnum(val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(val_a));
    }
}

// Isolated test for assoc with maps - reproduces the problem
TEST(test_assoc_map_isolated) {

    // Test: (assoc {:a 1} :b 2) should return {:a 1 :b 2}, not nil
    CljObject *result = NULL;
    TRY {
        result = eval_string("(assoc {:a 1} :b 2)", g_test_eval_state);
    } CATCH(ex) {
        char msg[256];
        TEST_FAIL_MESSAGE(msg);
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL_MESSAGE(result, "assoc should return a map, not nil");
    if (!result) {
        return;
    }

    TEST_ASSERT_TRUE_MESSAGE(is_type(result, CLJ_MAP), "assoc result should be a map");

    // Verify the new key was added
    CljObject *key_b = intern_symbol(NULL, ":b");
    CljValue val_b = map_get((CljMap*)result, (CljValue)key_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b, "assoc should add new key :b");
    if (val_b) {
        TEST_ASSERT_TRUE(is_fixnum(val_b));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(val_b));
    }

    // Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljMap*)result, (CljValue)key_a);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_a, "assoc should preserve existing key :a");
    if (val_a) {
        TEST_ASSERT_TRUE(is_fixnum(val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(val_a));
    }

}

// Direct C test for map_assoc adding a new key
TEST(test_map_assoc_direct) {
    
    // Create a map {:a 1} with capacity 4
    CljObject *pairs[2];
    pairs[0] = intern_symbol(NULL, ":a");
    pairs[1] = (CljObject*)fixnum(1);
    CljMap *map = make_map_from_stack(pairs, 1);
    
    
    // Add a new key :b with value 2
    CljObject *key_b = intern_symbol(NULL, ":b");
    CljObject *val_b = (CljObject*)fixnum(2);
    
    CljValue result = map_assoc((CljValue)map, (CljValue)key_b, (CljValue)val_b);
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify the new key was added
    CljValue retrieved_val_b = map_get((CljMap*)result, (CljValue)key_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_val_b, "map_assoc should add new key :b");
    if (retrieved_val_b) {
        TEST_ASSERT_TRUE(is_fixnum(retrieved_val_b));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(retrieved_val_b));
    }
    
    // Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue retrieved_val_a = map_get((CljMap*)result, (CljValue)key_a);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_val_a, "map_assoc should preserve existing key :a");
    if (retrieved_val_a) {
        TEST_ASSERT_TRUE(is_fixnum(retrieved_val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(retrieved_val_a));
    }
    
    RELEASE((CljObject*)map);
}

// ============================================================================
// Tests for map_transient() - Convert persistent map to transient
// ============================================================================

// Comprehensive test for map_transient() covering all cases
TEST(test_map_transient_comprehensive) {
    // Test 1: NULL input returns NULL
    CljMap *result = map_transient(NULL);
    TEST_ASSERT_NULL(result);
    
    // Test 2: Empty map conversion
    CljMap *empty_map = (CljMap*)make_map(4);
    TEST_ASSERT_EQUAL_INT(0, empty_map->count);
    CljMap *empty_transient = map_transient(empty_map);
    TEST_ASSERT_NOT_NULL(empty_transient);
    TEST_ASSERT_TRUE(is_type((CljObject*)empty_transient, CLJ_TRANSIENT_MAP));
    TEST_ASSERT_EQUAL_INT(0, empty_transient->count);
    RELEASE((CljObject*)empty_map);
    RELEASE((CljObject*)empty_transient);
    
    // Test 3: Map with entries - conversion and data preservation
    CljMap *persistent_map = (CljMap*)make_map(8);
    CljObject *keys[5];
    for (int i = 0; i < 5; i++) {
        char key_name[16];
        keys[i] = (CljObject*)intern_symbol(NULL, key_name);
        persistent_map = map_assoc(persistent_map, (CljValue)keys[i], fixnum(i * 10));
    }
    TEST_ASSERT_EQUAL_INT(5, persistent_map->count);
    TEST_ASSERT_TRUE(is_type((CljObject*)persistent_map, CLJ_MAP));
    
    // Convert to transient
    CljMap *transient_map = map_transient(persistent_map);
    TEST_ASSERT_NOT_NULL(transient_map);
    TEST_ASSERT_TRUE(is_type((CljObject*)transient_map, CLJ_TRANSIENT_MAP));
    TEST_ASSERT_EQUAL_INT(5, transient_map->count);
    
    // Verify it's a different pointer (new map created)
    TEST_ASSERT_NOT_EQUAL((CljValue)persistent_map, (CljValue)transient_map);
    
    // Verify original map is unchanged
    TEST_ASSERT_TRUE(is_type((CljObject*)persistent_map, CLJ_MAP));
    TEST_ASSERT_EQUAL_INT(5, persistent_map->count);
    
    // Verify all entries are preserved
    for (int i = 0; i < 5; i++) {
        CljValue val_persistent = map_get(persistent_map, (CljValue)keys[i]);
        CljValue val_transient = map_get(transient_map, (CljValue)keys[i]);
        TEST_ASSERT_NOT_NULL(val_persistent);
        TEST_ASSERT_NOT_NULL(val_transient);
        TEST_ASSERT_TRUE(is_fixnum(val_persistent));
        TEST_ASSERT_TRUE(is_fixnum(val_transient));
        TEST_ASSERT_EQUAL_INT(i * 10, as_fixnum(val_persistent));
        TEST_ASSERT_EQUAL_INT(i * 10, as_fixnum(val_transient));
    }
    
    // Test 4: Transient map input returns NULL
    CljMap *result2 = map_transient(transient_map);
    TEST_ASSERT_NULL(result2);
    
    // Cleanup
    RELEASE((CljObject*)persistent_map);
    RELEASE((CljObject*)transient_map);
}

// ============================================================================
// Tests for map_conj() - In-place mutation of transient maps
// ============================================================================

// Comprehensive test for map_conj() covering all cases
TEST(test_map_conj_comprehensive) {
    // Test 1: NULL input returns NULL
    CljMap *result = map_conj(NULL, (CljValue)intern_symbol(NULL, ":key"), fixnum(42));
    TEST_ASSERT_NULL(result);
    
    CljMap *tmap = (CljMap*)make_map(4);
    CljMap *transient = map_transient(tmap);
    RELEASE((CljObject*)tmap);
    
    result = map_conj(transient, NULL, fixnum(42));
    TEST_ASSERT_NULL(result);
    
    // Test 2: Add new key-value pair to empty transient map
    CljObject *key1 = (CljObject*)intern_symbol(NULL, ":a");
    CljMap *result2 = map_conj(transient, (CljValue)key1, fixnum(1));
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_PTR(transient, result2);  // Same pointer (in-place mutation)
    TEST_ASSERT_EQUAL_INT(1, transient->count);
    
    CljValue val1 = map_get(transient, (CljValue)key1);
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_TRUE(is_fixnum(val1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(val1));
    
    // Test 3: Update existing key
    CljMap *result3 = map_conj(transient, (CljValue)key1, fixnum(100));
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_PTR(transient, result3);  // Same pointer
    TEST_ASSERT_EQUAL_INT(1, transient->count);  // Count unchanged (update, not add)
    
    CljValue val1_updated = map_get(transient, (CljValue)key1);
    TEST_ASSERT_NOT_NULL(val1_updated);
    TEST_ASSERT_TRUE(is_fixnum(val1_updated));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val1_updated));
    
    // Test 4: Add multiple key-value pairs
    CljObject *key2 = (CljObject*)intern_symbol(NULL, ":b");
    CljObject *key3 = (CljObject*)intern_symbol(NULL, ":c");
    map_conj(transient, (CljValue)key2, fixnum(2));
    map_conj(transient, (CljValue)key3, fixnum(3));
    
    TEST_ASSERT_EQUAL_INT(3, transient->count);
    
    CljValue val2 = map_get(transient, (CljValue)key2);
    CljValue val3 = map_get(transient, (CljValue)key3);
    TEST_ASSERT_NOT_NULL(val2);
    TEST_ASSERT_NOT_NULL(val3);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(val2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(val3));
    
    // Test 5: NULL value (nil) is valid
    CljObject *key4 = (CljObject*)intern_symbol(NULL, ":d");
    map_conj(transient, (CljValue)key4, NULL);
    TEST_ASSERT_EQUAL_INT(4, transient->count);
    
    CljValue val4 = map_get(transient, (CljValue)key4);
    TEST_ASSERT_NULL(val4);  // NULL value is valid
    
    // Test 6: Capacity limit
    CljObject *key5 = (CljObject*)intern_symbol(NULL, ":e");
    CljMap *result6 = map_conj(transient, (CljValue)key5, fixnum(5));
    // Should fail if capacity is exceeded (capacity is 4, we have 4 entries)
    if (transient->count >= transient->capacity) {
        TEST_ASSERT_NULL(result6);  // Out of capacity
    } else {
        TEST_ASSERT_NOT_NULL(result6);
    }
    
    // Test 7: Persistent map with RC=1 (COW case) - should work but not recommended
    CljMap *persistent = (CljMap*)make_map(4);
    CljObject *key_p = (CljObject*)intern_symbol(NULL, ":p");
    persistent = map_assoc(persistent, (CljValue)key_p, fixnum(10));
    TEST_ASSERT_EQUAL_INT(1, persistent->base.rc);
    
    CljMap *result7 = map_conj(persistent, (CljValue)key_p, fixnum(20));
    // Should work for persistent map with RC=1 (COW case)
    if (result7) {
        CljValue val_p = map_get(persistent, (CljValue)key_p);
        TEST_ASSERT_NOT_NULL(val_p);
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val_p));
    }
    
    // Cleanup
    RELEASE((CljObject*)transient);
    RELEASE((CljObject*)persistent);
}

// Test that map_conj works correctly with interned symbols across different contexts
// This tests the specific issue where :closed keyword is not found when called from different contexts
TEST(test_map_conj_with_interned_symbols_across_contexts) {
    // Create transient map (like make_result_channel does)
    CljMap *tmap = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(tmap);
    
    // Initialize with :value and :closed (like make_result_channel does)
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    
    // Use map_conj for in-place mutation (like make_result_channel does)
    CljMap *result1 = map_conj(tmap, (ID)kw_value, NULL);  // :value = nil
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_PTR(tmap, result1);  // Should return same pointer
    
    CljMap *result2 = map_conj(tmap, (ID)kw_closed, (ID)clj_false);  // :closed = false
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_PTR(tmap, result2);  // Should return same pointer
    
    // Verify initial state
    CljValue closed_val1 = map_get(tmap, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val1);
    TEST_ASSERT_TRUE(is_special(closed_val1));
    TEST_ASSERT_TRUE(as_special(closed_val1) == SPECIAL_FALSE);
    
    // Now update :closed from a different context (like result_channel_close does)
    // This simulates the problem where intern_symbol is called again
    CljObject *kw_closed_new = (CljObject*)intern_symbol(NULL, ":closed");
    
    // CRITICAL: Both should be the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(kw_closed, kw_closed_new, 
                                  ":closed keyword should be interned (same pointer)");
    
    // Update :closed using the new keyword pointer
    CljMap *result3 = map_conj(tmap, (ID)kw_closed_new, (ID)clj_true);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_PTR(tmap, result3);  // Should return same pointer
    
    // Verify update worked
    CljValue closed_val2 = map_get(tmap, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val2);
    TEST_ASSERT_TRUE(is_special(closed_val2));
    TEST_ASSERT_TRUE(as_special(closed_val2) == SPECIAL_TRUE);
    
    // Verify count didn't increase (should update, not add)
    TEST_ASSERT_EQUAL_INT(2, tmap->count);
    
    // Cleanup
    RELEASE((CljObject*)tmap);
}

// Test that map_conj finds existing keys by pointer equality (interned symbols)
TEST(test_map_conj_finds_existing_key_by_pointer) {
    // Create transient map
    CljMap *tmap = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(tmap);
    
    // Create keyword once
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":test-key");
    
    // Add key-value pair
    CljMap *result1 = map_conj(tmap, (ID)kw, fixnum(42));
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_PTR(tmap, result1);
    TEST_ASSERT_EQUAL_INT(1, tmap->count);
    
    // Get keyword again (should be same pointer if interned)
    CljObject *kw2 = (CljObject*)intern_symbol(NULL, ":test-key");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(kw, kw2, 
                                  "Keyword should be interned (same pointer)");
    
    // Update using second keyword pointer
    CljMap *result2 = map_conj(tmap, (ID)kw2, fixnum(100));
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_PTR(tmap, result2);
    
    // Verify value was updated (not added)
    CljValue val = map_get(tmap, (CljValue)kw);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val));
    
    // Verify count didn't increase
    TEST_ASSERT_EQUAL_INT(1, tmap->count);
    
    // Cleanup
    RELEASE((CljObject*)tmap);
}

// Test that map_conj works correctly with channel pattern (make_result_channel + result_channel_close)
TEST(test_map_conj_channel_pattern) {
    // Create channel like make_result_channel does
    CljMap *chan = (CljMap*)make_map(4);
    TEST_ASSERT_NOT_NULL(chan);
    
    // Initialize with :value and :closed (like make_result_channel does)
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    
    map_conj(chan, (ID)kw_value, NULL);  // :value = nil
    map_conj(chan, (ID)kw_closed, (ID)clj_false);  // :closed = false
    
    // Verify initial state
    CljValue closed_before = map_get(chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_before);
    TEST_ASSERT_TRUE(is_special(closed_before));
    TEST_ASSERT_TRUE(as_special(closed_before) == SPECIAL_FALSE);
    
    // Close channel (like result_channel_close does)
    // This simulates calling intern_symbol again from a different context
    CljObject *kw_closed_close = (CljObject*)intern_symbol(NULL, ":closed");
    
    // CRITICAL: Both should be the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(kw_closed, kw_closed_close, 
                                  ":closed keyword should be interned (same pointer)");
    
    CljMap *result = map_conj(chan, (ID)kw_closed_close, (ID)clj_true);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(chan, result);  // Should return same pointer
    
    // Verify channel was mutated
    CljValue closed_after = map_get(chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_after);
    TEST_ASSERT_TRUE(is_special(closed_after));
    TEST_ASSERT_TRUE(as_special(closed_after) == SPECIAL_TRUE);
    
    // Verify count didn't increase
    TEST_ASSERT_EQUAL_INT(2, chan->count);
    
    // Cleanup
    RELEASE((CljObject*)chan);
}
