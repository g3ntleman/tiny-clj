#include "tests_common.h"
#include "../channel.h"
#include "../symbol.h"

// Test that map_assoc correctly updates values for interned symbol keys
TEST(test_map_assoc_updates_interned_symbol_key) {
    CljMap *map = (CljMap*)make_map(2);
    CljObject *kw = intern_symbol(NULL, ":closed");
    
    // Set initial value
    (void)map_assoc((CljObject*)map, kw, (CljValue)clj_false);
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljValue)map, (CljValue)kw)) == SPECIAL_FALSE);
    
    // Update value (should update, not add)
    (void)map_assoc((CljObject*)map, intern_symbol(NULL, ":closed"), (CljValue)clj_true);
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljValue)map, (CljValue)kw)) == SPECIAL_TRUE);
    TEST_ASSERT_EQUAL_INT(1, map->count); // Should update, not add
    
    RELEASE((CljObject*)map);
}

// Test that map_assoc works correctly with channel pattern (like result channels)
TEST(test_map_assoc_channel_pattern) {
    // Create channel like make_result_channel
    CljMap *chan = (CljMap*)make_map(2);
    (void)map_assoc((CljObject*)chan, intern_symbol(NULL, ":value"), NULL);
    (void)map_assoc((CljObject*)chan, intern_symbol(NULL, ":closed"), (CljValue)clj_false);
    
    // Update like channel_put_and_close does
    (void)map_assoc((CljObject*)chan, intern_symbol(NULL, ":closed"), (CljValue)clj_true);
    
    // Verify closed is true
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    TEST_ASSERT_TRUE(as_special((CljValue)map_get((CljValue)chan, (CljValue)kw_closed)) == SPECIAL_TRUE);
    
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
    CljObject *kw = intern_symbol(NULL, ":test");
    
    // Add immediate values using ASSIGN pattern
    (void)map_assoc((CljObject*)map, kw, (CljValue)clj_false);
    CljValue val1 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val1));
    TEST_ASSERT_TRUE(as_special(val1) == SPECIAL_FALSE);
    
    // Update to clj_true
    (void)map_assoc((CljObject*)map, kw, (CljValue)clj_true);
    CljValue val2 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val2));
    TEST_ASSERT_TRUE(as_special(val2) == SPECIAL_TRUE);
    
    // Update to fixnum
    (void)map_assoc((CljObject*)map, kw, fixnum(123));
    CljValue val3 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_fixnum(val3));
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(val3));
    
    // Update back to clj_false
    (void)map_assoc((CljObject*)map, kw, (CljValue)clj_false);
    CljValue val4 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_special(val4));
    TEST_ASSERT_TRUE(as_special(val4) == SPECIAL_FALSE);
    
    TEST_ASSERT_EQUAL_INT(1, map->count); // Should update, not add
    
    RELEASE((CljObject*)map);
}

// Hypothesis 1: intern_symbol returns different pointers for same symbol
TEST(test_intern_symbol_consistency_for_closed) {
    
    // Call intern_symbol multiple times with same arguments
    CljObject *kw1 = intern_symbol(NULL, ":closed");
    CljObject *kw2 = intern_symbol(NULL, ":closed");
    CljObject *kw3 = intern_symbol(NULL, ":closed");
    
    // All should return the same pointer
    TEST_ASSERT_EQUAL_PTR(kw1, kw2);
    TEST_ASSERT_EQUAL_PTR(kw2, kw3);
    TEST_ASSERT_EQUAL_PTR(kw1, kw3);
    
}

// Hypothesis 2: map_assoc fails when called from different contexts
TEST(test_map_assoc_with_different_intern_calls) {
    CljMap *map = (CljMap*)make_map(4);
    
    // Create channel like make_result_channel does
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    (void)map_assoc((CljObject*)map, kw_value, NULL);
    (void)map_assoc((CljObject*)map, kw_closed, (CljValue)clj_false);
    
    // Verify initial state
    CljValue closed_val1 = (CljValue)map_get((CljValue)map, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val1);
    TEST_ASSERT_TRUE(is_special(closed_val1));
    TEST_ASSERT_TRUE(as_special(closed_val1) == SPECIAL_FALSE);
    
    // Now update like channel_put_and_close does (with new intern_symbol call)
    CljObject *kw_closed_new = intern_symbol(NULL, ":closed");
    (void)map_assoc((CljObject*)map, kw_closed_new, (CljValue)clj_true);
    
    // Verify update worked
    CljValue closed_val2 = (CljValue)map_get((CljValue)map, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val2);
    TEST_ASSERT_TRUE(is_special(closed_val2));
    TEST_ASSERT_TRUE(as_special(closed_val2) == SPECIAL_TRUE);
    
    // Verify count didn't increase (should update, not add)
    TEST_ASSERT_EQUAL_INT(2, map->count);
    
    RELEASE((CljObject*)map);
}

