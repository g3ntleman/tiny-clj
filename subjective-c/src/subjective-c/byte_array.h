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

// Byte array flags (CljObject.flags) - only meaningful when type == CLJ_BYTE_ARRAY.
// If set, the byte array's payload is externally owned and must NOT be freed via CLJ_FREE(ba->data).
#define CLJ_FLAG_BYTE_ARRAY_EXTERNAL 0x10

// Optional external finalizer for externally owned payloads (e.g., lwIP pbuf).
// Called when the byte array object is released (rc reaches 0).
typedef void (*CljByteArrayExternalFreeFn)(void *ctx);

// Extended byte array object used when CLJ_FLAG_BYTE_ARRAY_EXTERNAL is set.
// The first member is CljByteArray so existing code can treat it as a normal byte array.
typedef struct {
    CljByteArray base_arr;
    void *external_ctx;
    CljByteArrayExternalFreeFn external_free_fn;
} CljByteArrayExternal;

static inline CljByteArray* as_byte_array(ID obj) {
    return (CljByteArray*)assert_type((CljObject*)obj, CLJ_BYTE_ARRAY);
}

CljByteArray* make_byte_array(int length);
CljValue make_byte_array_from_bytes(const uint8_t *bytes, int length);
// Create a zero-copy view over externally owned bytes.
// The returned object will NOT free the payload when released.
CljByteArray* make_byte_array_view(uint8_t *bytes, int length);
// Create a zero-copy view over externally owned bytes, with an optional finalizer callback.
// If free_fn is non-NULL, it will be called with external_ctx when the object is released.
CljByteArray* make_byte_array_external(uint8_t *bytes, int length, void *external_ctx, CljByteArrayExternalFreeFn free_fn);
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
