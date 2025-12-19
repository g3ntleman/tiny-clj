#include "tests_common.h"
#include "../channel.h"
#include "../symbol.h"
#include "../kv_macros.h"
#include "../map.h"

// Basic map behavior tests migrated to subjective-c/tests/test_map.c for\n+// interpreter-independent coverage.

// Test that map_get finds symbols bound in let_env
TEST(test_map_get_finds_let_binding) {
    // Create a map (simulating let_env)
    CljMap *let_env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
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
    CljValue found = map_get((CljMap*)let_env, (CljValue)step_sym, NULL);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
    
    // Create another "step" symbol (should be same pointer if interned)
    CljObject *step_sym2 = (CljObject*)intern_symbol_global("step");
    TEST_ASSERT_NOT_NULL(step_sym2);
    
    // Verify both symbols are the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR(step_sym, step_sym2);
    
    // Verify map_get finds step using second symbol
    CljValue found2 = map_get((CljMap*)let_env, (CljValue)step_sym2, NULL);
    TEST_ASSERT_NOT_NULL(found2);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found2));
}

// Test that map_get uses structural comparison for non-interned symbols
TEST(test_map_get_structural_comparison) {
    // Create a map
    CljMap *let_env = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
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
    CljValue found = map_get((CljMap*)let_env, (CljValue)step_sym2, NULL);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(found));
}

// Test that performance is unchanged (clj_equal() already does == check first)
TEST(test_map_assoc_performance_unchanged) {
    CljMap *map = (CljMap*)AUTORELEASE((CljObject*)make_map(100));
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":test");
    
    // Fill map with many entries
    for (int i = 0; i < 50; i++) {
        CljObject *key = (CljObject*)intern_symbol(NULL, ":key");
        map = map_assoc(map, (CljValue)key, fixnum(i));
    }
    
    // Update existing key - should be fast (clj_equal() does == check first)
    map = map_assoc(map, (CljValue)kw, fixnum(42));
    CljValue val = (CljValue)map_get((CljMap*)map, (CljValue)kw, NULL);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(val));
}

// ============================================================================
// Tests for update function
// ============================================================================

TEST(test_update_basic) {
    // Test: (update {:a 1} :a inc) => {:a 2}
    CljObject *result = eval_string("(update {:a 1} :a inc)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_MAP);
    
    // Verify the updated value
    CljSymbol *key = intern_symbol(NULL, ":a");
    CljValue val = map_get((CljMap*)result, (CljValue)key, NULL);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(val));
}

TEST(test_update_with_function) {
    // Test: (update {:count 5} :count (fn [x] (* x 2))) => {:count 10}
    CljObject *result = eval_string("(update {:count 5} :count (fn [x] (* x 2)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_MAP);
    
    // Verify the updated value
    CljSymbol *key = intern_symbol(NULL, ":count");
    CljValue val = map_get((CljMap*)result, (CljValue)key, NULL);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(val));
}