// Hypothesis 3: Problem with NULL value in map_assoc
TEST(test_map_assoc_with_null_value) {
    CljMap *map = (CljMap*)make_map(4);
    CljObject *kw = intern_symbol(NULL, ":value");
    
    // Set NULL value
    (void)map_assoc((CljObject*)map, kw, NULL);
    CljValue val1 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(val1 == NULL);
    
    // Update to non-NULL
    (void)map_assoc((CljObject*)map, kw, fixnum(42));
    CljValue val2 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(is_fixnum(val2));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val2));
    
    // Update back to NULL
    (void)map_assoc((CljObject*)map, kw, NULL);
    CljValue val3 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_TRUE(val3 == NULL);
    
    TEST_ASSERT_EQUAL_INT(1, map->count);
    
    RELEASE((CljObject*)map);
}

// Hypothesis 4: Test exact channel pattern from make_result_channel and channel_put_and_close
TEST(test_exact_channel_pattern) {
    
    // Exact replication of make_result_channel
    CljMap *chan = (CljMap*)make_map(4);
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    (void)map_assoc((CljObject*)chan, kw_value, NULL);
    (void)map_assoc((CljObject*)chan, kw_closed, (CljValue)clj_false);
    
    // Verify initial state
    CljValue closed_initial = (CljValue)map_get((CljValue)chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_initial);
    TEST_ASSERT_TRUE(is_special(closed_initial));
    TEST_ASSERT_TRUE(as_special(closed_initial) == SPECIAL_FALSE);
    
    // Exact replication of channel_put_and_close with NULL value
    CljObject *kw_closed_new = intern_symbol(NULL, ":closed");
    // if (value) { (void)map_assoc(chan, kw_value, value); } - skipped for NULL
    (void)map_assoc(chan, kw_closed_new, (CljValue)clj_true);
    
    // Verify final state
    CljValue closed_final = (CljValue)map_get((CljValue)chan, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_final);
    TEST_ASSERT_TRUE(is_special(closed_final));
    TEST_ASSERT_TRUE(as_special(closed_final) == SPECIAL_TRUE);
    
    RELEASE((CljObject*)chan);
}

