/*
 * Environment Management
 * 
 * Functions for managing execution environments (variable bindings).
 * Stack-based environment implementation using CljList of environment maps.
 * 
 * NEW: Stack-based CallFrame system for efficient parameter binding without heap allocation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "environment.h"

#include "map.h"
#include "memory.h"
#include "runtime.h"
#include "kv_macros.h"
#include "symbol.h"
#include "list.h"

extern void agent_debug_log_clear_once(void);

// ============================================================================
// CALL FRAME IMPLEMENTATION (Stack-based, zero heap allocation)
// ============================================================================

void frame_init(CallFrame *frame, CallFrame *parent) {
    if (!frame) return;

    frame->parent = parent;
    frame->param_count = 0;
}

void frame_set_bindings(CallFrame *frame, CallFrame *parent, ID *params, ID *values, int count) {
    if (!frame) return;
    if (count < 0) count = 0;

    frame_release(frame);
    frame->parent = parent;

    frame->param_count = count;
    for (int i = 0; i < count; i++) {
        ID param = params ? params[i] : NULL;
        ID value = values ? values[i] : NULL;
        if (param && !IS_IMMEDIATE(param)) {
            RETAIN((CljObject*)param);
        }
        if (value && !IS_IMMEDIATE(value)) {
            RETAIN((CljObject*)value);
        }
        *frame_param_slot(frame, i) = param;
        *frame_value_slot(frame, i) = frame_encode_value(value);
    }
}

bool frame_lookup(CallFrame *frame, ID symbol, ID *out_value) {
    if (!frame || !symbol) return false;

    // Protection against circular references - track visited frames
    CallFrame *visited[64];  // Max 64 frames deep (should be more than enough)
    int depth = 0;
    CallFrame *current = frame;
    
    // Iterate through frame chain (non-recursive to avoid stack overflow)
    while (current && depth < 64) {
        // Check for circular reference
        for (int i = 0; i < depth; i++) {
            if (visited[i] == current) {
                // Circular reference detected
                return false;
            }
        }
        visited[depth++] = current;

        // Search in current frame
        for (int i = current->param_count - 1; i >= 0; i--) {
            ID bound = *frame_param_slot(current, i);
            bool matches = false;
            if (bound == symbol) {
                matches = true;
            } else if (bound && symbol &&
                       TAG(bound) == CLJ_SYMBOL && TAG(symbol) == CLJ_SYMBOL &&
                       clj_equal(bound, symbol)) {
                matches = true;
            }
            
            if (matches) {
                ID encoded_value = *frame_value_slot(current, i);
                if (out_value) {
                    if (encoded_value == FRAME_NIL_SENTINEL) {
                        *out_value = FRAME_NIL_SENTINEL;
                    } else {
                        *out_value = frame_decode_value(encoded_value);
                    }
                }
                return true;
            }
        }
        
        // Move to parent frame
        current = current->parent;
    }
    
    return false;
}

void frame_release(CallFrame *frame) {
    if (!frame) return;

    for (int i = 0; i < frame->param_count; i++) {
        ID param = *frame_param_slot(frame, i);
        if (param && !IS_IMMEDIATE(param)) {
            RELEASE((CljObject*)param);
        }
        ID value = frame_decode_value(*frame_value_slot(frame, i));
        if (value && !IS_IMMEDIATE(value)) {
            RELEASE((CljObject*)value);
        }
        *frame_param_slot(frame, i) = NULL;
        *frame_value_slot(frame, i) = NULL;
    }

    frame->param_count = 0;
}

// ============================================================================
// LEGACY ENVIRONMENT STACK (Map-based, for cases requiring persistence)
// ============================================================================

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


