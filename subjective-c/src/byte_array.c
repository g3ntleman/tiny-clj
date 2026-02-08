#include "exception.h"
#include "byte_array.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Empty byte-array singleton: CLJ_BYTE_ARRAY with rc=SINGLETON_RC, statically initialized
static struct {
    CljByteArray ba;
} clj_empty_byte_array_singleton_data = {
    .ba = {
        .base = { .type = CLJ_BYTE_ARRAY, .flags = 0, .rc = SINGLETON_RC },
        .length = 0,
        .data = NULL
    }
};
static CljByteArray *clj_empty_byte_array_singleton = &clj_empty_byte_array_singleton_data.ba;

// ============================================================================
// BASIC OPERATIONS
// ============================================================================

/** Allocate byte-array of given length; rc=1, caller releases. */
CljByteArray* make_byte_array(int length) {
    assert(length >= 0 && "byte_array length must be non-negative");

    if (length < 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "byte-array length must be non-negative, got %d", length); return NULL;
    }

    CljByteArray *ba = ALLOC(CljByteArray, 1);
    if (!ba) {
        throw_oom();
    }

    ba->base.type = CLJ_BYTE_ARRAY;
    ba->base.flags = 0;
    ba->length = length;

    if (length > 0) {
        ba->data = (uint8_t*)CLJ_CALLOC(length, sizeof(uint8_t));
        if (!ba->data) {
            CLJ_FREE(ba);
            throw_oom();
        }
    } else {
        ba->data = NULL;
    }

    return ba;
}

CljValue make_byte_array_from_bytes(const uint8_t *bytes, int length) {
    assert(bytes != NULL && "bytes must not be NULL");
    assert(length >= 0 && "length must be non-negative");

    if (!bytes || length < 0) {
        return clj_empty_byte_array_singleton;
    }

    CljByteArray* arr = make_byte_array(length);

    if (length > 0) {
        CljByteArray *ba = as_byte_array(arr);
        memcpy(ba->data, bytes, length);
    }

    return arr;
}

/** Wrap existing bytes without copying; marks array as external view. */
CljByteArray* make_byte_array_view(uint8_t *bytes, int length) {
    assert(length >= 0 && "byte_array view length must be non-negative");
    if (length < 0) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "byte-array view length must be non-negative, got %d", length); return NULL;
    }
    if (length == 0) {
        return clj_empty_byte_array_singleton;
    }
    if (!bytes) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "byte-array view requires non-NULL bytes when length=%d", length); return NULL;
    }

    // Allocate as a CLJ_BYTE_ARRAY so the memory profiler / type tracking stays consistent.
    // We intentionally avoid ALLOC(CljByteArrayView, ...) because TYPE_OF() is not defined for it.
    CljByteArrayView *ext = (CljByteArrayView*)alloc(sizeof(CljByteArrayView), 1, CLJ_BYTE_ARRAY);
    if (!ext) {
        throw_oom();
        return NULL;
    }

    ext->base_arr.base.type = CLJ_BYTE_ARRAY;
    ext->base_arr.base.flags = CLJ_FLAG_EXTERNAL_DATA;
    ext->base_arr.length = length;
    ext->base_arr.data = bytes;
    ext->external_ctx = NULL;
    ext->external_free_fn = NULL;
    return (CljByteArray*)ext;
}

CljByteArray* make_byte_array_external(uint8_t *bytes, int length, void *external_ctx, CljByteArrayViewFreeFn free_fn) {
    CljByteArray *arr = make_byte_array_view(bytes, length);
    if (!arr) return NULL;
    if (length == 0) {
        // Empty singleton - no external finalizer.
        return arr;
    }

    CLJ_ASSERT(((CljObject*)arr)->type == CLJ_BYTE_ARRAY);
    CLJ_ASSERT((((CljObject*)arr)->flags & CLJ_FLAG_EXTERNAL_DATA) != 0);
    CljByteArrayView *ext = (CljByteArrayView*)arr;
    ext->external_ctx = external_ctx;
    ext->external_free_fn = free_fn;
    return arr;
}

uint8_t byte_array_get(CljValue arr, int index) {
    assert(arr != NULL && "byte array must not be NULL");

    CljByteArray *ba = as_byte_array(arr);
    assert(ba != NULL && "Invalid byte array");
    assert(index >= 0 && index < ba->length && "Index out of bounds");

    if (index < 0 || index >= ba->length) {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "Copy to offset %d with length %d exceeds destination length %d",
                dest_offset, length, dest_ba->length);
        return;
    }

    if (src_offset + length > src_ba->length) {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "Slice from offset %d with length %d exceeds array length %d",
                offset, length, ba->length); return NULL;
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "ID read at index %d (size %zu) exceeds array length %d",
                index, sizeof(ID), ba->length); return NULL;
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
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "ID write at index %d (size %zu) exceeds array length %d",
                index, sizeof(ID), ba->length);
        return;
    }

    memcpy(ba->data + index, &value, sizeof(ID));
}