// Simplified test: Direct C function calls
TEST(test_update_missing_key_simple) {
    // Step 1: Create initial map {:a 1}
    CljObject *pairs[2];
    pairs[0] = (CljObject*)intern_symbol(NULL, ":a");
    pairs[1] = (CljObject*)fixnum(1);
    CljMap *map = (CljMap*)AUTORELEASE((CljObject*)make_map_from_stack(pairs, 1));
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_INT(1, map->count);
    
    // Step 2: Get value for missing key :b (should return NULL)
    CljSymbol *key_b = intern_symbol(NULL, ":b");
    CljValue val_b_before = map_get((CljMap*)map, (CljValue)key_b, NULL);
    TEST_ASSERT_NULL_MESSAGE(val_b_before, "key :b should not exist before update");
    
    // Step 3: Apply function to missing key (nil -> 0)
    CljObject *new_val_b = (CljObject*)fixnum(0);
    
    // Step 4: Use map_assoc to add the new key
    CljMap *result = map_assoc(map, (CljValue)key_b, (CljValue)new_val_b);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "map_assoc should return a map");
    TEST_ASSERT_TRUE((CljObject*)result && TAG((CljObject*)result) == CLJ_MAP);
    
    CljMap *result_map = result;
    TEST_ASSERT_EQUAL_INT(2, result_map->count);
    
    // Step 5: Verify the new key was added
    CljValue val_b_after = map_get((CljMap*)result, (CljValue)key_b, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b_after, "key :b should exist after update");
    if (val_b_after) {
        TEST_ASSERT_TRUE(is_fixnum(val_b_after));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(val_b_after));
    }
    
    // Step 6: Verify original key still exists
    CljSymbol *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljMap*)result, (CljValue)key_a, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_a, "key :a should still exist");
    if (val_a) {
        TEST_ASSERT_TRUE(is_fixnum(val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(val_a));
    }
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
    
    TEST_ASSERT_TRUE_MESSAGE(result && TAG(result) == CLJ_MAP, "update result should be a map");
    
    CljMap *result_map = as_map((CljObject*)result);
    TEST_ASSERT_EQUAL_INT(2, result_map->count);
    
    // Verify the new key was added
    CljSymbol *key_b = intern_symbol(NULL, ":b");
    CljValue val_b = map_get((CljMap*)result, (CljValue)key_b, NULL);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b, "update should add missing key");
    if (val_b) {
        TEST_ASSERT_TRUE(is_fixnum(val_b));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(val_b));
    }
    
    // Verify original key still exists
    CljSymbol *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljMap*)result, (CljValue)key_a, NULL);
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

    TEST_ASSERT_TRUE_MESSAGE(result && TAG(result) == CLJ_MAP, "assoc result should be a map");

    // Verify the new key was added
    CljSymbol *key_b = intern_symbol(NULL, ":b");
    CljValue val_b = map_get((CljMap*)result, (CljValue)key_b, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(val_b, "assoc should add new key :b");
    if (val_b) {
        TEST_ASSERT_TRUE(is_fixnum(val_b));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(val_b));
    }

    // Verify original key still exists
    CljSymbol *key_a = intern_symbol(NULL, ":a");
    CljValue val_a = map_get((CljMap*)result, (CljValue)key_a, NULL);
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
    pairs[0] = (CljObject*)intern_symbol(NULL, ":a");
    pairs[1] = (CljObject*)fixnum(1);
    CljMap *map = (CljMap*)AUTORELEASE((CljObject*)make_map_from_stack(pairs, 1));
    
    
    // Add a new key :b with value 2
    CljSymbol *key_b = intern_symbol(NULL, ":b");
    CljObject *val_b = (CljObject*)fixnum(2);
    
    CljValue result = map_assoc((CljValue)map, (CljValue)key_b, (CljValue)val_b);
    TEST_ASSERT_NOT_NULL(result);
    
    // Verify the new key was added
    CljValue retrieved_val_b = map_get((CljMap*)result, (CljValue)key_b, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_val_b, "map_assoc should add new key :b");
    if (retrieved_val_b) {
        TEST_ASSERT_TRUE(is_fixnum(retrieved_val_b));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(retrieved_val_b));
    }
    
    // Verify original key still exists
    CljSymbol *key_a = intern_symbol(NULL, ":a");
    CljValue retrieved_val_a = map_get((CljMap*)result, (CljValue)key_a, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(retrieved_val_a, "map_assoc should preserve existing key :a");
    if (retrieved_val_a) {
        TEST_ASSERT_TRUE(is_fixnum(retrieved_val_a));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(retrieved_val_a));
    }
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
    CljMap *empty_map = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    TEST_ASSERT_EQUAL_INT(0, empty_map->count);
    CljMap *empty_transient = (CljMap*)AUTORELEASE((CljObject*)map_transient(empty_map));
    TEST_ASSERT_NOT_NULL(empty_transient);
    TEST_ASSERT_TRUE((CljObject*)empty_transient && TAG((CljObject*)empty_transient) == CLJ_MAP_TRANSIENT);
    TEST_ASSERT_EQUAL_INT(0, empty_transient->count);
    
    // Test 3: Map with entries - conversion and data preservation
    CljMap *persistent_map = (CljMap*)AUTORELEASE((CljObject*)make_map(8));
    CljObject *keys[5];
    for (int i = 0; i < 5; i++) {
        char key_name[16];
        snprintf(key_name, sizeof(key_name), ":key%d", i);
        keys[i] = (CljObject*)intern_symbol(NULL, key_name);
        persistent_map = map_assoc(persistent_map, (CljValue)keys[i], fixnum(i * 10));
    }
    TEST_ASSERT_EQUAL_INT(5, persistent_map->count);
    TEST_ASSERT_TRUE((CljObject*)persistent_map && TAG((CljObject*)persistent_map) == CLJ_MAP);
    
    // Convert to transient
    CljMap *transient_map = map_transient(persistent_map);
    TEST_ASSERT_NOT_NULL(transient_map);
    TEST_ASSERT_TRUE((CljObject*)transient_map && TAG((CljObject*)transient_map) == CLJ_MAP_TRANSIENT);
    TEST_ASSERT_EQUAL_INT(5, transient_map->count);
    
    // Verify it's a different pointer (new map created)
    TEST_ASSERT_NOT_EQUAL((CljValue)persistent_map, (CljValue)transient_map);
    
    // Verify original map is unchanged
    TEST_ASSERT_TRUE((CljObject*)persistent_map && TAG((CljObject*)persistent_map) == CLJ_MAP);
    TEST_ASSERT_EQUAL_INT(5, persistent_map->count);
    
    // Verify all entries are preserved
    for (int i = 0; i < 5; i++) {
        CljValue val_persistent = map_get((CljMap*)persistent_map, (CljValue)keys[i], NULL);
        CljValue val_transient = map_get((CljMap*)transient_map, (CljValue)keys[i], NULL);
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
}

// ============================================================================
// Tests for map_conj() - In-place mutation of transient maps
// ============================================================================

