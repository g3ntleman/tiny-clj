// Copy-on-Write (COW) Tests for Maps and Vectors
// Tests RC-based COW behavior: RC=1 → in-place mutation, RC>1 → Copy-on-Write
#include "tests_common.h"
#include "map.h"
#include "vector.h"
#include "../symbol.h"
#include "../env_stack.h"
#include "kv_macros.h"

// ============================================================================
// COW ASSUMPTIONS TESTS
// ============================================================================

TEST(test_autorelease_does_not_increase_rc) {
    WITH_AUTORELEASE_POOL({
        // Test 1: AUTORELEASE does NOT increase RC
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // AUTORELEASE should NOT increase RC
        AUTORELEASE(map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
    });
}

TEST(test_retain_increases_rc) {
    WITH_AUTORELEASE_POOL({
        // Test 3: RETAIN increases RC
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // RETAIN should increase RC
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Cleanup
        RELEASE(map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
    });
}

TEST(test_autorelease_with_retain) {
    WITH_AUTORELEASE_POOL({
        // Test 5: AUTORELEASE + RETAIN combination
        CljMap *map = (CljMap*)make_map(4);
        
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        AUTORELEASE(map);
        TEST_ASSERT_EQUAL(2, map->base.rc); // Should stay 2
        
        // Cleanup
        RELEASE(map);
    });
}

TEST(test_multiple_autorelease_same_object) {
    
    WITH_AUTORELEASE_POOL({
        // Test 6: Multiple AUTORELEASE same object
        CljMap *map = (CljMap*)make_map(4);
        
        AUTORELEASE(map);
        AUTORELEASE(map);
        AUTORELEASE(map);
        
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
// MAP COW FUNCTIONALITY TESTS
// ============================================================================

TEST(test_cow_inplace_mutation_rc_one) {
    
    WITH_AUTORELEASE_POOL({
        // Test 1: In-place mutation bei RC=1 (COW enabled)
        CljMap *map = (CljMap*)make_map(4);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // First assoc: RC=1 → in-place mutation (same pointer)
        CljMap *new_map1 = map_assoc(map, fixnum(1), fixnum(10));
        TEST_ASSERT_EQUAL(1, new_map1->base.rc);
        TEST_ASSERT_EQUAL_PTR((CljValue)map, (CljValue)new_map1); // Same pointer! (in-place)
        map = new_map1; // Update map reference
        
        // Second assoc: RC=1 → in-place mutation (same pointer)
        CljMap *new_map2 = map_assoc(map, fixnum(2), fixnum(20));
        TEST_ASSERT_EQUAL(1, new_map2->base.rc);
        TEST_ASSERT_EQUAL_PTR((CljValue)map, (CljValue)new_map2); // Same pointer! (in-place)
        map = new_map2; // Update map reference
        
        // Third assoc: Update existing key, RC=1 → in-place mutation
        CljMap *new_map3 = map_assoc(map, fixnum(1), fixnum(11));
        TEST_ASSERT_EQUAL(1, new_map3->base.rc);
        TEST_ASSERT_EQUAL_PTR((CljValue)map, (CljValue)new_map3); // Same pointer! (in-place)
        
        // Verify entries
        CljValue val1 = map_get_sentinel((CljMap*)map, fixnum(1), NULL);
        CljValue val2 = map_get_sentinel((CljMap*)map, fixnum(2), NULL);
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(11, as_fixnum(val1)); // Updated value
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
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Now COW should trigger
        CljMap *new_map = map_assoc(map, fixnum(2), fixnum(20));
        TEST_ASSERT_EQUAL(2, map->base.rc);  // Original RC unchanged
        TEST_ASSERT_NOT_EQUAL((CljValue)map, (CljValue)new_map); // NEW pointer!
        
        // Verify original map unchanged
        CljValue val1_orig = map_get_sentinel((CljMap*)map, fixnum(1), NULL);
        CljValue val2_orig = map_get_sentinel((CljMap*)map, fixnum(2), NULL);
        TEST_ASSERT_NOT_NULL(val1_orig);
        TEST_ASSERT_NULL(val2_orig);  // Original doesn't have key=2
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_orig));
        
        // Verify new map has both entries
        CljValue val1_new = map_get_sentinel((CljMap*)new_map, fixnum(1), NULL);
        CljValue val2_new = map_get_sentinel((CljMap*)new_map, fixnum(2), NULL);
        TEST_ASSERT_NOT_NULL(val1_new);
        TEST_ASSERT_NOT_NULL(val2_new);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_new));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2_new));
        
        
        // Cleanup
        RELEASE(map);
    });
}

