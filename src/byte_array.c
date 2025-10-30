#include "exception.h"
#include "byte_array.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Empty byte-array singleton: CLJ_BYTE_ARRAY with rc=0, statically initialized
static struct {
    CljByteArray ba;
} clj_empty_byte_array_singleton_data = {
    .ba = {
        .base = { .type = CLJ_BYTE_ARRAY, .rc = 0 },
        .length = 0,
        .data = NULL
    }
};
static CljByteArray *clj_empty_byte_array_singleton = &clj_empty_byte_array_singleton_data.ba;

// ============================================================================
// BASIC OPERATIONS
// ============================================================================

CljValue make_byte_array(int length) {
    assert(length >= 0 && "byte_array length must be non-negative");
    
    if (length < 0) {
        throw_exception_formatted("IllegalArgumentException", __FILE__, __LINE__, 0,
                "byte-array length must be non-negative, got %d", length);
        return NULL;
    }
    
    CljByteArray *ba = ALLOC(CljByteArray, 1);
    if (!ba) {
        throw_oom(CLJ_BYTE_ARRAY);
    }
    
    ba->base.type = CLJ_BYTE_ARRAY;
    ba->base.rc = 1;
    ba->length = length;
    
    if (length > 0) {
        ba->data = (uint8_t*)calloc(length, sizeof(uint8_t));
        if (!ba->data) {
            free(ba);
            throw_oom(CLJ_BYTE_ARRAY);
        }
    } else {
        ba->data = NULL;
    }
    
    return (CljValue)ba;
}

CljValue make_byte_array_from_bytes(const uint8_t *bytes, int length) {
    assert(bytes != NULL && "bytes must not be NULL");
    assert(length >= 0 && "length must be non-negative");
    
    if (!bytes || length < 0) {
        return (CljValue)clj_empty_byte_array_singleton;
    }
    
    CljValue arr = make_byte_array(length);
    
    if (length > 0) {
        CljByteArray *ba = as_byte_array(arr);
        memcpy(ba->data, bytes, length);
    }
    
    return arr;
}

uint8_t byte_array_get(CljValue arr, int index) {
    assert(arr != NULL && "byte array must not be NULL");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    assert(index >= 0 && index < ba->length && "Index out of bounds");
    
    if (index < 0 || index >= ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Index %d out of bounds for byte array of length %d", index, ba->length);
        return 0;
    }
    
    return ba->data[index];
}

void byte_array_set(CljValue arr, int index, uint8_t value) {
    assert(arr != NULL && "byte array must not be NULL");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    assert(index >= 0 && index < ba->length && "Index out of bounds");
    
    if (index < 0 || index >= ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Index %d out of bounds for byte array of length %d", index, ba->length);
        return;
    }
    
    ba->data[index] = value;
}

int byte_array_length(CljValue arr) {
    assert(arr != NULL && "byte array must not be NULL");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    
    return ba->length;
}

CljValue byte_array_clone(CljValue arr) {
    assert(arr != NULL && "byte array must not be NULL");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    
    return make_byte_array_from_bytes(ba->data, ba->length);
}

// ============================================================================
// BULK OPERATIONS
// ============================================================================

void byte_array_copy_from(CljValue dest, int dest_offset, const uint8_t *src, int length) {
    assert(dest != NULL && "destination byte array must not be NULL");
    assert(src != NULL && "source bytes must not be NULL");
    assert(dest_offset >= 0 && "destination offset must be non-negative");
    assert(length >= 0 && "length must be non-negative");
    
    CljByteArray *ba = as_byte_array(dest);
    assert(ba != NULL && "Invalid byte array");
    assert(dest_offset + length <= ba->length && "Copy would exceed array bounds");
    
    if (!src || dest_offset < 0 || length < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Invalid arguments to byte_array_copy_from",
                       __FILE__, __LINE__, 0);
        return;
    }
    
    if (dest_offset + length > ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Copy from offset %d with length %d exceeds array length %d",
                dest_offset, length, ba->length);
        return;
    }
    
    if (length > 0) {
        memcpy(ba->data + dest_offset, src, length);
    }
}