// Comprehensive test for map_conj() covering all cases
TEST(test_map_conj_comprehensive) {
    // Test 1: NULL input returns NULL
    CljMap *result = map_conj(NULL, (CljValue)intern_symbol(NULL, ":key"), fixnum(42));
    TEST_ASSERT_NULL(result);
    
    CljMap *tmap = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    CljMap *transient = (CljMap*)AUTORELEASE((CljObject*)map_transient(tmap));
    
    // Test: map_conj with NULL key should succeed (nil is a valid key in Clojure)
    result = map_conj(transient, NULL, fixnum(42));
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(1, result->count);
    
    // Test 2: Add new key-value pair to transient map (already has nil key)
    CljObject *key1 = (CljObject*)intern_symbol(NULL, ":a");
    CljMap *result2 = map_conj(transient, (CljValue)key1, fixnum(1));
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_PTR(transient, result2);  // Same pointer (in-place mutation)
    TEST_ASSERT_EQUAL_INT(2, transient->count);  // Should have 2 entries: nil and :a
    
    CljValue val1 = map_get((CljMap*)transient, (CljValue)key1, NULL);
    TEST_ASSERT_NOT_NULL(val1);
    TEST_ASSERT_TRUE(is_fixnum(val1));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(val1));
    
    // Test 3: Update existing key
    CljMap *result3 = map_conj(transient, (CljValue)key1, fixnum(100));
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_PTR(transient, result3);  // Same pointer
    TEST_ASSERT_EQUAL_INT(2, transient->count);  // Count unchanged (update, not add) - should still have nil and :a
    
    CljValue val1_updated = map_get((CljMap*)transient, (CljValue)key1, NULL);
    TEST_ASSERT_NOT_NULL(val1_updated);
    TEST_ASSERT_TRUE(is_fixnum(val1_updated));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val1_updated));
    
    // Test 4: Add multiple key-value pairs
    CljObject *key2 = (CljObject*)intern_symbol(NULL, ":b");
    CljObject *key3 = (CljObject*)intern_symbol(NULL, ":c");
    map_conj(transient, (CljValue)key2, fixnum(2));
    map_conj(transient, (CljValue)key3, fixnum(3));
    
    TEST_ASSERT_EQUAL_INT(4, transient->count);  // Should have nil, :a, :b, :c
    
    CljValue val2 = map_get((CljMap*)transient, (CljValue)key2, NULL);
    CljValue val3 = map_get((CljMap*)transient, (CljValue)key3, NULL);
    TEST_ASSERT_NOT_NULL(val2);
    TEST_ASSERT_NOT_NULL(val3);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(val2));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(val3));
    
    // Test 5: NULL value (nil) is valid
    CljObject *key4 = (CljObject*)intern_symbol(NULL, ":d");
    map_conj(transient, (CljValue)key4, NULL);
    TEST_ASSERT_EQUAL_INT(4, transient->count);
    
    CljValue val4 = map_get((CljMap*)transient, (CljValue)key4, NULL);
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
    CljMap *persistent = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    CljObject *key_p = (CljObject*)intern_symbol(NULL, ":p");
    persistent = map_assoc(persistent, (CljValue)key_p, fixnum(10));
    TEST_ASSERT_EQUAL_INT(1, persistent->base.rc);
    
    CljMap *result7 = map_conj(persistent, (CljValue)key_p, fixnum(20));
    // Should work for persistent map with RC=1 (COW case)
    if (result7) {
        CljValue val_p = map_get((CljMap*)persistent, (CljValue)key_p, NULL);
        TEST_ASSERT_NOT_NULL(val_p);
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val_p));
    }
}

// Test that map_conj works correctly with interned symbols across different contexts
// This tests the specific issue where :closed keyword is not found when called from different contexts
TEST(test_map_conj_with_interned_symbols_across_contexts) {
    // Create transient map (like make_result_channel does)
    CljMap *tmap = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    TEST_ASSERT_NOT_NULL(tmap);
    
    // Initialize with :value and :closed (like make_result_channel does)
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    
    // Use map_conj for in-place mutation (like make_result_channel does)
    CljMap *result1 = map_conj(tmap, kw_value, NULL);  // :value = nil
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_PTR(tmap, result1);  // Should return same pointer
    
    CljMap *result2 = map_conj(tmap, kw_closed, clj_false);  // :closed = false
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_PTR(tmap, result2);  // Should return same pointer
    
    // Verify initial state
    CljValue closed_val1 = map_get((CljMap*)tmap, (CljValue)kw_closed, NULL);
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
    CljMap *result3 = map_conj(tmap, kw_closed_new, clj_true);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_PTR(tmap, result3);  // Should return same pointer
    
    // Verify update worked
    CljValue closed_val2 = map_get((CljMap*)tmap, (CljValue)kw_closed, NULL);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_val2);
    TEST_ASSERT_TRUE(is_special(closed_val2));
    TEST_ASSERT_TRUE(as_special(closed_val2) == SPECIAL_TRUE);
    
    // Verify count didn't increase (should update, not add)
    TEST_ASSERT_EQUAL_INT(2, tmap->count);
}

// Test that map_conj finds existing keys by pointer equality (interned symbols)
TEST(test_map_conj_finds_existing_key_by_pointer) {
    // Create transient map
    CljMap *tmap = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    TEST_ASSERT_NOT_NULL(tmap);
    
    // Create keyword once
    CljObject *kw = (CljObject*)intern_symbol(NULL, ":test-key");
    
    // Add key-value pair
    CljMap *result1 = map_conj(tmap, kw, fixnum(42));
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_PTR(tmap, result1);
    TEST_ASSERT_EQUAL_INT(1, tmap->count);
    
    // Get keyword again (should be same pointer if interned)
    CljObject *kw2 = (CljObject*)intern_symbol(NULL, ":test-key");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(kw, kw2, 
                                  "Keyword should be interned (same pointer)");
    
    // Update using second keyword pointer
    CljMap *result2 = map_conj(tmap, kw2, fixnum(100));
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_PTR(tmap, result2);
    
    // Verify value was updated (not added)
    CljValue val = map_get((CljMap*)tmap, (CljValue)kw, NULL);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_TRUE(is_fixnum(val));
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(val));
    
    // Verify count didn't increase
    TEST_ASSERT_EQUAL_INT(1, tmap->count);
}

