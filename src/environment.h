#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include "object.h"
#include "value.h"
#include "map.h"  // Includes FRAME_NIL_SENTINEL definition
#include "list.h"
#include "symbol.h"
#include "kv_macros.h"  // For KV_KEY, KV_VALUE macros

typedef struct CallFrame {
    struct CallFrame *parent;  // Parent frame (for nested calls)
    int param_count;           // Number of active bindings
    ID entries[];              // Layout: [param0, value0, param1, value1, ...]
} CallFrame;

static inline ID frame_encode_value(ID value) {
    return value ? value : FRAME_NIL_SENTINEL;
}

static inline ID frame_decode_value(ID value) {
    return value == FRAME_NIL_SENTINEL ? NULL : value;
}

static inline size_t frame_allocation_size(int capacity) {
    if (capacity < 0) capacity = 0;
    return sizeof(CallFrame) + (size_t)capacity * 2 * sizeof(ID);
}

static inline ID* frame_param_slot(CallFrame *frame, int index) {
    return &KV_KEY(frame->entries, index);
}

static inline ID* frame_value_slot(CallFrame *frame, int index) {
    return &KV_VALUE(frame->entries, index);
}

// Frame management functions
/** Initialize an empty frame (caller allocates memory via alloca) */
void frame_init(CallFrame *frame, CallFrame *parent);

/** Replace frame contents with provided bindings (copies pointers, retains values) */
void frame_set_bindings(CallFrame *frame, CallFrame *parent, ID *params, ID *values, int count);

/** Look up a symbol in the frame chain (returns true if binding exists, even if value is nil) */
bool frame_lookup(CallFrame *frame, ID symbol, ID *out_value);

/** Release all values in a frame (for cleanup) */
void frame_release(CallFrame *frame);

// Legacy environment helpers (still used for cases requiring persistent maps)
/** Create new environment stack with param/value bindings (idiomatic CljList of maps). */
CljList* env_extend_stack(CljList *parent_stack, ID *params, ID *values, int count);

#endif