TEST(test_cow_original_map_unchanged) {
    
    WITH_AUTORELEASE_POOL({
        // Test 3: Original map unchanged after COW
        CljMap *map = (CljMap*)make_map(4);
        map = map_assoc(map, fixnum(1), fixnum(10));
        map = map_assoc(map, fixnum(2), fixnum(20));
        
        TEST_ASSERT_EQUAL(2, map->count);
        
        // RETAIN to trigger COW
        RETAIN(map);
        CljMap *new_map = map_assoc(map, fixnum(3), fixnum(30));
        
        // Original should be unchanged
        TEST_ASSERT_EQUAL(2, map->count); // Original unchanged
        
        // New map should have 3 entries
        CljValue val3 = map_get_sentinel((CljMap*)new_map, fixnum(3), NULL);
        TEST_ASSERT_NOT_NULL(val3);
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(val3));
        
        
        // Cleanup
        RELEASE(map);
    });
}

TEST(test_cow_with_autorelease) {
    
    WITH_AUTORELEASE_POOL({
        // Test 4: AUTORELEASE mit COW
        CljMap *map = (CljMap*)make_map(4);
        
        AUTORELEASE(map);
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
        RETAIN(map);
        CljMap *new_map = map_assoc(map, fixnum(5), fixnum(50));
        AUTORELEASE(new_map);
        
        // Cleanup
        RELEASE(map);
        
    });
}

// ============================================================================
// MAP COW EVAL INTEGRATION TESTS
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
        TEST_ASSERT_EQUAL(2, env->base.rc);  // Original unchanged
        TEST_ASSERT_NOT_EQUAL((CljValue)env, (CljValue)new_env); // NEW pointer!
        
        // Verify original env unchanged
        CljValue orig_x = map_get_sentinel((CljMap*)env, intern_symbol_global("x"), NULL);
        CljValue orig_y = map_get_sentinel((CljMap*)env, intern_symbol_global("y"), NULL);
        TEST_ASSERT_NOT_NULL(orig_x);
        TEST_ASSERT_NULL(orig_y);  // Original doesn't have y
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(orig_x));
        
        
        // Cleanup
        RELEASE((CljValue)env);
    });
}

TEST(test_cow_memory_efficiency_benchmark) {
    
    WITH_AUTORELEASE_POOL({
        // Test 4: Memory-Effizienz Benchmark
        CljMap *env = (CljMap*)make_map(4);
        
        for (int i = 0; i < 100; i++) {
            CljValue new_env = map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
            AUTORELEASE(new_env);
            
            if (i % 10 == 0) {
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
            CljValue val = map_get_sentinel((CljMap*)current_env, fixnum(i), NULL);
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
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation
        CljMap *new_map = map_assoc(map, fixnum(3), fixnum(30));
        TEST_ASSERT_EQUAL(2, map->base.rc);
        TEST_ASSERT_NOT_EQUAL((CljValue)map, (CljValue)new_map);
        
        // Verify original unchanged
        CljValue val3_orig = map_get_sentinel((CljMap*)map, fixnum(3), NULL);
        TEST_ASSERT_NULL(val3_orig);
        
        // Verify new map has all entries
        CljValue val1_new = map_get_sentinel((CljMap*)new_map, fixnum(1), NULL);
        CljValue val2_new = map_get_sentinel((CljMap*)new_map, fixnum(2), NULL);
        CljValue val3_new = map_get_sentinel((CljMap*)new_map, fixnum(3), NULL);
        TEST_ASSERT_NOT_NULL(val1_new);
        TEST_ASSERT_NOT_NULL(val2_new);
        TEST_ASSERT_NOT_NULL(val3_new);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1_new));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2_new));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(val3_new));
        
        
        // Cleanup
        RELEASE(map);
    });
}

// Test AUTORELEASE map_assoc with ASSIGN pattern (H1/H2/H3/H5 hypotheses)
TEST(test_map_assoc_autorelease_with_assign) {
    WITH_AUTORELEASE_POOL({
        // H1: Create map and use ASSIGN with autoreleased result
        CljMap *map = make_map(4);
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // H2: ASSIGN should work correctly with autoreleased map
        CljSymbol *key = intern_symbol_global("test-key");
        CljObject *value = fixnum(42);
        
        // This is the pattern used in ns_define
        ASSIGN(map, map_assoc(map, key, value));
        
        // H1: Map should still be valid after ASSIGN
        TEST_ASSERT_NOT_NULL(map);
        TEST_ASSERT_TRUE(map->base.rc >= 1);  // RC should be at least 1
        
        // H5: map_get should find the value
        ID retrieved = map_get(map, key);
        TEST_ASSERT_TRUE(retrieved != NOT_FOUND);
        
        // H3: Value should be correct fixnum
        TEST_ASSERT_TRUE(is_fixnum((CljValue)retrieved));
        TEST_ASSERT_EQUAL(42, as_fixnum((CljValue)retrieved));
        
        // Cleanup
        RELEASE(map);
    });
}

