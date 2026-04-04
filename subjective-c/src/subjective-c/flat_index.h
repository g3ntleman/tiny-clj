/*
 * CljFlatIndex — lightweight open-addressing hash table (uintptr_t → uintptr_t).
 *
 * Non-owning: does not RETAIN/RELEASE keys or values.
 * No tombstones: designed for bulk-clear + rebuild patterns.
 * Sentinel: key == 0 marks empty slots (caller must ensure keys are never 0).
 * Header-only: all functions are static inline.
 *
 * Usage patterns:
 *   ID→ID:        key = (uintptr_t)id,          value = (uintptr_t)id
 *   ID→row index: key = (uintptr_t)id,          value = (uintptr_t)row_idx
 *   composite:    key = user_hash(a,b),          value = (uintptr_t)idx
 */

#ifndef SUBJECTIVE_C_FLAT_INDEX_H
#define SUBJECTIVE_C_FLAT_INDEX_H

#include "memory.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    uintptr_t key;   /* 0 = empty */
    uintptr_t value;
} CljFlatIndexEntry;

typedef struct {
    CljFlatIndexEntry *entries;
    uint32_t capacity; /* power-of-2, 0 when unallocated */
    uint32_t count;    /* active entries */
} CljFlatIndex;

/* ---- hash ---- */

static inline uint32_t clj_flat_index_hash(uintptr_t key) {
    /* Fibonacci hashing — good scatter for pointer-derived keys. */
    return (uint32_t)((key * (uintptr_t)0x9E3779B9u) >> 16u);
}

/* ---- lifecycle ---- */

static inline void clj_flat_index_init(CljFlatIndex *idx) {
    idx->entries  = NULL;
    idx->capacity = 0u;
    idx->count    = 0u;
}

static inline void clj_flat_index_destroy(CljFlatIndex *idx) {
    CLJ_HOST_FREE(idx->entries);
    idx->entries  = NULL;
    idx->capacity = 0u;
    idx->count    = 0u;
}

/* Clear all entries without freeing (keeps allocation for reuse). */
static inline void clj_flat_index_clear(CljFlatIndex *idx) {
    if (idx->entries && idx->capacity > 0u) {
        memset(idx->entries, 0,
               (size_t)idx->capacity * sizeof(CljFlatIndexEntry));
    }
    idx->count = 0u;
}

/* ---- capacity ---- */

static inline uint32_t clj_flat_index_next_pow2(uint32_t v) {
    uint32_t cap = 8u;
    while (cap < v) { cap *= 2u; }
    return cap;
}

/* Ensure capacity for at least `needed` entries (load factor ≤ 0.5).
   Clears the table on growth — caller must re-insert.  Returns false on OOM. */
static inline bool clj_flat_index_reserve(CljFlatIndex *idx, uint32_t needed) {
    uint32_t required = (needed < 4u) ? 8u : (needed * 2u);
    uint32_t cap = clj_flat_index_next_pow2(required);
    if (idx->entries && idx->capacity >= cap) { return true; }

    CljFlatIndexEntry *p = (CljFlatIndexEntry *)CLJ_HOST_REALLOC(
        idx->entries, (size_t)cap * sizeof(CljFlatIndexEntry));
    if (!p) { return false; }
    memset(p, 0, (size_t)cap * sizeof(CljFlatIndexEntry));
    idx->entries  = p;
    idx->capacity = cap;
    idx->count    = 0u;
    return true;
}

/* ---- lookup ---- */

static inline CljFlatIndexEntry *clj_flat_index_find(
        const CljFlatIndex *idx, uintptr_t key) {
    if (!idx->entries || idx->capacity == 0u) { return NULL; }
    uint32_t mask = idx->capacity - 1u;
    uint32_t slot = clj_flat_index_hash(key) & mask;
    for (uint32_t probe = 0; probe < idx->capacity; probe++) {
        CljFlatIndexEntry *e = &idx->entries[slot];
        if (e->key == 0u)  { return NULL; }
        if (e->key == key)  { return e; }
        slot = (slot + 1u) & mask;
    }
    return NULL;
}

/* ---- mutation ---- */

/* Insert or update.  Returns false only if the table is full
   (should not happen when reserve was called correctly). */
static inline bool clj_flat_index_put(CljFlatIndex *idx,
                                       uintptr_t key, uintptr_t value) {
    if (!idx->entries || idx->capacity == 0u) { return false; }
    uint32_t mask = idx->capacity - 1u;
    uint32_t slot = clj_flat_index_hash(key) & mask;
    for (uint32_t probe = 0; probe < idx->capacity; probe++) {
        CljFlatIndexEntry *e = &idx->entries[slot];
        if (e->key == 0u) {
            e->key   = key;
            e->value = value;
            idx->count++;
            return true;
        }
        if (e->key == key) {
            e->value = value;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    return false;
}

/* ---- iteration ---- */

#define CLJ_FLAT_INDEX_FOR_EACH(idx_ptr, entry_var)            \
    for (uint32_t _fi_i = 0;                                   \
         (idx_ptr)->entries && _fi_i < (idx_ptr)->capacity;     \
         _fi_i++)                                               \
        if ((entry_var = &(idx_ptr)->entries[_fi_i])->key != 0u)

#endif /* SUBJECTIVE_C_FLAT_INDEX_H */
