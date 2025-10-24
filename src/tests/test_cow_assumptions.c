/*
 * Copy-on-Write Assumptions Tests
 * 
 * Diese Tests verifizieren kritische Annahmen über Reference Counting
 * und AUTORELEASE, bevor wir map_assoc_cow() implementieren.
 * 
 * Getestete Annahmen:
 * 1. AUTORELEASE erhöht RC NICHT
 * 2. RC bleibt 1 bei wiederholten map_assoc Operationen
 * 3. RETAIN erhöht RC
 * 4. Closure-Simulation zeigt RC=2
 * 5. AUTORELEASE + RETAIN Kombination
 */

#include "../object.h"
#include "../map.h"
#include "../memory.h"
#include "../memory_profiler.h"
#include "../symbol.h"
#include "../value.h"
#include "../../external/unity/src/unity.h"
#include <stdio.h>

// Test-Setup und Teardown
// static bool builtins_registered = false; // Unused

void setUp(void) {
    // Keine Initialisierung für direktes RC-Testing
}

void tearDown(void) {
    // Kein Cleanup nötig für direktes RC-Testing
}

// ============================================================================
// TEST 1: AUTORELEASE erhöht RC NICHT
// ============================================================================

void test_autorelease_does_not_increase_rc(void) {
    printf("\n=== Test 1: AUTORELEASE does NOT increase RC ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        printf("After make_map: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC=1 nach make_map
        
        CljMap *same = (CljMap*)AUTORELEASE((CljValue)map);
        printf("After AUTORELEASE: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1!
        TEST_ASSERT_EQUAL_PTR(map, same);    // Gleicher Pointer
        
        printf("✓ AUTORELEASE fügt nur weak reference hinzu, RC bleibt 1\n");
    });
}

// ============================================================================
// TEST 2: RC bleibt 1 bei wiederholten map_assoc Operationen
// ============================================================================

void test_rc_stays_one_in_loop(void) {
    printf("\n=== Test 2: RC stays 1 during repeated map_assoc ===\n");
    
    CljMap *env = (CljMap*)make_map(4);
    printf("Initial RC=%d\n", env->base.rc);
    TEST_ASSERT_EQUAL(1, env->base.rc);
    
    for (int i = 0; i < 10; i++) {
        // Simuliere aktuelles Pattern
        map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
        printf("After map_assoc[%d]: RC=%d\n", i, env->base.rc);
        TEST_ASSERT_EQUAL(1, env->base.rc);  // RC bleibt 1
    }
    
    printf("✓ RC bleibt konstant bei 1 während wiederholten map_assoc\n");
    RELEASE(env);
}

// ============================================================================
// TEST 3: RETAIN erhöht RC korrekt
// ============================================================================

void test_retain_increases_rc(void) {
    printf("\n=== Test 3: RETAIN increases RC ===\n");
    
    CljMap *map = (CljMap*)make_map(4);
    printf("After make_map: RC=%d\n", map->base.rc);
    TEST_ASSERT_EQUAL(1, map->base.rc);
    
    RETAIN(map);
    printf("After RETAIN: RC=%d\n", map->base.rc);
    TEST_ASSERT_EQUAL(2, map->base.rc);  // RC=2 nach RETAIN
    
    RELEASE(map);  // RC=1
    printf("After first RELEASE: RC=%d\n", map->base.rc);
    TEST_ASSERT_EQUAL(1, map->base.rc);
    
    RELEASE(map);  // RC=0, freed
    printf("✓ RETAIN erhöht RC, RELEASE verringert RC\n");
}

// ============================================================================
// TEST 4: Closure-Simulation zeigt RC=2
// ============================================================================

void test_closure_holds_env(void) {
    printf("\n=== Test 4: Closure holds environment (RC=2) ===\n");
    
    CljMap *env = (CljMap*)make_map(4);
    map_assoc((CljValue)env, intern_symbol_global("x"), fixnum(1));
    printf("After make_map and assoc: RC=%d\n", env->base.rc);
    TEST_ASSERT_EQUAL(1, env->base.rc);
    
    // Simuliere: Closure hält env
    RETAIN(env);  // Closure ownership
    printf("After RETAIN (simulating closure): RC=%d\n", env->base.rc);
    TEST_ASSERT_EQUAL(2, env->base.rc);
    
    // Jetzt wäre RC=2, map_assoc_cow würde COW triggern
    printf("✓ Closure-Ownership erhöht RC auf 2 (würde COW triggern)\n");
    
    RELEASE(env);  // Closure release
    RELEASE(env);  // Original release
}