// Test multiple ASSIGN calls in sequence (simulates ns_define behavior)
TEST(test_map_assoc_multiple_assign_sequence) {
    WITH_AUTORELEASE_POOL({
        CljMap *map = make_map(4);
        TEST_ASSERT_NOT_NULL(map);
        
        // Add multiple entries like ns_define does
        CljSymbol *key1 = intern_symbol_global("var1");
        CljSymbol *key2 = intern_symbol_global("var2");
        CljSymbol *key3 = intern_symbol_global("var3");
        
        ASSIGN(map, map_assoc(map, key1, fixnum(10)));
        ASSIGN(map, map_assoc(map, key2, fixnum(20)));
        ASSIGN(map, map_assoc(map, key3, fixnum(30)));
        
        // All values should be retrievable
        ID v1 = map_get(map, key1);
        ID v2 = map_get(map, key2);
        ID v3 = map_get(map, key3);
        
        TEST_ASSERT_TRUE(v1 != NOT_FOUND);
        TEST_ASSERT_TRUE(v2 != NOT_FOUND);
        TEST_ASSERT_TRUE(v3 != NOT_FOUND);
        
        TEST_ASSERT_EQUAL(10, as_fixnum((CljValue)v1));
        TEST_ASSERT_EQUAL(20, as_fixnum((CljValue)v2));
        TEST_ASSERT_EQUAL(30, as_fixnum((CljValue)v3));
        
        // Cleanup
        RELEASE(map);
    });
}

// ============================================================================
// VECTOR COW FUNCTIONALITY TESTS
// ============================================================================

// Test that vector_conj uses in-place mutation when RC=1
TEST(test_vector_conj_cow_rc_one_inplace) {
    WITH_AUTORELEASE_POOL({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        // base.rc is part of CljObject, access via cast
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // First conj should be in-place (RC=1, capacity allows)
        CljValue new_vec1 = (CljValue)vector_conj((CljVector*)vec, fixnum(10));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, (CljValue)new_vec1); // Same pointer! (in-place)
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        TEST_ASSERT_EQUAL_INT(1, vector_count(vec));
        
        // Second conj should also be in-place
        CljValue new_vec2 = (CljValue)vector_conj((CljVector*)vec, fixnum(20));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, (CljValue)new_vec2); // Same pointer! (in-place)
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // Third conj should also be in-place
        CljValue new_vec3 = (CljValue)vector_conj((CljVector*)vec, fixnum(30));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, (CljValue)new_vec3); // Same pointer! (in-place)
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
        
        // Verify entries
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(vec, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(vec, 2)));
    });
}

// Test that vector_conj uses Copy-on-Write when RC>1
TEST(test_vector_conj_cow_rc_greater_one) {
    WITH_AUTORELEASE_POOL({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // Add some entries
        vector_conj((CljVector*)vec, fixnum(10));
        
        // RETAIN to increase RC
        RETAIN(vec);
        TEST_ASSERT_EQUAL(2, ((CljObject*)vec)->rc);
        
        // Now COW should trigger
        CljValue new_vec = (CljValue)vector_conj((CljVector*)vec, fixnum(20));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, new_vec); // NEW pointer!
        TEST_ASSERT_EQUAL(2, ((CljObject*)vec)->rc); // Original RC unchanged
        
        // Verify original vector unchanged
        TEST_ASSERT_EQUAL_INT(1, vector_count(vec));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        
        // Verify new vector has both entries
        CljVector *new_vec_data = as_vector(new_vec);
        TEST_ASSERT_EQUAL_INT(2, vector_count(new_vec_data));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec_data, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec_data, 1)));
        
        // Cleanup
        RELEASE(vec);
        RELEASE(new_vec);
    });
}