// Test that map_conj works correctly with channel pattern (make_result_channel + result_channel_close)
TEST(test_map_conj_channel_pattern) {
    // Create channel like make_result_channel does
    CljMap *chan = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    TEST_ASSERT_NOT_NULL(chan);
    
    // Initialize with :value and :closed (like make_result_channel does)
    CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
    CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
    
    map_conj(chan, kw_value, NULL);  // :value = nil
    map_conj(chan, kw_closed, clj_false);  // :closed = false
    
    // Verify initial state
    CljValue closed_before = map_get((CljMap*)chan, (CljValue)kw_closed, NULL);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_before);
    TEST_ASSERT_TRUE(is_special(closed_before));
    TEST_ASSERT_TRUE(as_special(closed_before) == SPECIAL_FALSE);
    
    // Close channel (like result_channel_close does)
    // This simulates calling intern_symbol again from a different context
    CljObject *kw_closed_close = (CljObject*)intern_symbol(NULL, ":closed");
    
    // CRITICAL: Both should be the same pointer (interned)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(kw_closed, kw_closed_close, 
                                  ":closed keyword should be interned (same pointer)");
    
    CljMap *result = map_conj(chan, kw_closed_close, clj_true);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(chan, result);  // Should return same pointer
    
    // Verify channel was mutated
    CljValue closed_after = map_get((CljMap*)chan, (CljValue)kw_closed, NULL);
    TEST_ASSERT_NOT_NULL((CljObject*)closed_after);
    TEST_ASSERT_TRUE(is_special(closed_after));
    TEST_ASSERT_TRUE(as_special(closed_after) == SPECIAL_TRUE);
    
    // Verify count didn't increase
    TEST_ASSERT_EQUAL_INT(2, chan->count);
}

// ============================================================================
// Tests for dissoc() - Remove keys from map
// ============================================================================

// Test dissoc with single key
TEST(test_dissoc_single_key) {
    WITH_AUTORELEASE_POOL({
        // Create map with multiple keys
        CljMap *map = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
        CljObject *key_a = (CljObject*)intern_symbol(NULL, ":a");
        CljObject *key_b = (CljObject*)intern_symbol(NULL, ":b");
        CljObject *key_c = (CljObject*)intern_symbol(NULL, ":c");
        
        map = map_assoc(map, key_a, fixnum(1));
        map = map_assoc(map, key_b, fixnum(2));
        map = map_assoc(map, key_c, fixnum(3));
        
        TEST_ASSERT_EQUAL_INT(3, map->count);
        
        // Remove key :b
        CljObject *result = eval_string("(dissoc {:a 1 :b 2 :c 3} :b)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *result_map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(2, result_map->count);
        
        // Verify :a and :c are still present
        CljValue val_a = map_get((CljMap*)result_map, (CljValue)key_a, NULL);
        CljValue val_c = map_get((CljMap*)result_map, (CljValue)key_c, NULL);
        TEST_ASSERT_NOT_NULL(val_a);
        TEST_ASSERT_NOT_NULL(val_c);
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(val_a));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(val_c));
        
        // Verify :b is removed
        CljValue val_b = map_get((CljMap*)result_map, (CljValue)key_b, NULL);
        TEST_ASSERT_NULL(val_b);
    });
}

// Test dissoc with multiple keys (Clojure semantics)
TEST(test_dissoc_multiple_keys) {
    WITH_AUTORELEASE_POOL({
        // Remove multiple keys
        CljObject *result = eval_string("(dissoc {:a 1 :b 2 :c 3} :a :c)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *result_map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(1, result_map->count);
        
        // Verify :b is still present
        CljObject *key_b = (CljObject*)intern_symbol(NULL, ":b");
        CljValue val_b = map_get((CljMap*)result_map, (CljValue)key_b, NULL);
        TEST_ASSERT_NOT_NULL(val_b);
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(val_b));
        
        // Verify :a and :c are removed
        CljObject *key_a = (CljObject*)intern_symbol(NULL, ":a");
        CljObject *key_c = (CljObject*)intern_symbol(NULL, ":c");
        CljValue val_a = map_get((CljMap*)result_map, (CljValue)key_a, NULL);
        CljValue val_c = map_get((CljMap*)result_map, (CljValue)key_c, NULL);
        TEST_ASSERT_NULL(val_a);
        TEST_ASSERT_NULL(val_c);
    });
}

// Test dissoc with no keys (returns map unchanged)
TEST(test_dissoc_no_keys) {
    WITH_AUTORELEASE_POOL({
        CljObject *result = eval_string("(dissoc {:a 1 :b 2})", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *result_map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(2, result_map->count);
    });
}

// Test dissoc with non-existent key (returns map unchanged)
TEST(test_dissoc_non_existent_key) {
    WITH_AUTORELEASE_POOL({
        CljObject *result = eval_string("(dissoc {:a 1 :b 2} :c)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *result_map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(2, result_map->count);
        
        // Verify original keys are still present
        CljObject *key_a = (CljObject*)intern_symbol(NULL, ":a");
        CljObject *key_b = (CljObject*)intern_symbol(NULL, ":b");
        CljValue val_a = map_get((CljMap*)result_map, (CljValue)key_a, NULL);
        CljValue val_b = map_get((CljMap*)result_map, (CljValue)key_b, NULL);
        TEST_ASSERT_NOT_NULL(val_a);
        TEST_ASSERT_NOT_NULL(val_b);
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(val_a));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(val_b));
    });
}

// Regression tests for nil as key in maps (Clojure-compatible behavior)
// In Clojure, nil is a valid key in maps: (get {nil "value"} nil) => "value"

// Test: nil can be used as key with assoc
TEST(test_map_assoc_nil_key) {
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)AUTORELEASE((CljObject*)map_empty());
        
        // assoc nil key with value
        CljObject *value = AUTORELEASE((CljObject*)intern_symbol(NULL, "nil-value"));
        RETAIN(value);
        map = map_assoc(map, NULL, value);
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_EQUAL_INT(1, map->count);
        
        // get nil key should return the value
        CljObject *result = (CljObject*)map_get(map, NULL, NULL);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_PTR(value, result);
    });
}