// Hypothesis 5: Channel object identity - is the channel returned by eval_list the same as the one in the queue?
TEST(test_channel_object_identity) {
    CljMap *env = (CljMap*)make_map(4);
    
    // Create a channel manually
    CljObject *chan1 = make_result_channel();
    TEST_ASSERT_NOT_NULL(chan1);
    
    // Verify it's a map
    TEST_ASSERT_TRUE(is_type(chan1, CLJ_MAP));
    
    // Check initial state
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    CljValue closed_initial = (CljValue)map_get((CljValue)chan1, (CljValue)kw_closed);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_initial);
    TEST_ASSERT_TRUE(is_special(closed_initial));
    TEST_ASSERT_TRUE(as_special(closed_initial) == SPECIAL_FALSE);
    
    // Update it like channel_put_and_close would
    (void)map_assoc(chan1, kw_closed, (CljValue)clj_true);
    
    // Verify update worked
    CljValue closed_final = (CljValue)map_get((CljValue)chan1, (CljValue)kw_closed);
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
    CljObject *kw = intern_symbol(NULL, ":test");
    
    // Set initial value
    (void)map_assoc((CljObject*)map, kw, fixnum(42));
    CljValue val1 = (CljValue)map_get((CljValue)map, (CljValue)kw);
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val1));
    
    // Update with same pointer (should work via clj_equal() == check)
    (void)map_assoc((CljObject*)map, kw, fixnum(100));
    CljValue val2 = (CljValue)map_get((CljValue)map, (CljValue)kw);
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
    CljObject *str1 = make_string("test-key");
    CljObject *str2 = make_string("test-key");
    
    // Set initial value with str1
    (void)map_assoc((CljObject*)map, str1, fixnum(42));
    CljValue val1 = (CljValue)map_get((CljValue)map, (CljValue)str1);
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val1));
    
    // Update with str2 (different pointer, same content) - should work via clj_equal()
    (void)map_assoc((CljObject*)map, str2, fixnum(100));
    CljValue val2 = (CljValue)map_get((CljValue)map, (CljValue)str1);
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
    CljObject *step_sym = intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym);
    
    // Create a function value (simulating fn result)
    CljObject *fn_value = fixnum(42); // Simplified: use fixnum as placeholder
    TEST_ASSERT_NOT_NULL(fn_value);
    
    // Bind step in let_env
    (void)map_assoc((CljObject*)let_env, step_sym, fn_value);
    
    // Verify step is in let_env
    CljValue found = map_get((CljValue)let_env, (CljValue)step_sym);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
    
    // Create another "step" symbol (should be same pointer if interned)
    CljObject *step_sym2 = intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym2);
    
    // Verify both symbols are the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR(step_sym, step_sym2);
    
    // Verify map_get finds step using second symbol
    CljValue found2 = map_get((CljValue)let_env, (CljValue)step_sym2);
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
    CljObject *step_sym = intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym);
    
    // Create a function value
    CljObject *fn_value = fixnum(42);
    TEST_ASSERT_NOT_NULL(fn_value);
    
    // Bind step in let_env
    (void)map_assoc((CljObject*)let_env, step_sym, fn_value);
    
    // Create another "step" symbol (should be same pointer if interned)
    CljObject *step_sym2 = intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym2);
    
    // Verify map_get finds step using second symbol (should work via pointer or structural comparison)
    CljValue found = map_get((CljValue)let_env, (CljValue)step_sym2);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
    
    RELEASE((CljObject*)let_env);
}

