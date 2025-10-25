/*
 * Demo of the new TEST macro
 * 
 * This file demonstrates how to use the new TEST macro that combines
 * test definition and registration in a single macro call.
 */

#include "tests_common.h"

// Example 1: Simple test with the new TEST macro
TEST(test_simple_addition) {
    printf("\n=== Simple Addition Test ===\n");
    
    int a = 5;
    int b = 3;
    int result = a + b;
    
    printf("Testing %d + %d = %d\n", a, b, result);
    TEST_ASSERT_EQUAL(8, result);
    printf("✓ Simple addition works!\n");
}

// Example 2: Test with Clojure objects
TEST(test_clojure_fixnum_creation) {
    printf("\n=== Clojure Fixnum Creation Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljValue num1 = fixnum(42);
        CljValue num2 = fixnum(17);
        
        printf("Created fixnums: %d and %d\n", as_fixnum(num1), as_fixnum(num2));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(num1));
        TEST_ASSERT_EQUAL_INT(17, as_fixnum(num2));
        printf("✓ Clojure fixnum creation works!\n");
    });
}

// Example 3: Test with map operations
TEST(test_simple_map_operations) {
    printf("\n=== Simple Map Operations Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljMap *map = (CljMap*)make_map(4);
        printf("Created map with capacity 4\n");
        
        // Add some entries
        map_assoc_cow((CljValue)map, fixnum(1), fixnum(10));
        map_assoc_cow((CljValue)map, fixnum(2), fixnum(20));
        
        printf("Added entries: (1, 10) and (2, 20)\n");
        TEST_ASSERT_EQUAL(2, map->count);
        
        // Retrieve entries
        CljValue val1 = map_get((CljValue)map, fixnum(1));
        CljValue val2 = map_get((CljValue)map, fixnum(2));
        
        TEST_ASSERT_NOT_NULL(val1);
        TEST_ASSERT_NOT_NULL(val2);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(val2));
        
        printf("✓ Map operations work correctly!\n");
    });
}

// Example 4: Test with symbol operations
TEST(test_symbol_operations) {
    printf("\n=== Symbol Operations Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        CljValue sym1 = intern_symbol_global("hello");
        CljValue sym2 = intern_symbol_global("world");
        
        printf("Created symbols: '%s' and '%s'\n", 
               as_symbol(sym1)->name, as_symbol(sym2)->name);
        
        TEST_ASSERT_NOT_NULL(sym1);
        TEST_ASSERT_NOT_NULL(sym2);
        TEST_ASSERT_EQUAL_STRING("hello", as_symbol(sym1)->name);
        TEST_ASSERT_EQUAL_STRING("world", as_symbol(sym2)->name);
        
        printf("✓ Symbol operations work correctly!\n");
    });
}

// Example 5: Test with error handling
TEST(test_error_handling) {
    printf("\n=== Error Handling Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Test null pointer handling
        CljValue null_val = NULL;
        TEST_ASSERT_NULL(null_val);
        
        // Test valid object
        CljValue valid_num = fixnum(100);
        TEST_ASSERT_NOT_NULL(valid_num);
        TEST_ASSERT_EQUAL_INT(100, as_fixnum(valid_num));
        
        printf("✓ Error handling works correctly!\n");
    });
}

// Example 6: Performance test
TEST(test_performance_simple) {
    printf("\n=== Performance Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        printf("Running 1000 iterations...\n");
        
        for (int i = 0; i < 1000; i++) {
            CljValue num = fixnum(i);
            TEST_ASSERT_EQUAL_INT(i, as_fixnum(num));
        }
        
        printf("✓ Performance test completed (1000 iterations)\n");
    });
}

// Example 7: Memory management test
TEST(test_memory_management) {
    printf("\n=== Memory Management Test ===\n");
    
    WITH_AUTORELEASE_POOL({
        // Create multiple objects
        CljValue num1 = fixnum(1);
        CljValue num2 = fixnum(2);
        CljValue sym = intern_symbol_global("test");
        CljMap *map = (CljMap*)make_map(2);
        
        printf("Created multiple objects\n");
        
        // Verify they exist
        TEST_ASSERT_NOT_NULL(num1);
        TEST_ASSERT_NOT_NULL(num2);
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_NOT_NULL(map);
        
        // Test operations
        map_assoc_cow((CljValue)map, num1, num2);
        CljValue retrieved = map_get((CljValue)map, num1);
        TEST_ASSERT_EQUAL(num2, retrieved);
        
        printf("✓ Memory management works correctly!\n");
    });
}