void byte_array_copy_to(CljValue src, int src_offset, uint8_t *dest, int length) {
    assert(src != NULL && "source byte array must not be NULL");
    assert(dest != NULL && "destination bytes must not be NULL");
    assert(src_offset >= 0 && "source offset must be non-negative");
    assert(length >= 0 && "length must be non-negative");
    
    CljByteArray *ba = as_byte_array(src);
    assert(ba != NULL && "Invalid byte array");
    assert(src_offset + length <= ba->length && "Copy would exceed array bounds");
    
    if (!dest || src_offset < 0 || length < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Invalid arguments to byte_array_copy_to",
                       __FILE__, __LINE__, 0);
        return;
    }
    
    if (src_offset + length > ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Copy from offset %d with length %d exceeds array length %d",
                src_offset, length, ba->length);
        return;
    }
    
    if (length > 0) {
        memcpy(dest, ba->data + src_offset, length);
    }
}

void byte_array_copy(CljValue dest, int dest_offset, CljValue src, int src_offset, int length) {
    assert(dest != NULL && "destination byte array must not be NULL");
    assert(src != NULL && "source byte array must not be NULL");
    assert(dest_offset >= 0 && "destination offset must be non-negative");
    assert(src_offset >= 0 && "source offset must be non-negative");
    assert(length >= 0 && "length must be non-negative");
    
    CljByteArray *dest_ba = as_byte_array(dest);
    CljByteArray *src_ba = as_byte_array(src);
    
    assert(dest_ba != NULL && "Invalid destination byte array");
    assert(src_ba != NULL && "Invalid source byte array");
    assert(dest_offset + length <= dest_ba->length && "Copy would exceed destination bounds");
    assert(src_offset + length <= src_ba->length && "Copy would exceed source bounds");
    
    if (dest_offset < 0 || src_offset < 0 || length < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Invalid arguments to byte_array_copy",
                       __FILE__, __LINE__, 0);
        return;
    }
    
    if (dest_offset + length > dest_ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Copy to offset %d with length %d exceeds destination length %d",
                dest_offset, length, dest_ba->length);
        return;
    }
    
    if (src_offset + length > src_ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Copy from offset %d with length %d exceeds source length %d",
                src_offset, length, src_ba->length);
        return;
    }
    
    if (length > 0) {
        memmove(dest_ba->data + dest_offset, src_ba->data + src_offset, length);
    }
}

CljValue byte_array_slice(CljValue arr, int offset, int length) {
    assert(arr != NULL && "byte array must not be NULL");
    assert(offset >= 0 && "offset must be non-negative");
    assert(length >= 0 && "length must be non-negative");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    assert(offset + length <= ba->length && "Slice would exceed array bounds");
    
    if (offset < 0 || length < 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "Invalid arguments to byte_array_slice",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    if (offset + length > ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "Slice from offset %d with length %d exceeds array length %d",
                offset, length, ba->length);
        return NULL;
    }
    
    return make_byte_array_from_bytes(ba->data + offset, length);
}

// ============================================================================
// ID/POINTER OPERATIONS
// ============================================================================

ID byte_array_get_id(CljValue arr, int index) {
    assert(arr != NULL && "byte array must not be NULL");
    assert(index >= 0 && "index must be non-negative");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    assert(index + sizeof(ID) <= (size_t)ba->length && "ID read would exceed array bounds");
    
    if (index < 0 || index + sizeof(ID) > (size_t)ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "ID read at index %d (size %zu) exceeds array length %d",
                index, sizeof(ID), ba->length);
        return NULL;
    }
    
    ID value;
    memcpy(&value, ba->data + index, sizeof(ID));
    return value;
}

void byte_array_set_id(CljValue arr, int index, ID value) {
    assert(arr != NULL && "byte array must not be NULL");
    assert(index >= 0 && "index must be non-negative");
    
    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    assert(index + sizeof(ID) <= (size_t)ba->length && "ID write would exceed array bounds");
    
    if (index < 0 || index + sizeof(ID) > (size_t)ba->length) {
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "ID write at index %d (size %zu) exceeds array length %d",
                index, sizeof(ID), ba->length);
        return;
    }
    
    memcpy(ba->data + index, &value, sizeof(ID));
}

