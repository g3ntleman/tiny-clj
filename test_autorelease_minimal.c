#include "src/object.h"
#include "src/memory.h"
#include "src/memory_profiler.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🧪 Testing AUTORELEASE functionality\n\n");
    
    // Initialize memory profiling
    memory_profiling_init_with_hooks();
    
    printf("1. Testing basic AUTORELEASE:\n");
    {
        WITH_MEMORY_PROFILING({
            // Create objects with AUTORELEASE
            CljObject *int_obj = AUTORELEASE(make_int(42));
            CljObject *float_obj = AUTORELEASE(make_float(3.14));
            
            printf("   Created objects with AUTORELEASE\n");
            printf("   int_obj type: %d\n", int_obj->type);
            printf("   float_obj type: %d\n", float_obj->type);
            
            // Objects should be automatically released when pool pops
        });
    }
    
    printf("\n2. Testing AUTORELEASE vs RELEASE:\n");
    {
        WITH_MEMORY_PROFILING({
            // Mix of AUTORELEASE and manual RELEASE
            CljObject *autoreleased = AUTORELEASE(make_int(100));
            CljObject *manual = make_int(200);
            
            printf("   Autoreleased object type: %d\n", autoreleased->type);
            printf("   Manual object type: %d\n", manual->type);
            
            // Manual object needs explicit release
            RELEASE(manual);
            // Autoreleased object will be released automatically
        });
    }
    
    printf("\n3. Testing nested autorelease pools:\n");
    {
        WITH_MEMORY_PROFILING({
            CljObject *outer = AUTORELEASE(make_int(1));
            printf("   Outer object type: %d\n", outer->type);
            
            // Nested pool
            autorelease_pool_push();
            CljObject *inner = AUTORELEASE(make_int(2));
            printf("   Inner object type: %d\n", inner->type);
            autorelease_pool_pop(); // Inner objects released
            
            printf("   Outer object still valid: type %d\n", outer->type);
            // Outer object will be released when outer pool pops
        });
    }
    
    memory_profiling_cleanup_with_hooks();
    printf("\n✅ AUTORELEASE tests completed\n");
    return 0;
}