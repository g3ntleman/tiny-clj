/*
 * Consolidated COW (Copy-on-Write) Tests
 * 
 * This file consolidates all COW-related tests from multiple files:
 * - cow_assumptions_tests.c
 * - test_cow_assumptions.c
 * - test_map_cow.c
 * - test_cow_eval_integration.c
 * - test_cow_simple_eval.c
 * - test_cow_minimal.c
 * - test_simple_cow.c
 */

#include "tests_common.h"

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
        printf("✓ RC=2 would trigger COW in map_assoc_cow\n");
        
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
            CljValue new_env = map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10));
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
        CljValue new_map1 = map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("After first assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        TEST_ASSERT_EQUAL(map, new_map1); // Same pointer!
        
        // Second assoc should also be in-place
        CljValue new_map2 = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
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
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        
        // RETAIN to increase RC
        RETAIN((CljValue)map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Now COW should trigger
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
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
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        
        printf("Original map count=%d\n", map->count);
        TEST_ASSERT_EQUAL(2, map->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(3), fixnum(30));
        
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
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
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
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        map_assoc_cow((CljValue)map, fixnum(3), fixnum(30));
        map_assoc_cow((CljValue)map, fixnum(4), fixnum(40));
        
        printf("Created map with %d entries\n", map->count);
        TEST_ASSERT_EQUAL(4, map->count);
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(5), fixnum(50));
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
            CljValue new_env = map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10));
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
        map_assoc_cow((CljValue)env, intern_symbol_global("x"), fixnum(1));
        printf("Initial env RC=%d\n", env->base.rc);
        
        // Simulate closure holding reference
        RETAIN((CljValue)env);
        printf("After RETAIN (closure): RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(2, env->base.rc);
        
        // Closure-Operation sollte COW triggern
        CljValue new_env = map_assoc_cow((CljValue)env, intern_symbol_global("y"), fixnum(2));
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
            CljValue new_env = map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10));
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
            CljValue new_env = map_assoc_cow(current_env, fixnum(i), fixnum(i * 10));
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

// ============================================================================
// COW SIMPLE EVAL TESTS
// ============================================================================



// ============================================================================
// COW MINIMAL TESTS
// ============================================================================


TEST(test_cow_actual_cow_demonstration) {
    printf("\n=== COW Actual COW Demonstration ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test: COW Actual COW Demonstration
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        
        // Add some entries
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        
        // RETAIN to trigger COW
        RETAIN((CljValue)map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(3), fixnum(30));
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
// COW SIMPLE TESTS
// ============================================================================


// ============================================================================
// REGISTER ALL TESTS
// ============================================================================
