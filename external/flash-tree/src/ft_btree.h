// ft_btree.h - Minimal sorted-key index (placeholder for CoW B-Tree).
//
// For early test-first progress, we model the index as an in-memory sorted array
// with lexicographic byte-wise ordering. Later this will be replaced with a
// persisted CoW B-Tree while preserving the observable semantics.

#pragma once

#include <stddef.h>

#include "flash_tree.h"

typedef struct ft_kv_ref {
    const void* key;
    size_t key_len;
    const void* val;
    size_t val_len;
} ft_kv_ref_t;

// Compare keys in lexicographic byte order (memcmp order, then shorter first).
int ft_lex_bytes_cmp(const void* a, size_t a_len, const void* b, size_t b_len);

// Return the first index i where entries[i].key >= key (lex order).
size_t ft_lower_bound_kv(const ft_kv_ref_t* entries, size_t n,
                         const void* key, size_t key_len);

// Iterate all entries whose key has the given prefix (in lex order).
ft_status_t ft_iter_prefix_kv(const ft_kv_ref_t* entries, size_t n,
                              const void* prefix, size_t prefix_len,
                              ft_key_cb cb, void* arg);

