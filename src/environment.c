/*
 * Environment Management
 * 
 * Functions for managing execution environments (variable bindings).
 * Stack-based environment implementation for function calls.
 */

#include <stdlib.h>
#include "environment.h"
#include "object.h"
#include "map.h"
#include "memory.h"
#include "runtime.h"
#include "kv_macros.h"

CljMap* env_extend_stack(CljMap *parent_env, ID *params, ID *values, int count) {
    if (count > MAX_FUNCTION_PARAMS) return NULL;
    
    // OPTIMIZATION: Use map_copy_with_additions to avoid multiple heap allocations
    // Instead of N+M map_assoc calls (each potentially creating a new map),
    // we create a single map with all bindings in one operation.
    
    // Prepare additions array on stack (max 16 params * 2 = 32 pointers)
    CljObject *additions_stack[32];
    CljObject **additions = additions_stack;
    int addition_count = 0;
    
    // Build additions array from parameters
    for (int i = 0; i < count && i < 16; i++) {
        if (params[i]) {
            additions[addition_count * 2] = (CljObject*)params[i];
            additions[addition_count * 2 + 1] = (CljObject*)values[i];
            addition_count++;
        }
    }
    
    // Use optimized copy function (single heap allocation)
    CljMap *new_env = map_copy_with_additions(parent_env, additions, addition_count);
    
    return new_env;
}


