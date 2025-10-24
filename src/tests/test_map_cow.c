/*
 * Copy-on-Write Map Tests
 * 
 * Diese Tests verifizieren die COW-Funktionalität von map_assoc_cow():
 * 1. In-place mutation bei RC=1
 * 2. COW bei RC>1 (mit Sharing-Test)
 * 3. Old Map unverändert nach COW
 * 4. AUTORELEASE funktioniert korrekt mit COW
 * 5. Memory Leak Detection
 */

#include "../object.h"
#include "../map.h"
#include "../memory.h"
#include "../value.h"
#include "unity/src/unity.h"
#include <stdio.h>

// ============================================================================
// TEST 1: In-place mutation bei RC=1
// ============================================================================

void test_cow_inplace_mutation_rc_one(void) {
    printf("\n=== Test 1: In-place mutation bei RC=1 ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        printf("Initial RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Add first entry
        CljValue result1 = map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("After first assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1
        TEST_ASSERT_EQUAL(map, result1); // Gleicher Pointer!
        
        // Add second entry
        CljValue result2 = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        printf("After second assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1
        TEST_ASSERT_EQUAL(map, result2); // Gleicher Pointer!
        
        // Verify entries exist
        CljValue val1 = map_get((CljValue)map, fixnum(1));
        CljValue val2 = map_get((CljValue)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        printf("✓ In-place mutation funktioniert bei RC=1\n");
    });
}

// ============================================================================
// TEST 2: COW bei RC>1 (mit Sharing-Test)
// ============================================================================

void test_cow_copy_on_write_rc_greater_one(void) {
    printf("\n=== Test 2: COW bei RC>1 ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        printf("Initial RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Simuliere: Closure hält env (RC=2)
        RETAIN(map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Jetzt sollte COW triggern
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        printf("After COW assoc: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);  // Original RC bleibt 2
        TEST_ASSERT_NOT_EQUAL(map, new_map); // NEUER Pointer!
        
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
        
        RELEASE(map);  // Cleanup
    });
}

// ============================================================================
// TEST 3: Old Map unverändert nach COW
// ============================================================================

void test_cow_original_map_unchanged(void) {
    printf("\n=== Test 3: Original Map unverändert nach COW ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        map_assoc((CljValue)map, fixnum(2), fixnum(20));
        printf("Original map count=%d\n", map->count);
        TEST_ASSERT_EQUAL(2, map->count);
        
        // Simuliere sharing
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // COW operation
        CljValue new_map = map_assoc_cow((CljValue)map, fixnum(3), fixnum(30));
        
        // Original map should be unchanged
        TEST_ASSERT_EQUAL(2, map->count);  // Count unchanged
        TEST_ASSERT_EQUAL(2, map->base.rc);  // RC unchanged
        
        // Original should only have keys 1,2
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map, fixnum(1)));
        TEST_ASSERT_NOT_NULL(map_get((CljValue)map, fixnum(2)));
        TEST_ASSERT_NULL(map_get((CljValue)map, fixnum(3)));
        
        // New map should have all keys 1,2,3
        CljMap *new_map_data = as_map(new_map);
        TEST_ASSERT_EQUAL(3, new_map_data->count);
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(1)));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(2)));
        TEST_ASSERT_NOT_NULL(map_get(new_map, fixnum(3)));
        
        printf("✓ Original Map bleibt unverändert nach COW\n");
        
        RELEASE(map);  // Cleanup
    });
}

// ============================================================================
// TEST 4: AUTORELEASE funktioniert korrekt mit COW
// ============================================================================

void test_cow_with_autorelease(void) {
    printf("\n=== Test 4: AUTORELEASE mit COW ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        map_assoc((CljValue)map, fixnum(1), fixnum(10));
        printf("Initial RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // AUTORELEASE sollte RC nicht erhöhen
        CljMap *same = (CljMap*)AUTORELEASE((CljValue)map);
        printf("After AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1!
        TEST_ASSERT_EQUAL_PTR(map, same);
        
        // Jetzt sollte in-place mutation funktionieren
        CljValue result = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        printf("After COW with AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1
        TEST_ASSERT_EQUAL(map, result);  // Gleicher Pointer!
        
        // Verify entry added
        CljValue val2 = map_get((CljValue)map, fixnum(2));
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        printf("✓ AUTORELEASE funktioniert korrekt mit COW\n");
    });
}

// ============================================================================
// TEST 5: Memory Leak Detection
// ============================================================================

void test_cow_memory_leak_detection(void) {
    printf("\n=== Test 5: Memory Leak Detection ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Create map with multiple entries
        CljMap *map = (CljMap*)make_map(4);
        for (int i = 0; i < 5; i++) {
            map_assoc((CljValue)map, fixnum(i), fixnum(i * 10));
        }
        printf("Created map with %d entries\n", map->count);
        
        // Simulate sharing scenario
        RETAIN(map);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Multiple COW operations
        CljValue new_map1 = map_assoc_cow((CljValue)map, fixnum(5), fixnum(50));
        CljValue new_map2 = map_assoc_cow((CljValue)map, fixnum(6), fixnum(60));
        
        // Verify all maps are valid
        TEST_ASSERT_NOT_NULL(new_map1);
        TEST_ASSERT_NOT_NULL(new_map2);
        TEST_ASSERT_NOT_EQUAL(map, new_map1);
        TEST_ASSERT_NOT_EQUAL(map, new_map2);
        TEST_ASSERT_NOT_EQUAL(new_map1, new_map2);
        
        // Verify entries
        TEST_ASSERT_NOT_NULL(map_get(new_map1, fixnum(5)));
        TEST_ASSERT_NULL(map_get(new_map1, fixnum(6)));
        TEST_ASSERT_NULL(map_get(new_map2, fixnum(5)));
        TEST_ASSERT_NOT_NULL(map_get(new_map2, fixnum(6)));
        
        printf("✓ Keine Memory Leaks bei COW-Operationen\n");
        
        RELEASE(map);  // Cleanup original
    });
}

// ============================================================================
// TEST 6: Performance Simulation
// ============================================================================

void test_cow_performance_simulation(void) {
    printf("\n=== Test 6: Performance Simulation ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *env = (CljMap*)make_map(4);
        printf("Starting performance simulation...\n");
        
        // Simulate loop pattern: env = AUTORELEASE(map_assoc_cow(env, key, value))
        for (int i = 0; i < 100; i++) {
            env = (CljMap*)AUTORELEASE(map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10)));
            
            // RC should stay 1 (in-place optimization)
            TEST_ASSERT_EQUAL(1, env->base.rc);
            
            if (i % 20 == 0) {
                printf("Iteration %d: RC=%d, count=%d\n", i, env->base.rc, env->count);
            }
        }
        
        // Verify final state
        TEST_ASSERT_EQUAL(100, env->count);
        CljValue val50 = map_get((CljValue)env, fixnum(50));
        TEST_ASSERT_NOT_NULL(val50);
        TEST_ASSERT_EQUAL_INT(500, as_fixnum(val50));
        
        printf("✓ Performance simulation erfolgreich (100 Iterationen)\n");
    });
}
