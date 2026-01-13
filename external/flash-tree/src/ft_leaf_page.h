// ft_leaf_page.h - Minimal persisted leaf page with key prefix-compression.
//
// Keys are prefix-compressed against the previous key in the page.
// Values are stored as raw (len + bytes) without compression.
//
// This is intentionally small: single-leaf root, no splits/inner nodes/WAL.
//
// All comments in English (workspace rule).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "flash_tree.h"

// Magic 'F''T''L''F' (host endian; tests assume little-endian platform).
#define FT_LEAF_MAGIC 0x464C5446u
#define FT_LEAF_VERSION 1u
#define FT_LEAF_FLAG_LEAF 1u

typedef struct ft_leaf_entry_ref {
    const void* key;
    size_t key_len;
    const void* val;
    size_t val_len;
} ft_leaf_entry_ref_t;

typedef struct ft_leaf_stats {
    size_t n_entries;
    size_t raw_key_bytes;    // sum of full key lengths
    size_t stored_key_bytes; // sum of stored suffix lengths (prefix-compressed)
} ft_leaf_stats_t;

// Initialize an empty leaf page (fills with 0xFF and writes header).
ft_status_t ft_leaf_init_empty(uint8_t* page, size_t page_len);

// Encode entries into a leaf page. Entries must be in lexicographic byte order.
// The page buffer is fully rewritten (filled with 0xFF then populated).
ft_status_t ft_leaf_encode(uint8_t* page, size_t page_len,
                           const ft_leaf_entry_ref_t* entries, size_t n_entries,
                           ft_leaf_stats_t* out_stats);

// Iterate all entries in stored order, reconstructing the key into key_scratch.
// The key pointer passed to cb is only valid until the next callback invocation.
ft_status_t ft_leaf_iter(const uint8_t* page, size_t page_len,
                         uint8_t* key_scratch, size_t key_scratch_cap,
                         ft_key_cb cb, void* arg,
                         ft_leaf_stats_t* out_stats);

// Like ft_leaf_iter, but only invokes cb for keys matching prefix.
ft_status_t ft_leaf_iter_prefix(const uint8_t* page, size_t page_len,
                                const void* prefix, size_t prefix_len,
                                uint8_t* key_scratch, size_t key_scratch_cap,
                                ft_key_cb cb, void* arg,
                                ft_leaf_stats_t* out_stats);

