// tdb_blob.h - Internal blob layer (Meta-Key + Index-Keys + Data-Pages)
//
// This implements large values without BSD btree overflow pages by storing:
// - Meta-Key: <user_key> | 0x00 | 'M'  -> BlobDesc + inline pgno[]
// - Index-Key: <user_key> | 0x00 | 'C' | gen_be(u32) | block_i_be(u32) -> IndexBlock(pgno[])
//
// Data pages are stored as mpool pages (pgno) and referenced by pgno arrays.
//
// NOTE: This is an internal header (src/). Public entry points are declared in inc/tiny_db.h.
#pragma once

#include "tiny_db.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== On-flash formats (encoded in KV values) ============== */

#define TDB_BLOB_DESC_VERSION 1u

typedef struct tdb_blob_desc {
    uint8_t version;            /* must be TDB_BLOB_DESC_VERSION */
    uint8_t reserved;           /* must be 0 */
    uint16_t chunk_size;        /* bytes per data page payload (big-endian on wire) */
    uint32_t logical_size;      /* total blob size (big-endian on wire) */
    uint32_t generation;        /* current generation (big-endian on wire) */
    uint16_t index_block_count; /* number of index keys (big-endian on wire) */
    uint16_t inline_pgno_count; /* number of inline pgno entries (big-endian on wire) */
} tdb_blob_desc_t;

/* Fixed header size (without inline pgno array). */
#define TDB_BLOB_DESC_HDR_SIZE 16u

typedef struct tdb_index_block {
    uint16_t count; /* number of pgno entries (big-endian on wire) */
    /* followed by count * u32 pgno entries (big-endian on wire) */
} tdb_index_block_t;

#define TDB_INDEX_BLOCK_HDR_SIZE 2u

/* Encode/Decode helpers (used by tests and tdb_blob.c). */
tdb_status_t tdb_blob_desc_encode(uint8_t* out, size_t out_cap, const tdb_blob_desc_t* desc,
                                const uint32_t* inline_pgnos, uint16_t inline_pgno_count,
                                size_t* out_len);

tdb_status_t tdb_blob_desc_decode(const uint8_t* buf, size_t len, tdb_blob_desc_t* out_desc,
                                uint32_t* out_inline_pgnos, uint16_t* inout_inline_pgno_count);

tdb_status_t tdb_index_block_encode(uint8_t* out, size_t out_cap, const uint32_t* pgnos,
                                  uint16_t count, size_t* out_len);

tdb_status_t tdb_index_block_decode(const uint8_t* buf, size_t len, uint32_t* out_pgnos,
                                  uint16_t* inout_count);

#ifdef __cplusplus
} // extern "C"
#endif

/*
 * =========================
 * On-wire format reference
 * =========================
 *
 * All integers are encoded big-endian (network byte order). The structs above
 * are *not* written to flash directly; only the encoded byte sequences are.
 *
 * Key encoding (byte strings):
 *
 *   Meta key:
 *     <user_key> | 0x00 | 'M'
 *
 *   Index key:
 *     <user_key> | 0x00 | 'C' | gen_be(u32) | block_i_be(u32)
 *
 * Value formats:
 *
 *   BlobDesc (value for Meta key):
 *
 *     Offset  Size  Field               Type
 *     ------  ----  ------------------  -------------------------
 *     0       1     version             u8   (must be 1)
 *     1       1     reserved            u8   (must be 0)
 *     2       2     chunk_size          u16  (payload bytes per data page)
 *     4       4     logical_size        u32  (total blob size in bytes)
 *     8       4     generation          u32  (commit generation)
 *     12      2     index_block_count   u16  (# of Index keys for this gen)
 *     14      2     inline_pgno_count   u16  (# of inline pgno entries)
 *     16      ...   inline_pgno[]       u32[inline_pgno_count]
 *
 *   IndexBlock (value for Index key):
 *
 *     Offset  Size  Field               Type
 *     ------  ----  ------------------  -------------------------
 *     0       2     count               u16  (# of pgno entries)
 *     2       ...   pgno[]              u32[count]
 *
 * Semantics / invariants:
 * - Data pages are stored in the mpool log and referenced by their pgno (u32).
 * - The blob's content is the concatenation of the referenced pages in order.
 * - The last page may be partially used; logical_size defines the exact length.
 * - Commit point: writing the Meta key is the final step. Readers use
 *   (generation, index_block_count, inline_pgno_count) to locate pages.
 * - Implementations must validate bounds (length matches header + arrays) and
 *   version/reserved fields to detect corruption.
 */
