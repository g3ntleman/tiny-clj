#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include "object.h"
#include "value.h"
#include "map.h"
#include "list.h"
#include "symbol.h"

// Maximum parameters per function (eliminates __chkstk_darwin calls)
#define CALLFRAME_MAX_PARAMS 16

typedef struct CallFrame {
    struct CallFrame *parent;  // Parent frame (for nested calls)
    ID *params;                // Pointer to params array (borrowed, no RETAIN)
    int param_count;           // Number of active bindings
    ID values[CALLFRAME_MAX_PARAMS];  // Fixed size to avoid dynamic stack allocation
} CallFrame;

static inline ID frame_encode_value(ID value) {
    return value ? value : NOT_FOUND;
}

static inline ID frame_decode_value(ID value) {
    return value == NOT_FOUND ? NULL : value;
}

// Compatibility: Returns sizeof(CallFrame) since size is now fixed
static inline size_t frame_allocation_size(int capacity) {
    (void)capacity;  // No longer used, size is fixed
    return sizeof(CallFrame);
}

// Frame management functions
/** Initialize an empty frame (caller allocates memory via alloca) */
void frame_init(CallFrame *frame, CallFrame *parent);

/** Replace frame contents with provided bindings (copies pointers, retains values) */
void frame_set_bindings(CallFrame *frame, CallFrame *parent, ID *params, ID *values, int count);

// Fast path for initializing a fresh frame (assumes no active bindings to release).
// Copies pointers and retains values.
static inline void frame_set_bindings_init(CallFrame *frame, CallFrame *parent, ID *params, ID *values, int count) {
    if (!frame) return;
    if (count < 0) count = 0;

    frame->parent = parent;
    frame->params = params;  // Borrowed pointer
    frame->param_count = count;

    for (int i = 0; i < count; i++) {
        ID value = values ? values[i] : NULL;
        RETAIN(value);
        frame->values[i] = frame_encode_value(value);
    }
}

/** Look up a symbol in the frame chain (returns true if binding exists, even if value is nil) */
bool frame_lookup(CallFrame *frame, ID symbol, ID *out_value);

// O(1) lexical addressing: get (depth, slot) value from frame chain.
// Returns NOT_FOUND only when out-of-range; otherwise returns decoded value (may be NULL for nil).
static inline ID frame_get_slot(CallFrame *frame, uint8_t depth, uint8_t slot) {
    CallFrame *cur = frame;
    while (cur && depth > 0) {
        cur = cur->parent;
        depth--;
    }
    if (!cur) return NOT_FOUND;
    if (slot >= (uint8_t)cur->param_count) return NOT_FOUND;
    return frame_decode_value(cur->values[slot]);
}

/** Release all values in a frame (for cleanup) */
void frame_release(CallFrame *frame);

/** Release all values in a frame except one retained result that stays with caller. */
void frame_release_except(CallFrame *frame, ID keep);

#endif