// ============================================================================
// TEST 5: AUTORELEASE mit RETAIN Kombination
// ============================================================================

void test_autorelease_with_retain(void) {
    printf("\n=== Test 5: AUTORELEASE with RETAIN ===\n");
    
    CljMap *map = (CljMap*)make_map(4);
    RETAIN(map);  // RC=2
    printf("After make_map + RETAIN: RC=%d\n", map->base.rc);
    TEST_ASSERT_EQUAL(2, map->base.rc);
    
    CljMap *same = (CljMap*)AUTORELEASE((CljValue)map);
    printf("After AUTORELEASE: RC=%d\n", map->base.rc);
    TEST_ASSERT_EQUAL(2, map->base.rc);  // RC bleibt 2!
    TEST_ASSERT_EQUAL_PTR(map, same);
    
    printf("✓ AUTORELEASE ändert RC nicht, auch bei RC>1\n");
    
    // Pool drain released eine Reference
    // Wir müssen noch eine manuell releasen
    RELEASE(map);
}

// ============================================================================
// TEST 6: Mehrere AUTORELEASE auf gleichem Objekt
// ============================================================================

void test_multiple_autorelease_same_object(void) {
    printf("\n=== Test 6: Multiple AUTORELEASE on same object ===\n");
    
    CljMap *map = (CljMap*)make_map(4);
    printf("Initial RC=%d\n", map->base.rc);
    TEST_ASSERT_EQUAL(1, map->base.rc);
    
    // Mehrmals AUTORELEASE (wie in Loop)
    for (int i = 0; i < 5; i++) {
        CljMap *same = (CljMap*)AUTORELEASE((CljValue)map);
        printf("After AUTORELEASE[%d]: RC=%d\n", i, map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);  // RC bleibt 1!
        TEST_ASSERT_EQUAL_PTR(map, same);
    }
    
    printf("✓ Mehrfaches AUTORELEASE ändert RC nicht\n");
    // Pool drain sollte map einmal freigeben (weak references)
}

// ============================================================================
// TEST 7: AUTORELEASE in Loop (realistische Simulation)
// ============================================================================

void test_autorelease_in_loop_realistic(void) {
    printf("\n=== Test 7: AUTORELEASE in loop (realistic simulation) ===\n");
    
    CljMap *env = (CljMap*)make_map(4);
    printf("Initial RC=%d\n", env->base.rc);
    
    for (int i = 0; i < 100; i++) {
        // Simuliere: env = AUTORELEASE(map_assoc_cow(env, key, value))
        // Ohne COW (noch nicht implementiert), nur RC-Check
        map_assoc((CljValue)env, fixnum(i), fixnum(i * 10));
        env = (CljMap*)AUTORELEASE((CljValue)env);
        
        TEST_ASSERT_EQUAL(1, env->base.rc);  // RC muss 1 bleiben!
        
        if (i % 10 == 0) {
            printf("Iteration %d: RC=%d\n", i, env->base.rc);
        }
    }
    
    printf("✓ RC bleibt 1 auch nach 100 Iterationen mit AUTORELEASE\n");
    // env wird durch pool drain freigegeben
}

// ============================================================================
// MAIN Test Runner
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    printf("\n");
    printf("========================================\n");
    printf("Copy-on-Write Assumptions Tests\n");
    printf("========================================\n");
    printf("Diese Tests verifizieren kritische Annahmen über RC und AUTORELEASE\n");
    printf("vor der Implementierung von map_assoc_cow().\n");
    printf("\n");
    
    RUN_TEST(test_autorelease_does_not_increase_rc);
    RUN_TEST(test_rc_stays_one_in_loop);
    RUN_TEST(test_retain_increases_rc);
    RUN_TEST(test_closure_holds_env);
    RUN_TEST(test_autorelease_with_retain);
    RUN_TEST(test_multiple_autorelease_same_object);
    RUN_TEST(test_autorelease_in_loop_realistic);
    
    return UNITY_END();
}

