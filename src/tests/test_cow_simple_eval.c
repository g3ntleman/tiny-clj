/*
 * Simple COW Eval Test - Minimal test without symbol dependencies
 */

#include "../object.h"
#include "../map.h"
#include "../memory.h"
#include "unity/src/unity.h"
#include <stdio.h>

void test_cow_simple_eval_loop(void) {
    printf("\n=== Simple COW Eval Loop Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Simuliere Clojure-Code: (loop [env {} i 0] (if (< i 100) (recur (assoc env i (* i 10)) (inc i)) env))
        CljMap *env = (CljMap*)make_map(4);
        printf("Initial env RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(1, env->base.rc);
        
        // Simuliere Loop mit map_assoc_cow
        for (int i = 0; i < 100; i++) {
            // env = AUTORELEASE(map_assoc_cow(env, key, value))
            env = (CljMap*)AUTORELEASE(map_assoc_cow((CljValue)env, fixnum(i), fixnum(i * 10)));
            
            // RC sollte 1 bleiben (in-place optimization)
            TEST_ASSERT_EQUAL(1, env->base.rc);
            
            if (i % 20 == 0) {
                printf("Loop iteration %d: RC=%d, count=%d\n", i, env->base.rc, env->count);
            }
        }
        
        // Verify final state
        TEST_ASSERT_EQUAL(100, env->count);
        CljValue val50 = map_get((CljValue)env, fixnum(50));
        TEST_ASSERT_NOT_NULL(val50);
        TEST_ASSERT_EQUAL_INT(500, as_fixnum(val50));
        
        printf("✓ Simple COW Eval Loop funktioniert (100 Iterationen)\n");
    });
}

void test_cow_simple_eval_closure(void) {
    printf("\n=== Simple COW Eval Closure Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Simuliere Closure-Environment-Sharing
        CljMap *env = (CljMap*)make_map(4);
        map_assoc((CljValue)env, fixnum(1), fixnum(10));
        printf("Initial env RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(1, env->base.rc);
        
        // Simuliere: Closure hält env (RC=2)
        RETAIN(env);
        printf("After RETAIN (closure): RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(2, env->base.rc);
        
        // Closure-Operation sollte COW triggern
        CljValue new_env = map_assoc_cow((CljValue)env, fixnum(2), fixnum(20));
        printf("After COW closure operation: RC=%d\n", env->base.rc);
        TEST_ASSERT_EQUAL(2, env->base.rc);  // Original unverändert
        TEST_ASSERT_NOT_EQUAL(env, new_env); // NEUER Pointer!
        
        // Verify original env unchanged
        CljValue orig_1 = map_get((CljValue)env, fixnum(1));
        CljValue orig_2 = map_get((CljValue)env, fixnum(2));
        TEST_ASSERT_NOT_NULL(orig_1);
        TEST_ASSERT_NULL(orig_2);  // Original hat key=2 nicht
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(orig_1));
        
        // Verify new env has both
        CljValue new_1 = map_get(new_env, fixnum(1));
        CljValue new_2 = map_get(new_env, fixnum(2));
        TEST_ASSERT_NOT_NULL(new_1);
        TEST_ASSERT_NOT_NULL(new_2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(new_1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(new_2));
        
        printf("✓ Simple COW Eval Closure funktioniert\n");
        
        RELEASE(env);  // Cleanup
    });
}