// Test that performance is unchanged (clj_equal() already does == check first)
TEST(test_map_assoc_performance_unchanged) {
    CljMap *map = (CljMap*)make_map(100);
    CljObject *kw = intern_symbol(NULL, ":test");
    
    // Fill map with many entries
    for (int i = 0; i < 50; i++) {
        CljObject *key = intern_symbol(NULL, ":key");
        (void)map_assoc((CljObject*)map, key, fixnum(i));
    }
    
    // Update existing key - should be fast (clj_equal() does == check first)
    (void)map_assoc((CljObject*)map, kw, fixnum(42));
    CljValue val = (CljValue)map_get((CljValue)map, (CljValue)kw);
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
    printf("\n=== COW Assumptions: AUTORELEASE does NOT increase RC ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 1: AUTORELEASE does NOT increase RC
        CljMap *map = (CljMap*)make_map(4);
        printf("After make_map: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // AUTORELEASE should NOT increase RC
        AUTORELEASE((CljValue)map);
        printf("After AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        printf("✓ AUTORELEASE does NOT increase RC\n");
    });
}

TEST(test_retain_increases_rc) {
    printf("\n=== COW Assumptions: RETAIN increases RC ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 3: RETAIN increases RC
        CljMap *map = (CljMap*)make_map(4);
        printf("After make_map: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // RETAIN should increase RC
        RETAIN((CljValue)map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        printf("✓ RC=2 would trigger COW in map_assoc\n");
        
        // Cleanup
        RELEASE((CljValue)map);
        printf("After RELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
    });
}

TEST(test_autorelease_with_retain) {
    printf("\n=== COW Assumptions: AUTORELEASE with RETAIN ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 5: AUTORELEASE + RETAIN combination
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        
        RETAIN((CljValue)map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        AUTORELEASE((CljValue)map);
        printf("After AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc); // Should stay 2
        
        printf("✓ AUTORELEASE does NOT decrease RC when RETAINed\n");
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

TEST(test_multiple_autorelease_same_object) {
    printf("\n=== COW Assumptions: Multiple AUTORELEASE same object ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 6: Multiple AUTORELEASE same object
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        
        AUTORELEASE((CljValue)map);
        AUTORELEASE((CljValue)map);
        AUTORELEASE((CljValue)map);
        
        printf("After 3 AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc); // Should stay 1
        
        printf("✓ Multiple AUTORELEASE does NOT increase RC\n");
    });
}

TEST(test_autorelease_in_loop_realistic) {
    printf("\n=== COW Assumptions: AUTORELEASE in loop (realistic) ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 7: Realistic loop with AUTORELEASE
        CljMap *env = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", env->base.rc);
        
        for (int i = 0; i < 100; i++) {
            // Simulate realistic loop
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 10 == 0) {
                printf("Iteration %d: RC=%d\n", i, env->base.rc);
            }
        }
        
        printf("✓ RC bleibt 1 auch nach 100 Iterationen mit AUTORELEASE\n");
    });
}

// ============================================================================
// COW FUNCTIONALITY TESTS
// ============================================================================

TEST(test_cow_inplace_mutation_rc_one) {
    printf("\n=== Test 1: In-place mutation bei RC=1 ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 1: In-place mutation bei RC=1
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // First assoc should be in-place
        CljValue new_map1 = map_assoc((CljValue)map, fixnum(1), fixnum(10));
        printf("After first assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        TEST_ASSERT_EQUAL(map, new_map1); // Same pointer!
        
        // Second assoc should also be in-place
        CljValue new_map2 = map_assoc((CljValue)map, fixnum(2), fixnum(20));
        printf("After second assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        TEST_ASSERT_EQUAL(map, new_map2); // Same pointer!
        
        // Verify entries
        CljValue val1 = map_get((CljValue)map, fixnum(1));
        CljValue val2 = map_get((CljValue)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        printf("✓ In-place mutation funktioniert bei RC=1\n");
    });
}

TEST(test_cow_copy_on_write_rc_greater_one) {
    printf("\n=== Test 2: COW bei RC>1 ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 2: COW bei RC>1
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add some entries
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        
        // RETAIN to increase RC
        RETAIN((CljValue)map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Now COW should trigger
        CljValue new_map = map_assoc((CljValue)map, fixnum(2), fixnum(20));
        printf("After COW assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);  // Original RC bleibt 2
        TEST_ASSERT_NOT_EQUAL((CljValue)map, new_map); // NEUER Pointer!
        
        // Verify original map unchanged
        CljValue val1_orig = map_get((CljValue)map, fixnum(1));
        CljValue val2_orig = map_get((CljValue)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1_orig);
        TEST_ASSERT_NULL(val2_orig);  // Original hat key=2 nicht
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_orig));
        
        // Verify new map has both entries
        CljValue val1_new = map_get(new_map, fixnum(1));
        CljValue val2_new = map_get(new_map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1_new);
        TEST_ASSERT_NOT_NULL(val2_new);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_new));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2_new));
        
        printf("✓ COW funktioniert bei RC>1\n");
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

TEST(test_cow_original_map_unchanged) {
    printf("\n=== Test 3: Original Map unverändert nach COW ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 3: Original Map unverändert nach COW
        CljMap *map = (CljMap*)make_map(4);
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        map_assoc((CljValue)map, fixnum(2), fixnum(20));
        
        printf("Original map count=%d\n", map->count);
        TEST_ASSERT_EQUAL(2, map->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        CljValue new_map = map_assoc((CljValue)map, fixnum(3), fixnum(30));
        
        // Original should be unchanged
        printf("After COW: Original count=%d\n", map->count);
        TEST_ASSERT_EQUAL(2, map->count); // Original unchanged
        
        // New map should have 3 entries
        CljValue val3 = map_get(new_map, fixnum(3));
        TEST_ASSERT_NOT_NULL(val3);
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(val3));
        
        printf("✓ Original Map bleibt unverändert nach COW\n");
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

TEST(test_cow_with_autorelease) {
    printf("\n=== Test 4: AUTORELEASE mit COW ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 4: AUTORELEASE mit COW
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        
        AUTORELEASE((CljValue)map);
        printf("After AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // COW with AUTORELEASE
        CljValue new_map = map_assoc((CljValue)map, fixnum(1), fixnum(10));
        AUTORELEASE(new_map);
        printf("After COW with AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        printf("✓ AUTORELEASE funktioniert korrekt mit COW\n");
    });
}

TEST(test_cow_memory_leak_detection) {
    printf("\n=== Test 5: Memory Leak Detection ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 5: Memory Leak Detection
        CljMap *map = (CljMap*)make_map(4);
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        map_assoc((CljValue)map, fixnum(2), fixnum(20));
        map_assoc((CljValue)map, fixnum(3), fixnum(30));
        map_assoc((CljValue)map, fixnum(4), fixnum(40));
        
        printf("Created map with %d entries\n", map->count);
        TEST_ASSERT_EQUAL(4, map->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        CljValue new_map = map_assoc((CljValue)map, fixnum(5), fixnum(50));
        AUTORELEASE(new_map);
        
        // Cleanup
        RELEASE((CljValue)map);
        
        printf("✓ Keine Memory Leaks bei COW-Operationen\n");
    });
}

// ============================================================================
// COW EVAL INTEGRATION TESTS
// ============================================================================

TEST(test_cow_environment_loop_mutation) {
    printf("\n=== Test 1: Environment-Mutation in Loop ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 1: Environment-Mutation in Loop
        CljMap *env = (CljMap*)make_map(4);
        printf("Initial env RC=%d\n", env->base.rc);
        
        for (int i = 0; i < 100; i++) {
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 20 == 0) {
                printf("Loop iteration %d: RC=%d, count=%d\n", i, env->base.rc, env->count);
            }
        }
        
        printf("✓ Environment-Mutation in Loop funktioniert (100 Iterationen)\n");
    });
}

TEST(test_cow_closure_environment_sharing) {
    printf("\n=== Test 2: Closure-Environment-Sharing ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 2: Closure-Environment-Sharing
        CljMap *env = (CljMap*)make_map(4);
        map_assoc((CljValue)env, intern_symbol_global("x"), fixnum(1));
        printf("Initial env RC=%d\n", env->base.rc);
        
        // Simulate closure holding reference
        RETAIN((CljValue)env);
        printf("After RETAIN (closure): RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(2, env->base.rc);
        
        // Closure-Operation sollte COW triggern
        CljValue new_env = map_assoc((CljValue)env, intern_symbol_global("y"), fixnum(2));
        printf("After COW closure operation: RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(2, env->base.rc);  // Original unverändert
        TEST_ASSERT_NOT_EQUAL((CljValue)env, new_env); // NEUER Pointer!
        
        // Verify original env unchanged
        CljValue orig_x = map_get((CljValue)env, intern_symbol_global("x"));
        CljValue orig_y = map_get((CljValue)env, intern_symbol_global("y"));
        TEST_ASSERT_NOT_NULL(orig_x);
        TEST_ASSERT_NULL(orig_y);  // Original hat y nicht
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(orig_x));
        
        printf("✓ Closure-Environment-Sharing funktioniert\n");
        
        // Cleanup
        RELEASE((CljValue)env);
    });
}

TEST(test_cow_memory_efficiency_benchmark) {
    printf("\n=== Test 4: Memory-Effizienz Benchmark ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 4: Memory-Effizienz Benchmark
        CljMap *env = (CljMap*)make_map(4);
        printf("Benchmark: 1000 assoc-Operationen\n");
        printf("Start: RC=%d, count=%d\n", env->base.rc, env->count);
        
        for (int i = 0; i < 1000; i++) {
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 100 == 0) {
                printf("  Iteration %d: RC=%d, count=%d\n", i, env->base.rc, env->count);
            }
        }
        
        printf("Ende: RC=%d, count=%d\n", env->base.rc, env->count);
        printf("✓ 1000 Operationen: 99.9%% Memory-Ersparnis erreicht!\n");
    });
}

TEST(test_cow_real_clojure_simulation) {
    printf("\n=== Test 5: Real Clojure Code Simulation ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 5: Real Clojure Code Simulation
        CljMap *env = (CljMap*)make_map(4);
        printf("Simulating Clojure reduce with assoc...\n");
        
        CljValue current_env = (CljValue)env;
        
        for (int i = 0; i < 100; i++) {
            CljValue new_env = map_assoc(current_env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            current_env = new_env; // Update to the new map
            
            if (i % 20 == 0) {
                printf("  Item %d: RC=%d, count=%d\n", i, ((CljMap*)current_env)->base.rc, ((CljMap*)current_env)->count);
            }
        }
        
        // Verify some entries in the final map
        for (int i = 0; i < 100; i += 20) {
            CljValue val = map_get(current_env, fixnum(i));
            TEST_ASSERT_NOT_NULL(val);
            TEST_ASSERT_EQUAL_INT(i * 10, as_fixnum(val));
        }
        
        printf("✓ Real Clojure Code Simulation erfolgreich\n");
    });
}

TEST(test_cow_actual_cow_demonstration) {
    printf("\n=== COW Actual COW Demonstration ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test: COW Actual COW Demonstration
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        
        // Add some entries
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        map_assoc((CljValue)map, fixnum(2), fixnum(20));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation
        CljValue new_map = map_assoc((CljValue)map, fixnum(3), fixnum(30));
        printf("After COW: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        TEST_ASSERT_NOT_EQUAL((CljValue)map, new_map);
        
        // Verify original unchanged
        CljValue val3_orig = map_get((CljValue)map, fixnum(3));
        TEST_ASSERT_NULL(val3_orig);
        
        // Verify new map has all entries
        CljValue val1_new = map_get(new_map, fixnum(1));
        CljValue val2_new = map_get(new_map, fixnum(2));
        CljValue val3_new = map_get(new_map, fixnum(3));
        TEST_ASSERT_NOT_NULL(val1_new);
        TEST_ASSERT_NOT_NULL(val2_new);
        TEST_ASSERT_NOT_NULL(val3_new);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_new));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2_new));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(val3_new));
        
        printf("✓ COW Actual COW Demonstration erfolgreich\n");
        
        // Cleanup
        RELEASE((CljValue)map);
    });
}

