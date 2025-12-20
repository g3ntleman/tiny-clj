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
#include "symbol.h"
#include "list.h"

// ============================================================================
// CALL FRAME IMPLEMENTATION (Stack-based, zero heap allocation)
// ============================================================================

void frame_init(CallFrame *frame, CallFrame *parent) {
    if (!frame) return;

    frame->parent = parent;
    frame->params = NULL;
    frame->param_count = 0;
}

void frame_set_bindings(CallFrame *frame, CallFrame *parent, ID *params, ID *values, int count) {
    if (!frame) return;
    if (count < 0) count = 0;

    frame_release(frame);
    frame->parent = parent;
    frame->params = params;  // Only copy pointer (symbols are singletons)
    frame->param_count = count;
    
    for (int i = 0; i < count; i++) {
        ID value = values ? values[i] : NULL;
        RETAIN(value);
        frame->values[i] = frame_encode_value(value);
    }
}

bool frame_lookup(CallFrame *frame, ID symbol, ID *out_value) {
    if (!frame || !symbol) return false;

#ifdef DEBUG
    // Protection against circular references - track visited frames (DEBUG only)
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
        if (current->params) {
            for (int i = current->param_count - 1; i >= 0; i--) {
                ID bound = current->params[i];
                // Symbols are always interned - pointer comparison is sufficient
                if (bound == symbol) {
                    ID encoded_value = current->values[i];
                    if (out_value) {
                        if (encoded_value == NOT_FOUND) {
                            *out_value = NOT_FOUND;
                        } else {
                            *out_value = frame_decode_value(encoded_value);
                        }
                    }
                    return true;
                }
            }
        }
        
        // Move to parent frame
        current = current->parent;
    }
#else
    // Release build: fast path without circular reference checking
    // Simple iterative parent chain traversal
    CallFrame *current = frame;
    int depth = 0;
    const int MAX_DEPTH = 64;  // Safety limit to prevent infinite loops
    
    while (current && depth < MAX_DEPTH) {
        // Search in current frame (backwards scan for shadowing semantics)
        if (current->params) {
            for (int i = current->param_count - 1; i >= 0; i--) {
                ID bound = current->params[i];
                // Symbols are always interned - pointer comparison is sufficient
                if (bound == symbol) {
                    ID encoded_value = current->values[i];
                    if (out_value) {
                        if (encoded_value == NOT_FOUND) {
                            *out_value = NOT_FOUND;
                        } else {
                            *out_value = frame_decode_value(encoded_value);
                        }
                    }
                    return true;
                }
            }
        }
        
        // Move to parent frame
        current = current->parent;
        depth++;
    }
#endif
    
    return false;
}

void frame_release(CallFrame *frame) {
    if (!frame) return;

    for (int i = 0; i < frame->param_count; i++) {
        ID value = frame_decode_value(frame->values[i]);
        if (value && !IS_IMMEDIATE(value)) {
            RELEASE((CljObject*)value);
        }
        frame->values[i] = NULL;
    }
    frame->param_count = 0;
    frame->params = NULL;
}