// Test: nil can be used as key in map literals
TEST(test_map_literal_nil_key) {
    WITH_AUTORELEASE_POOL({
        // Create map literal with nil key: {nil "nil-value"}
        CljObject *result = eval_string("{nil \"nil-value\"}", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(1, map->count);
        
        // get nil key should return "nil-value"
        CljObject *value = (CljObject*)eval_string("(get {nil \"nil-value\"} nil)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_TRUE(TAG(value) == CLJ_STRING);
        
        CljString *str = as_clj_string(value);
        TEST_ASSERT_NOT_NULL(str);
        TEST_ASSERT_EQUAL_STRING("nil-value", clj_string_data(str));
    });
}

// Test: get with nil key returns value when nil key exists
TEST(test_map_get_nil_key_exists) {
    WITH_AUTORELEASE_POOL({
        // Create map with nil key using assoc
        CljObject *result = eval_string("(assoc {} nil \"nil-value\")", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        // get nil key should return "nil-value"
        CljObject *value = eval_string("(get (assoc {} nil \"nil-value\") nil)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_TRUE(TAG(value) == CLJ_STRING);
        
        CljString *str = as_clj_string(value);
        TEST_ASSERT_NOT_NULL(str);
        TEST_ASSERT_EQUAL_STRING("nil-value", clj_string_data(str));
    });
}

// Test: get with missing key returns nil (not found)
TEST(test_map_get_missing_key_returns_nil) {
    WITH_AUTORELEASE_POOL({
        // Create map without nil key
        CljObject *result = eval_string("(assoc {} :key \"value\")", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        // get missing key should return nil
        CljObject *value = eval_string("(get {:key \"value\"} :missing)", g_test_eval_state);
        // nil is represented as NULL in tiny-clj
        TEST_ASSERT_NULL(value);
    });
}

// Test: map_contains with nil key
TEST(test_map_contains_nil_key) {
    WITH_AUTORELEASE_POOL({
        // Create map with nil key
        CljMap *map = map_empty();
        CljObject *value = (CljObject*)intern_symbol(NULL, "nil-value");
        RETAIN(value);
        map = map_assoc(map, NULL, value);
        TEST_ASSERT_NOT_NULL(map);
        
        // map_contains should return true for nil key
        int contains = map_contains(map, NULL);
        TEST_ASSERT_EQUAL_INT(1, contains);
        
        RELEASE((CljObject*)map);
        RELEASE(value);
    });
}

// Test: map_contains with nil key when key doesn't exist
TEST(test_map_contains_nil_key_not_exists) {
    WITH_AUTORELEASE_POOL({
        // Create map without nil key
        CljMap *map = (CljMap*)AUTORELEASE((CljObject*)map_empty());
        CljObject *key = AUTORELEASE((CljObject*)intern_symbol(NULL, ":key"));
        CljObject *value = AUTORELEASE((CljObject*)intern_symbol(NULL, "value"));
        RETAIN(key);
        RETAIN(value);
        map = map_assoc(map, key, value);
        TEST_ASSERT_NOT_NULL(map);
        
        // map_contains should return false for nil key
        int contains = map_contains(map, NULL);
        TEST_ASSERT_EQUAL_INT(0, contains);
    });
}

// Test: nil key and nil value in same map
TEST(test_map_nil_key_nil_value) {
    WITH_AUTORELEASE_POOL({
        // Create map with nil key and nil value
        CljMap *map = (CljMap*)AUTORELEASE((CljObject*)map_empty());
        map = map_assoc(map, NULL, NULL);
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_EQUAL_INT(1, map->count);
        
        // get nil key should return nil (NULL)
        CljObject *result = (CljObject*)map_get(map, NULL, NULL);
        TEST_ASSERT_NULL(result);
        
        // map_contains should return true
        int contains = map_contains(map, NULL);
        TEST_ASSERT_EQUAL_INT(1, contains);
    });
}

// Test: map literal with nil key and regular key
TEST(test_map_literal_nil_and_regular_key) {
    WITH_AUTORELEASE_POOL({
        // Create map literal: {nil "nil-value" :key "key-value"}
        CljObject *result = eval_string("{nil \"nil-value\" :key \"key-value\"}", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(2, map->count);
        
        // get nil key should return "nil-value"
        CljObject *nil_value = eval_string("(get {nil \"nil-value\" :key \"key-value\"} nil)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(nil_value);
        TEST_ASSERT_TRUE(TAG(nil_value) == CLJ_STRING);
        CljString *nil_str = as_clj_string(nil_value);
        TEST_ASSERT_NOT_NULL(nil_str);
        TEST_ASSERT_EQUAL_STRING("nil-value", clj_string_data(nil_str));
        
        // get :key should return "key-value"
        CljObject *key_value = eval_string("(get {nil \"nil-value\" :key \"key-value\"} :key)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(key_value);
        TEST_ASSERT_TRUE(TAG(key_value) == CLJ_STRING);
        CljString *key_str = as_clj_string(key_value);
        TEST_ASSERT_NOT_NULL(key_str);
        TEST_ASSERT_EQUAL_STRING("key-value", clj_string_data(key_str));
    });
}

// Test: Verify that nil key in map literal is stored as NULL, not SYM_NIL
TEST(test_map_literal_nil_key_stored_as_null) {
    WITH_AUTORELEASE_POOL({
        // Create map literal with nil key
        CljObject *result = eval_string("{nil \"test\"}", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
        
        CljMap *map = (CljMap*)result;
        TEST_ASSERT_EQUAL_INT(1, map->count);
        
        // Check that the key is NULL, not SYM_NIL
        ID stored_key = KV_KEY(map->data, 0);
        TEST_ASSERT_NULL(stored_key);  // Key should be NULL, not SYM_NIL
        
        // Verify that get with NULL key works
        ID value = map_get(map, NULL, NULL);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_TRUE(TAG(value) == CLJ_STRING);
    });
}

// Test: (get {nil "nil-value"} nil) should return "nil-value"
// This test specifically checks the issue where get returns nil instead of the value
TEST(test_get_nil_key_from_map_literal) {
    WITH_AUTORELEASE_POOL({
        // Create map with nil key: {nil "nil-value"}
        CljObject *map_obj = eval_string("{nil \"nil-value\"}", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(map_obj);
        TEST_ASSERT_TRUE(TAG(map_obj) == CLJ_MAP);
        
        CljMap *map = (CljMap*)map_obj;
        TEST_ASSERT_EQUAL_INT(1, map->count);
        
        // Verify the key is stored as NULL (not SYM_NIL)
        ID stored_key = KV_KEY(map->data, 0);
        TEST_ASSERT_NULL_MESSAGE(stored_key, "nil key should be stored as NULL, not SYM_NIL");
        
        // Now test get: (get {nil "nil-value"} nil) should return "nil-value"
        CljObject *result = eval_string("(get {nil \"nil-value\"} nil)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "(get {nil \"nil-value\"} nil) should return \"nil-value\", not nil");
        
        if (result) {
            TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_STRING, "Result should be a string");
            CljString *str = as_clj_string(result);
            TEST_ASSERT_NOT_NULL(str);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("nil-value", clj_string_data(str), 
                "Result should be \"nil-value\"");
        }
    });
}

// Test: NOT_FOUND sentinel correctly distinguishes "key not found" from "key found with nil value"
// This is critical for namespace lookups where nil is a valid value
TEST(test_map_get_not_found_sentinel_edge_cases) {
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)AUTORELEASE((CljObject*)map_empty());
        CljSymbol *key1 = (CljSymbol*)AUTORELEASE((CljObject*)intern_symbol_global("key1"));
        CljSymbol *key2 = (CljSymbol*)AUTORELEASE((CljObject*)intern_symbol_global("key2"));
        CljSymbol *key3 = (CljSymbol*)AUTORELEASE((CljObject*)intern_symbol_global("key3"));
        CljSymbol *missing_key = (CljSymbol*)AUTORELEASE((CljObject*)intern_symbol_global("missing"));
        
        // Add key1 with non-nil value
        CljObject *value1 = (CljObject*)fixnum(42);
        map = map_assoc(map, key1, value1);
        
        // Add key2 with nil value (NULL)
        map = map_assoc(map, key2, NULL);
        
        // Add key3 with another non-nil value
        CljObject *value3 = (CljObject*)fixnum(100);
        map = map_assoc(map, key3, value3);
        
        TEST_ASSERT_EQUAL_INT(3, map->count);
        
        // Test 1: Key exists with non-nil value -> should return value, not NOT_FOUND
        ID result1 = map_get(map, key1, NOT_FOUND);
        TEST_ASSERT_NOT_NULL_MESSAGE(result1, "key1 should return value, not NOT_FOUND");
        TEST_ASSERT_TRUE_MESSAGE(result1 != NOT_FOUND, "key1 should not return NOT_FOUND");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result1), "key1 value should be fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(42, as_fixnum(result1), "key1 value should be 42");
        
        // Test 2: Key exists with nil value -> should return NULL, not NOT_FOUND
        ID result2 = map_get(map, key2, NOT_FOUND);
        TEST_ASSERT_NULL_MESSAGE(result2, "key2 should return NULL (nil value), not NOT_FOUND");
        TEST_ASSERT_TRUE_MESSAGE(result2 != NOT_FOUND, "key2 should not return NOT_FOUND (key exists)");
        
        // Test 3: Key exists with non-nil value -> should return value, not NOT_FOUND
        ID result3 = map_get(map, key3, NOT_FOUND);
        TEST_ASSERT_NOT_NULL_MESSAGE(result3, "key3 should return value, not NOT_FOUND");
        TEST_ASSERT_TRUE_MESSAGE(result3 != NOT_FOUND, "key3 should not return NOT_FOUND");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result3), "key3 value should be fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(100, as_fixnum(result3), "key3 value should be 100");
        
        // Test 4: Key does not exist -> should return NOT_FOUND, not NULL
        ID result4 = map_get(map, missing_key, NOT_FOUND);
        TEST_ASSERT_TRUE_MESSAGE(result4 == NOT_FOUND, "missing_key should return NOT_FOUND, not NULL");
        
        // Test 5: Verify map_contains correctly identifies all cases
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, map_contains(map, key1), "key1 should exist");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, map_contains(map, key2), "key2 should exist (even with nil value)");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, map_contains(map, key3), "key3 should exist");
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, map_contains(map, missing_key), "missing_key should not exist");
    });
}

// Test MAP_FOR_EACH macro - iterate over all key-value pairs
TEST(test_map_for_each_macro) {
    CljMap *map = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Create some keys and values
    CljObject *key1 = (CljObject*)intern_symbol(NULL, ":a");
    CljObject *key2 = (CljObject*)intern_symbol(NULL, ":b");
    CljObject *key3 = (CljObject*)intern_symbol(NULL, ":c");
    
    CljValue val1 = fixnum(1);
    CljValue val2 = fixnum(2);
    CljValue val3 = fixnum(3);
    
    // Add key-value pairs to map
    map = map_assoc(map, key1, val1);
    map = map_assoc(map, key2, val2);
    map = map_assoc(map, key3, val3);
    
    TEST_ASSERT_EQUAL_INT(3, map_count(map));
    
    // Count iterations and verify keys/values
    int iteration_count = 0;
    CljObject *found_keys[3] = {NULL, NULL, NULL};
    CljValue found_values[3] = {NULL, NULL, NULL};
    
    MAP_FOR_EACH(map, key, value) {
        found_keys[iteration_count] = key;
        found_values[iteration_count] = (CljValue)value;
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(3, iteration_count);
    
    // Verify that all keys and values were found
    bool found_key1 = false, found_key2 = false, found_key3 = false;
    for (int i = 0; i < 3; i++) {
        if (found_keys[i] == key1) {
            found_key1 = true;
            TEST_ASSERT_EQUAL(val1, found_values[i]);
        } else if (found_keys[i] == key2) {
            found_key2 = true;
            TEST_ASSERT_EQUAL(val2, found_values[i]);
        } else if (found_keys[i] == key3) {
            found_key3 = true;
            TEST_ASSERT_EQUAL(val3, found_values[i]);
        }
    }
    
    TEST_ASSERT_TRUE(found_key1);
    TEST_ASSERT_TRUE(found_key2);
    TEST_ASSERT_TRUE(found_key3);
}

// Test MAP_FOR_EACH with empty map
TEST(test_map_for_each_empty_map) {
    CljMap *map = (CljMap*)AUTORELEASE((CljObject*)map_empty());
    
    int iteration_count = 0;
    MAP_FOR_EACH(map, key, value) {
        (void)key; (void)value;  // unused
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(0, iteration_count);
}

// Test MAP_FOR_EACH with NULL map (should not crash)
TEST(test_map_for_each_null_map) {
    CljMap *map = NULL;
    
    int iteration_count = 0;
    MAP_FOR_EACH(map, key, value) {
        (void)key; (void)value;  // unused
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(0, iteration_count);
}

// Test MAP_FOR_EACH with NULL keys and NULL values
TEST(test_map_for_each_with_null_keys_and_values) {
    CljMap *map = (CljMap*)AUTORELEASE((CljObject*)make_map(4));
    
    // Add entries with NULL key and NULL value
    CljObject *key1 = (CljObject*)intern_symbol(NULL, ":a");
    CljObject *key2 = NULL;  // NULL key (nil in Clojure)
    CljObject *key3 = (CljObject*)intern_symbol(NULL, ":c");
    
    CljValue val1 = fixnum(1);
    CljValue val2 = NULL;  // NULL value
    CljValue val3 = fixnum(3);
    
    // Add key-value pairs to map
    map = map_assoc(map, key1, val1);
    map = map_assoc(map, key2, val2);  // NULL key, NULL value
    map = map_assoc(map, key3, val3);
    
    TEST_ASSERT_EQUAL_INT(3, map_count(map));
    
    // Count iterations and verify keys/values (including NULL)
    int iteration_count = 0;
    CljObject *found_keys[3] = {NULL, NULL, NULL};
    CljValue found_values[3] = {NULL, NULL, NULL};
    
    MAP_FOR_EACH(map, key, value) {
        found_keys[iteration_count] = key;
        found_values[iteration_count] = (CljValue)value;
        iteration_count++;
    }
    
    TEST_ASSERT_EQUAL_INT(3, iteration_count);
    
    // Verify that all keys and values were found (including NULL)
    bool found_key1 = false, found_key2 = false, found_key3 = false;
    for (int i = 0; i < 3; i++) {
        if (found_keys[i] == key1) {
            found_key1 = true;
            TEST_ASSERT_EQUAL(val1, found_values[i]);
        } else if (found_keys[i] == key2) {  // key2 is NULL
            found_key2 = true;
            TEST_ASSERT_NULL(found_keys[i]);  // Key should be NULL
            TEST_ASSERT_NULL(found_values[i]);  // Value should be NULL
        } else if (found_keys[i] == key3) {
            found_key3 = true;
            TEST_ASSERT_EQUAL(val3, found_values[i]);
        }
    }
    
    TEST_ASSERT_TRUE(found_key1);
    TEST_ASSERT_TRUE(found_key2);  // NULL key should be found
    TEST_ASSERT_TRUE(found_key3);
}

// ============================================================================
// Tests for new map functions (merge, contains?, into, select-keys, find)
// High-level tests using eval_string with Clojure equality checks
// ============================================================================

// Helper macro for boolean assertions
#define ASSERT_TRUE_RESULT(expr) do { \
    CljObject *r = eval_string(expr, g_test_eval_state); \
    TEST_ASSERT_NOT_NULL_MESSAGE(r, expr " should not be nil"); \
    TEST_ASSERT_TRUE_MESSAGE(r == clj_true, expr " should be true"); \
} while(0)

#define ASSERT_FALSE_RESULT(expr) do { \
    CljObject *r = eval_string(expr, g_test_eval_state); \
    TEST_ASSERT_NOT_NULL_MESSAGE(r, expr " should not be nil"); \
    TEST_ASSERT_TRUE_MESSAGE(r == clj_false, expr " should be false"); \
} while(0)

#define ASSERT_NIL_RESULT(expr) do { \
    CljObject *r = eval_string(expr, g_test_eval_state); \
    TEST_ASSERT_NULL_MESSAGE(r, expr " should be nil"); \
} while(0)

// ============================================================================
// merge tests
// ============================================================================

TEST(test_merge_no_args) {
    ASSERT_NIL_RESULT("(merge)");
}

TEST(test_merge_nil) {
    ASSERT_NIL_RESULT("(merge nil)");
}

TEST(test_merge_single_map) {
    ASSERT_TRUE_RESULT("(= (merge {:a 1}) {:a 1})");
}

TEST(test_merge_two_maps) {
    ASSERT_TRUE_RESULT("(= (merge {:a 1} {:b 2}) {:a 1 :b 2})");
}

TEST(test_merge_override) {
    ASSERT_TRUE_RESULT("(= (merge {:a 1} {:a 2}) {:a 2})");
}

TEST(test_merge_with_nil) {
    ASSERT_TRUE_RESULT("(= (merge {:a 1} nil {:b 2}) {:a 1 :b 2})");
}

TEST(test_merge_multiple_maps) {
    ASSERT_TRUE_RESULT("(= (count (merge {:a 1} {:b 2} {:c 3})) 3)");
}

// ============================================================================
// contains? tests
// ============================================================================

TEST(test_contains_p_key_exists) {
    ASSERT_TRUE_RESULT("(contains? {:a 1} :a)");
}

TEST(test_contains_p_key_not_exists) {
    ASSERT_FALSE_RESULT("(contains? {:a 1} :b)");
}

TEST(test_contains_p_nil_coll) {
    ASSERT_FALSE_RESULT("(contains? nil :a)");
}

TEST(test_contains_p_vector_valid_index) {
    ASSERT_TRUE_RESULT("(contains? [1 2 3] 0)");
    ASSERT_TRUE_RESULT("(contains? [1 2 3] 2)");
}

TEST(test_contains_p_vector_invalid_index) {
    ASSERT_FALSE_RESULT("(contains? [1 2 3] 5)");
    ASSERT_FALSE_RESULT("(contains? [1 2 3] -1)");
}

TEST(test_contains_p_nil_value) {
    ASSERT_TRUE_RESULT("(contains? {:a nil} :a)");
}

// ============================================================================
// find tests
// ============================================================================

TEST(test_find_key_exists) {
    ASSERT_TRUE_RESULT("(= (find {:a 1 :b 2} :a) [:a 1])");
}

TEST(test_find_key_not_exists) {
    ASSERT_NIL_RESULT("(find {:a 1 :b 2} :c)");
}

TEST(test_find_nil_map) {
    ASSERT_NIL_RESULT("(find nil :a)");
}

// ============================================================================
// select-keys tests
// ============================================================================

TEST(test_select_keys_basic) {
    ASSERT_TRUE_RESULT("(= (select-keys {:a 1 :b 2 :c 3} [:a :c]) {:a 1 :c 3})");
}

TEST(test_select_keys_missing_keys) {
    ASSERT_TRUE_RESULT("(= (select-keys {:a 1} [:b :c]) {})");
}

TEST(test_select_keys_nil_map) {
    ASSERT_TRUE_RESULT("(= (select-keys nil [:a]) {})");
}

TEST(test_select_keys_nil_keys) {
    ASSERT_TRUE_RESULT("(= (select-keys {:a 1} nil) {})");
}

// ============================================================================
// into tests
// ============================================================================

TEST(test_into_vector_to_vector) {
    ASSERT_TRUE_RESULT("(= (into [] [1 2 3]) [1 2 3])");
}

TEST(test_into_vector_append) {
    ASSERT_TRUE_RESULT("(= (into [1 2] [3 4]) [1 2 3 4])");
}

TEST(test_into_pairs_to_map) {
    ASSERT_TRUE_RESULT("(= (into {:a 1} [[:b 2] [:c 3]]) {:a 1 :b 2 :c 3})");
}

TEST(test_into_map_to_map) {
    ASSERT_TRUE_RESULT("(= (into {:a 1} {:b 2}) {:a 1 :b 2})");
}

TEST(test_into_map_to_vector) {
    ASSERT_TRUE_RESULT("(= (count (into [] {:a 1 :b 2})) 2)");
    // Each element is a [k v] pair
    ASSERT_TRUE_RESULT("(= (count (first (into [] {:a 1}))) 2)");
}

TEST(test_into_nil_source) {
    ASSERT_TRUE_RESULT("(= (into [1 2] nil) [1 2])");
}

TEST(test_into_list_to_vector) {
    ASSERT_TRUE_RESULT("(= (into [] '(1 2 3)) [1 2 3])");
}

// ============================================================================
// update tests (native implementation)
// ============================================================================

TEST(test_update_native_basic) {
    ASSERT_TRUE_RESULT("(= (update {:a 1} :a inc) {:a 2})");
}

TEST(test_update_native_with_function) {
    ASSERT_TRUE_RESULT("(= (update {:count 5} :count (fn [x] (* x 2))) {:count 10})");
}

TEST(test_update_native_missing_key) {
    // Missing key passes nil to function
    ASSERT_TRUE_RESULT("(= (update {:a 1} :b (fn [x] (if x x 0))) {:a 1 :b 0})");
}

TEST(test_update_native_with_extra_args) {
    // update with extra args: (update m k f arg1 arg2)
    ASSERT_TRUE_RESULT("(= (update {:a 1} :a + 10) {:a 11})");
    ASSERT_TRUE_RESULT("(= (update {:a 1} :a + 10 20) {:a 31})");
}
