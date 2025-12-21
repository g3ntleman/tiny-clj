#ifndef TINY_CLJ_ENVIRONMENT_H
#define TINY_CLJ_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include "object.h"
#include "value.h"
#include "map.h"
#include "list.h"
#include "symbol.h"
#include "vector.h"

// Maximum parameters per function (eliminates __chkstk_darwin calls)
#define CALLFRAME_MAX_PARAMS 16

typedef struct CallFrame {
    struct CallFrame *parent;  // Parent frame (for nested calls)
    CljVector *params;         // Direct Vector (borrowed, no RETAIN) or NULL for let bindings
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

/** Replace frame contents with provided bindings (params_vec provides both symbols and count) */
void frame_set_bindings(CallFrame *frame, CallFrame *parent, CljVector *params_vec, ID *values);

/** Look up a symbol in the frame chain (returns true if binding exists, even if value is nil) */
bool frame_lookup(CallFrame *frame, ID symbol, ID *out_value);

/** Release all values in a frame (for cleanup) */
void frame_release(CallFrame *frame);

#endif