// Test that vector_conj handles capacity growth with COW
TEST(test_vector_conj_cow_capacity_growth) {
    WITH_AUTORELEASE_POOL({
        CljVector *vec = (CljVector*)make_vector(2, CLJ_VECTOR);
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // Fill capacity
        vector_conj((CljVector*)vec, fixnum(10));
        vector_conj((CljVector*)vec, fixnum(20));
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // RETAIN to trigger COW
        RETAIN(vec);
        
        // Add more - should trigger COW with growth
        CljValue new_vec = (CljValue)vector_conj((CljVector*)vec, fixnum(30));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, new_vec); // NEW pointer!
        
        CljVector *new_vec_data = as_vector(new_vec);
        // Capacity is implementation detail, only check count
        TEST_ASSERT_EQUAL_INT(3, vector_count(new_vec_data));
        
        // Verify all entries
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec_data, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec_data, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec_data, 2)));
        
        // Original unchanged
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // Cleanup
        RELEASE(vec);
        RELEASE(new_vec);
    });
}

// Ensure we don't accidentally force full copies via persistent(transient(pv)).
// The vector implementation exposes a copy counter (make_vector_copy) for this.
TEST(test_vector_copy_counter_detects_forced_copy_patterns) {
    WITH_AUTORELEASE_POOL({
        // Setup: a small persistent vector with enough capacity for a conj without growth.
        CljVector *pv = make_vector(8, CLJ_VECTOR);
        TEST_ASSERT_NOT_NULL(pv);

        vector_make_copy_count_reset();

        // Pure persistent conj with RC=1 should not need to copy backing storage.
        CljVector *pv2 = vector_conj(pv, fixnum(1));
        TEST_ASSERT_EQUAL_PTR(pv, pv2);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, (uint64_t)vector_make_copy_count(),
                                        "vector_conj on RC=1 (no growth) should not call make_vector_copy");

        // Converting persistent -> transient should copy backing storage (by design).
        CljVector *tv = vector_transient(pv);
        TEST_ASSERT_NOT_NULL(tv);
        TEST_ASSERT_TRUE(TAG(tv) == CLJ_VECTOR_TRANSIENT || TAG(tv) == CLJ_VECTOR_TRANSIENT_WEAK);
        TEST_ASSERT_TRUE_MESSAGE(vector_make_copy_count() > 0,
                                 "vector_transient should copy backing storage (make_vector_copy_count must increase)");

        // Cleanup
        RELEASE(tv);
        RELEASE(pv);
    });
}

// Ensure env-stack helpers keep rc=1 fast-paths viable:
// - no accidental RETAIN of the stack (rc must stay 1)
// - no accidental forced copies (make_vector_copy_count stays 0 for typical push/pop)
TEST(test_env_stack_helpers_do_not_break_cow_fast_path) {
    WITH_AUTORELEASE_POOL({
        CljVector *stack = NULL;

        vector_make_copy_count_reset();

        CljMap *m1 = (CljMap*)make_map(4);
        CljMap *m2 = (CljMap*)make_map(4);

        env_stack_push_inplace(&stack, m1);
        env_stack_push_inplace(&stack, m2);

        TEST_ASSERT_NOT_NULL(stack);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, ((CljObject*)stack)->rc,
                                      "env_stack must not RETAIN the stack; rc must remain 1");
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, (uint64_t)vector_make_copy_count(),
                                         "env_stack push should not force make_vector_copy in the rc=1 / no-growth case");

        // Pop once; should remain rc=1 and still not force copies.
        env_stack_pop_inplace(&stack);
        TEST_ASSERT_NOT_NULL(stack);
        TEST_ASSERT_EQUAL_INT(1, ((CljObject*)stack)->rc);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, (uint64_t)vector_make_copy_count(),
                                         "env_stack pop should not force make_vector_copy in the rc=1 case");

        // Balance retains: vectors retain their elements on insert.
        RELEASE(m1);
        RELEASE(m2);
        RELEASE(stack);
    });
}

// Test that original vector remains unchanged after COW
TEST(test_vector_conj_cow_original_unchanged) {
    WITH_AUTORELEASE_POOL({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        
        // Add entries
        vector_conj((CljVector*)vec, fixnum(10));
        vector_conj((CljVector*)vec, fixnum(20));
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        
        // RETAIN to trigger COW
        RETAIN(vec);
        CljValue new_vec = (CljValue)vector_conj((CljVector*)vec, fixnum(30));
        
        // Original should be unchanged
        TEST_ASSERT_EQUAL_INT(2, vector_count(vec));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(vec, 1)));
        
        // New vector should have all entries
        CljVector *new_vec_data = as_vector(new_vec);
        TEST_ASSERT_EQUAL_INT(3, vector_count(new_vec_data));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec_data, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec_data, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec_data, 2)));
        
        // Cleanup
        RELEASE(vec);
        RELEASE(new_vec);
    });
}