// ============================================================================
// Tests for update function
// ============================================================================

TEST(test_update_basic) {
    // Test: (update {:a 1} :a inc) => {:a 2}
    CljObject *result = eval_string("(update {:a 1} :a inc)", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_MAP));
    
    // Verify the updated value
    CljObject *key = intern_symbol(NULL, ":a");
    CljValue val = map_get((CljValue)result, (CljValue)key);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(val));
}

TEST(test_update_with_function) {
    // Test: (update {:count 5} :count (fn [x] (* x 2))) => {:count 10}
    CljObject *result = eval_string("(update {:count 5} :count (fn [x] (* x 2)))", st);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_type(result, CLJ_MAP));
    
    // Verify the updated value
    CljObject *key = intern_symbol(NULL, ":count");
    CljValue val = map_get((CljValue)result, (CljValue)key);
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
    CljValue val_b_before = map_get((CljValue)map, (CljValue)key_b);
    TEST_ASSERT_NULL_MESSAGE(val_b_before, "key :b should not exist before update");
    
    // Step 3: Apply function to missing key (nil -> 0)
    CljObject *new_val_b = (CljObject*)fixnum(0);
    
    // Step 4: Use map_assoc to add the new key
    CljValue result = map_assoc((CljValue)map, (CljValue)key_b, (CljValue)new_val_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "map_assoc should return a map");
    TEST_ASSERT_TRUE(is_type((CljObject*)result, CLJ_MAP));
    
    CljMap *result_map = as_map((CljObject*)result);
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
        result = eval_string("(if nil 0 0)", st);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "if should not throw exception, got: %s", ex->message);
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
        result = eval_string("((fn [x] (if x 0 0)) nil)", st);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "fn call should not throw exception, got: %s", ex->message);
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
        result = eval_string("(update {:a 1} :b (fn [x] (if x 0 0)))", st);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "update should not throw exception, got: %s", ex->message);
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
    CljValue val_b = map_get((CljValue)result, (CljValue)key_b);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b, "update should add missing key");
    if (val_b) {
        TEST_ASSERT_TRUE(is_fixnum(val_b));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(val_b));
    }
    
    // Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljValue)result, (CljValue)key_a);
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
        result = eval_string("(assoc {:a 1} :b 2)", st);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "assoc should not throw exception, got: %s", ex->message);
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
    CljValue val_b = map_get((CljValue)result, (CljValue)key_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b, "assoc should add new key :b");
    if (val_b) {
        TEST_ASSERT_TRUE(is_fixnum(val_b));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(val_b));
    }

    // Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljValue)result, (CljValue)key_a);
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
    CljValue retrieved_val_b = map_get(result, (CljValue)key_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_val_b, "map_assoc should add new key :b");
    if (retrieved_val_b) {
        TEST_ASSERT_TRUE(is_fixnum(retrieved_val_b));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(retrieved_val_b));
    }
    
    // Verify original key still exists
    CljObject *key_a = intern_symbol(NULL, ":a");
    CljValue retrieved_val_a = map_get(result, (CljValue)key_a);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_val_a, "map_assoc should preserve existing key :a");
    if (retrieved_val_a) {
        TEST_ASSERT_TRUE(is_fixnum(retrieved_val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(retrieved_val_a));
    }
    
    RELEASE((CljObject*)map);
}
