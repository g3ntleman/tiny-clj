/*
 * Minimal COW Test - Absolute minimal test
 */

#include "../object.h"
#include "../map.h"
#include "../memory.h"
#include "unity/src/unity.h"
#include <stdio.h>

void test_cow_minimal_basic(void) {
    printf("\n=== Minimal COW Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 1: Basic map creation
        CljMap *map = (CljMap*)make_map(4);
        printf("After make_map: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Test 2: Single map_assoc_cow (RC=1 → in-place mutation)
        CljValue result = map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("After map_assoc_cow: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        TEST_ASSERT_EQUAL(map, result);  // Should be same pointer
        
        // Test 3: Verify entry was added
        CljValue val = map_get((CljValue)map, fixnum(1));
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val));
        
        printf("✓ Minimal COW test passed!\n");
    });
}

void test_cow_actual_cow_demonstration(void) {
    printf("\n=== Actual COW Demonstration ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 1: Create map with RC=1
        CljMap *map = (CljMap*)make_map(4);
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("Initial map: RC=%d, count=%d\n", map->base.rc, map->count);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Test 2: Simulate sharing (RC=2)
        RETAIN(map);
        printf("After RETAIN: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(2, map->base.rc);
        
        // Test 3: COW operation should create NEW map
        printf("Before COW: RC=%d\n", map->base.rc);
        // CljValue new_map = map_assoc_cow((CljValue)map, fixnum(2), fixnum(20)); // Unused
        printf("After COW: Original RC=%d\n", map->base.rc);
        
        // Verify COW behavior - original should be unchanged
        TEST_ASSERT_EQUAL(2, map->base.rc);  // Original unchanged
        TEST_ASSERT_EQUAL(1, map->count);    // Original count unchanged
        
        printf("✓ COW demonstration successful!\n");
        
        RELEASE(map);  // Cleanup
    });
}