// Test memory leak detection for vector_conj COW
TEST(test_vector_conj_cow_memory_leak) {
    WITH_MEMORY_PROFILING({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        
        // Add entries
        vector_conj((CljVector*)vec, fixnum(10));
        vector_conj((CljVector*)vec, fixnum(20));
        vector_conj((CljVector*)vec, fixnum(30));
        
        // RETAIN to trigger COW
        RETAIN(vec);
        CljValue new_vec = (CljValue)vector_conj((CljVector*)vec, fixnum(40));
        
        // Cleanup
        RELEASE(vec);
        RELEASE(new_vec);
        
        // Memory should be clean (no leaks)
    });
}

// ============================================================================
// VECTOR ASSOC COW TESTS
// ============================================================================

// Test that vector_assoc uses in-place mutation when RC=1
TEST(test_vector_assoc_cow_rc_one_inplace) {
    WITH_AUTORELEASE_POOL({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // Add initial entries
        vec = vector_conj(vec, fixnum(10));
        vec = vector_conj(vec, fixnum(20));
        vec = vector_conj(vec, fixnum(30));
        TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc); // Still RC=1
        
        // Update at index 1: RC=1 → in-place mutation (same pointer)
        CljVector *new_vec = vector_assoc(vec, 1, fixnum(99));
        TEST_ASSERT_EQUAL(1, ((CljObject*)new_vec)->rc);
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, (CljValue)new_vec); // Same pointer! (in-place)
        
        // Verify update
        TEST_ASSERT_EQUAL_INT(99, as_fixnum((CljValue)vector_nth(new_vec, 1)));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec, 0)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec, 2)));
        
        // Verify multiple updates all use same pointer
        CljVector *new_vec2 = vector_assoc(vec, 0, fixnum(88));
        TEST_ASSERT_EQUAL_PTR((CljValue)vec, (CljValue)new_vec2); // Same pointer!
    });
}

// Test that vector_assoc uses Copy-on-Write when RC>1
TEST(test_vector_assoc_cow_rc_greater_one) {
    WITH_AUTORELEASE_POOL({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        TEST_ASSERT_EQUAL(1, ((CljObject*)vec)->rc);
        
        // Add entries
        vec = vector_conj(vec, fixnum(10));
        vec = vector_conj(vec, fixnum(20));
        vec = vector_conj(vec, fixnum(30));
        TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
        
        // RETAIN to increase RC
        RETAIN(vec);
        TEST_ASSERT_EQUAL(2, ((CljObject*)vec)->rc);
        
        // Now COW should trigger
        CljVector *new_vec = vector_assoc(vec, 1, fixnum(99));
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, (CljValue)new_vec); // NEW pointer!
        TEST_ASSERT_EQUAL(2, ((CljObject*)vec)->rc); // Original RC unchanged
        
        // Verify original vector unchanged
        TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(vec, 1)));
        
        // Verify new vector has updated value
        TEST_ASSERT_EQUAL_INT(3, vector_count(new_vec));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(new_vec, 0)));
        TEST_ASSERT_EQUAL_INT(99, as_fixnum((CljValue)vector_nth(new_vec, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec, 2)));
        
        // Cleanup
        RELEASE(vec);
        RELEASE(new_vec);
    });
}

// Test that original vector remains unchanged after vector_assoc COW
TEST(test_vector_assoc_cow_original_unchanged) {
    WITH_AUTORELEASE_POOL({
        CljVector* vec = make_vector(4, CLJ_VECTOR);
        
        // Add entries
        vec = vector_conj(vec, fixnum(10));
        vec = vector_conj(vec, fixnum(20));
        vec = vector_conj(vec, fixnum(30));
        TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
        
        // RETAIN to trigger COW
        RETAIN(vec);
        CljVector *new_vec = vector_assoc(vec, 0, fixnum(99));
        
        // Should be different pointer (COW triggered)
        TEST_ASSERT_NOT_EQUAL((CljValue)vec, (CljValue)new_vec);
        
        // Original should be unchanged
        TEST_ASSERT_EQUAL_INT(3, vector_count(vec));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum((CljValue)vector_nth(vec, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(vec, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(vec, 2)));
        
        // New vector should have updated value
        TEST_ASSERT_EQUAL_INT(3, vector_count(new_vec));
        TEST_ASSERT_EQUAL_INT(99, as_fixnum((CljValue)vector_nth(new_vec, 0)));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum((CljValue)vector_nth(new_vec, 1)));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum((CljValue)vector_nth(new_vec, 2)));
        
        // Cleanup
        RELEASE(vec);
        RELEASE(new_vec);
    });
}

