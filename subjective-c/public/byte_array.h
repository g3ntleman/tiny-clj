#ifndef SUBJECTIVE_C_BYTE_ARRAY_H
#define SUBJECTIVE_C_BYTE_ARRAY_H

#include "object.h"
#include "value.h"
#include <stdint.h>

typedef struct {
    CljObject base;
    int length;
    uint8_t *data;
} CljByteArray;

static inline CljByteArray* as_byte_array(ID obj) {
    return (CljByteArray*)assert_type((CljObject*)obj, CLJ_BYTE_ARRAY);
}

CljByteArray* make_byte_array(int length);
CljValue make_byte_array_from_bytes(const uint8_t *bytes, int length);
uint8_t byte_array_get(CljValue arr, int index);
void byte_array_set(CljValue arr, int index, uint8_t value);
int byte_array_length(CljValue arr);
CljValue byte_array_clone(CljValue arr);
void byte_array_copy_from(CljValue dest, int dest_offset, const uint8_t *src, int length);
void byte_array_copy_to(CljValue src, int src_offset, uint8_t *dest, int length);
void byte_array_copy(CljValue dest, int dest_offset, CljValue src, int src_offset, int length);
CljValue byte_array_slice(CljValue arr, int offset, int length);
ID byte_array_get_id(CljValue arr, int index);
void byte_array_set_id(CljValue arr, int index, ID value);

#endif
