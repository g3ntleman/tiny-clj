/*
 * Simple COW Test - Minimal test to verify basic map_assoc_cow functionality
 */

#include "../object.h"
#include "../map.h"
#include "../memory.h"
#include "unity/src/unity.h"
#include <stdio.h>

void test_simple_cow_basic(void) {
    printf("\n=== Simple COW Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test 1: Basic map creation
        CljMap *map = (CljMap*)make_map(4);
        printf("After make_map: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        
        // Test 2: In-place mutation bei RC=1
        CljValue result = map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        printf("After map_assoc_cow: RC=%d\n", map->base.rc);
        TEST_ASSERT_EQUAL(1, map->base.rc);
        TEST_ASSERT_EQUAL(map, result);  // Should be same pointer
        
        // Test 3: Verify entry was added
        CljValue val = map_get((CljValue)map, fixnum(1));
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val));
        
        printf("✓ Basic COW functionality works!\n");
    });
}
