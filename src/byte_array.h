#ifndef TINY_CLJ_BYTE_ARRAY_H
#define TINY_CLJ_BYTE_ARRAY_H

#include "object.h"
#include "value.h"
#include <stdint.h>

// CljByteArray struct definition
typedef struct {
    CljObject base;
    int length;
    uint8_t *data;
} CljByteArray;

// Type-safe casting
static inline CljByteArray* as_byte_array(ID obj) {
    return (CljByteArray*)assert_type((CljObject*)obj, CLJ_BYTE_ARRAY);
}

// ============================================================================
// BASIC OPERATIONS
// ============================================================================

/** Create a byte array with given length (zero-initialized) */
CljValue make_byte_array(int length);

/** Create a byte array from existing byte data (copies data) */
CljValue make_byte_array_from_bytes(const uint8_t *bytes, int length);

/** Get byte at index (bounds-checked) */
uint8_t byte_array_get(CljValue arr, int index);

/** Set byte at index (bounds-checked) */
void byte_array_set(CljValue arr, int index, uint8_t value);

/** Get length of byte array */
int byte_array_length(CljValue arr);

/** Clone byte array (creates new array with copied data) */
CljValue byte_array_clone(CljValue arr);

// ============================================================================
// BULK OPERATIONS (efficient for network streams)
// ============================================================================

/** Copy from C byte array to byte array */
void byte_array_copy_from(CljValue dest, int dest_offset, const uint8_t *src, int length);

/** Copy from byte array to C byte array */
void byte_array_copy_to(CljValue src, int src_offset, uint8_t *dest, int length);

/** Copy between two byte arrays */
void byte_array_copy(CljValue dest, int dest_offset, CljValue src, int src_offset, int length);

/** Create new byte array with copy of a slice */
CljValue byte_array_slice(CljValue arr, int offset, int length);

// ============================================================================
// ID/POINTER OPERATIONS (for serialization)
// ============================================================================

/** Read 32-bit ID at byte index (4 bytes, bounds-checked) */
ID byte_array_get_id(CljValue arr, int index);

/** Write 32-bit ID at byte index (4 bytes, bounds-checked) */
void byte_array_set_id(CljValue arr, int index, ID value);

#endif

