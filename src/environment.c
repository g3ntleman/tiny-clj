/*
 * Environment Management
 *
 * Stack-based CallFrame system for parameter binding (zero heap allocation).
 */

#include <stdlib.h>
#include <string.h>
#include "environment.h"
#include "memory.h"

#define FRAME_MAX_DEPTH 64

static inline bool frame_find_in_frame(CallFrame *cur, ID symbol, ID *out_value) {
    if (!cur->params) return false;
    for (int i = cur->param_count - 1; i >= 0; i--) {
        if (cur->params[i] == symbol) {
            if (out_value) {
                ID enc = cur->values[i];
                *out_value = (enc == NOT_FOUND) ? NOT_FOUND : frame_decode_value(enc);
            }
            return true;
        }
    }
    return false;
}

void frame_init(CallFrame *frame, CallFrame *parent) {
    if (!frame) return;
    frame->parent = parent;
    frame->params = NULL;
    frame->param_count = 0;
}

void frame_set_bindings(CallFrame *frame, CallFrame *parent, ID *params, ID *values, int count) {
    if (!frame || count < 0) return;
    frame_release(frame);
    frame->parent = parent;
    frame->params = params;
    frame->param_count = count;
    for (int i = 0; i < count; i++) {
        ID value = values ? values[i] : NULL;
        RETAIN(value);
        frame->values[i] = frame_encode_value(value);
    }
}

bool frame_lookup(CallFrame *frame, ID symbol, ID *out_value) {
    if (!frame || !symbol) return false;
    CallFrame *current = frame;
    int depth = 0;
#ifdef DEBUG
    CallFrame *visited[FRAME_MAX_DEPTH];
    while (current && depth < FRAME_MAX_DEPTH) {
        for (int i = 0; i < depth; i++)
            if (visited[i] == current) return false;
        visited[depth++] = current;
        if (frame_find_in_frame(current, symbol, out_value)) return true;
        current = current->parent;
    }
#else
    while (current && depth < FRAME_MAX_DEPTH) {
        if (frame_find_in_frame(current, symbol, out_value)) return true;
        current = current->parent;
        depth++;
    }
#endif
    return false;
}

/** Release all values in a call frame. MEMORY_POLICY: caller must RETAIN when storing (frame_set_bindings_init / frame_set_bindings). */
void frame_release(CallFrame *frame) {
    if (!frame) return;
    for (int i = 0; i < frame->param_count; i++) {
        ID value = frame_decode_value(frame->values[i]);
        if (value && !IS_IMMEDIATE(value))
            RELEASE(value);
        frame->values[i] = NULL;
    }
    frame->param_count = 0;
    frame->params = NULL;
}

