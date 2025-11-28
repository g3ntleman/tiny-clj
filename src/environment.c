/*
 * Environment Management
 * 
 * Functions for managing execution environments (variable bindings).
 * Stack-based environment implementation using CljList of environment maps.
 */

#include <stdlib.h>
#include "environment.h"
#include "object.h"
#include "map.h"
#include "memory.h"
#include "runtime.h"
#include "kv_macros.h"
#include "symbol.h"
#include "list.h"

// Extend environment stack with new parameter bindings
// Returns a new CljList with the new environment map as first element
CljList* env_extend_stack(CljList *parent_stack, ID *params, ID *values, int count) {
    if (count > MAX_FUNCTION_PARAMS) return NULL;
    
    // Count valid parameters
    int param_count = 0;
    for (int i = 0; i < count && i < 16; i++) {
        if (params[i]) {
            param_count++;
        }
    }
    
    // Prepare additions array on stack (max 16 params * 2 = 32 pointers)
    CljObject *additions_stack[32];
    CljObject **additions = additions_stack;
    int addition_idx = 0;
    
    // Build additions array from parameters
    for (int i = 0; i < count && i < 16; i++) {
        if (params[i]) {
            additions[addition_idx * 2] = (CljObject*)params[i];
            additions[addition_idx * 2 + 1] = (CljObject*)values[i];
            addition_idx++;
        }
    }
    
    // Create new environment map with parameter bindings
    CljMap *new_env = map_copy_with_additions(NULL, additions, param_count);
    
    // Create new list with new_env as first element and parent_stack as rest
    CljList *new_stack = make_list((ID)new_env, parent_stack);
    
    return new_stack;
}


