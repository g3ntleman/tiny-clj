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

/** @brief Create byte array with specified length
 * @param length Array length in bytes
 * @return New byte array
 */
CljByteArray* make_byte_array(int length);

/** @brief Create byte array from bytes (copies data)
 * @param bytes Source bytes
 * @param length Number of bytes
 * @return New byte array (as CljValue)
 */
CljValue make_byte_array_from_bytes(const uint8_t *bytes, int length);

/** @brief Create zero-copy view over external bytes
 * @param bytes External bytes (not freed when released)
 * @param length Number of bytes
 * @return Byte array view
 */
CljByteArray* make_byte_array_view(uint8_t *bytes, int length);

/** @brief Create zero-copy view with finalizer callback
 * @param bytes External bytes
 * @param length Number of bytes
 * @param external_ctx Context for finalizer
 * @param free_fn Finalizer called on release (can be NULL)
 * @return Byte array view
 */
CljByteArray* make_byte_array_external(uint8_t *bytes, int length, void *external_ctx, CljByteArrayExternalFreeFn free_fn);

/** @brief Get byte at index
 * @param arr Byte array
 * @param index Index to access
 * @return Byte value
 */
uint8_t byte_array_get(CljValue arr, int index);

/** @brief Set byte at index
 * @param arr Byte array
 * @param index Index to set
 * @param value New byte value
 */
void byte_array_set(CljValue arr, int index, uint8_t value);

/** @brief Get byte array length
 * @param arr Byte array
 * @return Length in bytes
 */
int byte_array_length(CljValue arr);

/** @brief Clone byte array
 * @param arr Source array
 * @return New copy
 */
CljValue byte_array_clone(CljValue arr);

/** @brief Copy bytes from C array to byte array
 * @param dest Destination byte array
 * @param dest_offset Offset in destination
 * @param src Source bytes
 * @param length Number of bytes to copy
 */
void byte_array_copy_from(CljValue dest, int dest_offset, const uint8_t *src, int length);

/** @brief Copy bytes from byte array to C array
 * @param src Source byte array
 * @param src_offset Offset in source
 * @param dest Destination buffer
 * @param length Number of bytes to copy
 */
void byte_array_copy_to(CljValue src, int src_offset, uint8_t *dest, int length);

/** @brief Copy bytes between byte arrays
 * @param dest Destination array
 * @param dest_offset Offset in destination
 * @param src Source array
 * @param src_offset Offset in source
 * @param length Number of bytes to copy
 */
void byte_array_copy(CljValue dest, int dest_offset, CljValue src, int src_offset, int length);

/** @brief Create slice of byte array
 * @param arr Source array
 * @param offset Start offset
 * @param length Slice length
 * @return New array slice
 */
CljValue byte_array_slice(CljValue arr, int offset, int length);

/** @brief Get byte as ID
 * @param arr Byte array
 * @param index Index to access
 * @return Byte value as ID
 */
ID byte_array_get_id(CljValue arr, int index);

/** @brief Set byte from ID
 * @param arr Byte array
 * @param index Index to set
 * @param value Value as ID
 */
void byte_array_set_id(CljValue arr, int index, ID value);

#endif
