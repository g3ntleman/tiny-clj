/*
 * Memory Profiling Demo
 * 
 * Demonstrates memory profiling in action
 */

#include "src/memory_profiler.h"
#include "src/CljObject.h"
#include "src/clj_symbols.h"
#include <stdio.h>

int main(void) {
    printf("=== Memory Profiling Demo ===\n");
    
    // Initialize memory profiler
    MEMORY_PROFILER_INIT();
    
    // Initialize symbol table
    init_special_symbols();
    
    printf("\n--- Test 1: Basic Object Creation ---\n");
    MEMORY_TEST_START("Basic Object Creation");
    
    CljObject *obj1 = make_int(42);
    CljObject *obj2 = make_float(3.14);
    CljObject *obj3 = make_string("hello");
    
    printf("Created objects: %p, %p, %p\n", obj1, obj2, obj3);
    
    release(obj1);
    release(obj2);
    release(obj3);
    
    MEMORY_TEST_END("Basic Object Creation");
    
    printf("\n--- Test 2: Multiple Object Creation ---\n");
    MEMORY_TEST_START("Multiple Object Creation");
    
    CljObject *obj4 = make_int(200);
    CljObject *obj5 = make_int(300);
    printf("Created more objects: %p, %p\n", obj4, obj5);
    
    release(obj4);
    release(obj5);
    
    MEMORY_TEST_END("Multiple Object Creation");
    
    printf("\n--- Test 3: Reference Counting ---\n");
    MEMORY_TEST_START("Reference Counting");
    
    CljObject *obj = make_int(100);
    printf("Created object: %p\n", obj);
    
    retain(obj);
    printf("Retained object\n");
    
    release(obj);
    printf("Released object (should still exist)\n");
    
    release(obj);
    printf("Released object again (should be freed)\n");
    
    MEMORY_TEST_END("Reference Counting");
    
    // Final cleanup
    MEMORY_PROFILER_CLEANUP();
    
    return 0;
}
