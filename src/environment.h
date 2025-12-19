#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include "object.h"
#include "value.h"
#include "map.h"  // Includes FRAME_NIL_SENTINEL definition
#include "list.h"
#include "symbol.h"

typedef struct CallFrame {
    struct CallFrame *parent;  // Parent frame (for nested calls)
    ID *params;                // Pointer to func->params_array (borrowed, no RETAIN)
    int param_count;           // Number of active bindings
    ID values[];               // Only values: [value0, value1, ...]
} CallFrame;

static inline ID frame_encode_value(ID value) {
    return value ? value : FRAME_NIL_SENTINEL;
}

static inline ID frame_decode_value(ID value) {
    return value == FRAME_NIL_SENTINEL ? NULL : value;
}

static inline size_t frame_allocation_size(int capacity) {
    if (capacity < 0) capacity = 0;
    return sizeof(CallFrame) + (size_t)capacity * sizeof(ID);
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

#endif
