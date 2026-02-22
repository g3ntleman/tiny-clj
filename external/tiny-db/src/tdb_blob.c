// tdb_blob.c - Large values via Meta-Key + Index-Keys + Data-Pages.
//
// This layer stores big payloads without BSD btree overflow pages by:
// - appending payload to mpool pages (pgno)
// - storing pgno lists in KV values (Meta + Index blocks)
//
// Atomicity model (single-writer):
// - write new data pages
// - write new index keys (generation-specific)
// - write meta key last (commit point)

#include "tiny_db.h"

#include "tdb_blob.h"
#include "tdb_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef __DBINTERFACE_PRIVATE
#define __DBINTERFACE_PRIVATE
#endif
#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"
#include "tdb_bsd_mpool.h"
#include "tdb_kv_internal.h"

/* ============== Small scratch buffers (avoid heap for small keys) ============== */

typedef struct tdb_tmp_buf {
    uint8_t* p;
    size_t len;
    uint8_t stack[64];
} tdb_tmp_buf_t;

/**
 * @brief tdb_tmp_buf_alloc.
 * @param b Temporary buffer state.
 * @param n Element count.
 * @return Status code (TDB_OK on success).
 */
static inline tdb_status_t tdb_tmp_buf_alloc(tdb_tmp_buf_t* b, size_t n) {
    if (!b)
        return TDB_ERR_INVALID_ARG;
    b->p = NULL;
    b->len = 0;
    if (n <= sizeof(b->stack)) {
        b->p = b->stack;
        b->len = n;
        return TDB_OK;
    }
    b->p = (uint8_t*)malloc(n);
    if (!b->p)
        return TDB_ERR_NO_MEMORY;
    b->len = n;
    return TDB_OK;
}

/**
 * @brief tdb_tmp_buf_free.
 * @param b Temporary buffer state.
 */
static inline void tdb_tmp_buf_free(tdb_tmp_buf_t* b) {
    if (!b)
        return;
    if (b->p && b->p != b->stack)
        free(b->p);
    b->p = NULL;
    b->len = 0;
}

/* ============== Streaming helpers ============== */

typedef struct tdb_blob_copy_ctx {
    uint8_t* dst;
    size_t cap;
    size_t written;
} tdb_blob_copy_ctx_t;

/**
 * @brief tdb_blob_copy_cb.
 * @param data Value bytes.
 * @param len Length in bytes.
 * @param arg Callback/user context.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_copy_cb(const void* data, size_t len, void* arg) {
    tdb_blob_copy_ctx_t* c = (tdb_blob_copy_ctx_t*)arg;
    if (!c)
        return TDB_ERR_INVALID_ARG;
    if (c->written >= c->cap)
        return TDB_OK;
    size_t space = c->cap - c->written;
    size_t take = (len < space) ? len : space;
    memcpy(c->dst + c->written, data, take);
    c->written += take;
    return TDB_OK;
}

/* ============== Key helpers ============== */

/**
 * @brief tdb_blob_build_meta_key.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param out Output buffer pointer.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_build_meta_key(const void* user_key, size_t user_key_len,
                                          tdb_tmp_buf_t* out) {
    if (!out)
        return TDB_ERR_INVALID_ARG;
    if (!user_key && user_key_len != 0)
        return TDB_ERR_INVALID_ARG;
    const size_t n = user_key_len + 2u;
    tdb_status_t st = tdb_tmp_buf_alloc(out, n);
    if (st != TDB_OK)
        return st;
    if (user_key_len)
        memcpy(out->p, user_key, user_key_len);
    out->p[user_key_len + 0] = 0x00;
    out->p[user_key_len + 1] = (uint8_t)'M';
    return TDB_OK;
}

/**
 * @brief tdb_blob_build_index_key.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param generation Blob generation number.
 * @param block_i Index block number.
 * @param out Output buffer pointer.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_build_index_key(const void* user_key, size_t user_key_len,
                                           uint32_t generation, uint32_t block_i,
                                           tdb_tmp_buf_t* out) {
    if (!out)
        return TDB_ERR_INVALID_ARG;
    if (!user_key && user_key_len != 0)
        return TDB_ERR_INVALID_ARG;
    const size_t n = user_key_len + 1u + 1u + 4u + 4u;
    tdb_status_t st = tdb_tmp_buf_alloc(out, n);
    if (st != TDB_OK)
        return st;
    if (user_key_len)
        memcpy(out->p, user_key, user_key_len);
    out->p[user_key_len + 0] = 0x00;
    out->p[user_key_len + 1] = (uint8_t)'C';
    tdb_u32_wire_write(&out->p[user_key_len + 2], generation);
    tdb_u32_wire_write(&out->p[user_key_len + 6], block_i);
    return TDB_OK;
}

/* ============== Format helpers (public for tests via src/tdb_blob.h) ============== */

/**
 * @brief tdb_blob_desc_encode.
 * @param out Output buffer pointer.
 * @param out_cap Output pointer receiving cap.
 * @param desc Input pointer.
 * @param inline_pgnos Input pointer.
 * @param inline_pgno_count Element count.
 * @param out_len Output pointer receiving len.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_desc_encode(uint8_t* out, size_t out_cap, const tdb_blob_desc_t* desc,
                                const uint32_t* inline_pgnos, uint16_t inline_pgno_count,
                                size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!out || !desc || (!inline_pgnos && inline_pgno_count != 0) || !out_len)
        return TDB_ERR_INVALID_ARG;

    if (desc->version != TDB_BLOB_DESC_VERSION)
        return TDB_ERR_INVALID_ARG;
    if (desc->reserved != 0)
        return TDB_ERR_INVALID_ARG;
    if (desc->inline_pgno_count != inline_pgno_count)
        return TDB_ERR_INVALID_ARG;

    const size_t need = (size_t)TDB_BLOB_DESC_HDR_SIZE + (size_t)inline_pgno_count * 4u;
    if (need > out_cap)
        return TDB_ERR_INVALID_ARG;

    out[0] = desc->version;
    out[1] = desc->reserved;
    tdb_u16_wire_write(&out[2], desc->chunk_size);
    tdb_u32_wire_write(&out[4], desc->logical_size);
    tdb_u32_wire_write(&out[8], desc->generation);
    tdb_u16_wire_write(&out[12], desc->index_block_count);
    tdb_u16_wire_write(&out[14], inline_pgno_count);
    for (uint16_t i = 0; i < inline_pgno_count; i++) {
        tdb_u32_wire_write(&out[16u + (size_t)i * 4u], inline_pgnos[i]);
    }

    *out_len = need;
    return TDB_OK;
}

/**
 * @brief tdb_blob_desc_decode.
 * @param buf Input pointer.
 * @param len Length in bytes.
 * @param out_desc Output pointer receiving desc.
 * @param out_inline_pgnos Output pointer receiving inline pgnos.
 * @param inout_inline_pgno_count Element count.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_desc_decode(const uint8_t* buf, size_t len, tdb_blob_desc_t* out_desc,
                                uint32_t* out_inline_pgnos, uint16_t* inout_inline_pgno_count) {
    if (out_desc)
        memset(out_desc, 0, sizeof(*out_desc));
    if (!buf || !out_desc || !inout_inline_pgno_count)
        return TDB_ERR_INVALID_ARG;

    if (len < TDB_BLOB_DESC_HDR_SIZE)
        return TDB_ERR_CORRUPT;
    const uint8_t version = buf[0];
    const uint8_t reserved = buf[1];
    if (version != TDB_BLOB_DESC_VERSION)
        return TDB_ERR_CORRUPT;
    if (reserved != 0)
        return TDB_ERR_CORRUPT;

    const uint16_t chunk_size = tdb_u16_wire_read(&buf[2]);
    const uint32_t logical_size = tdb_u32_wire_read(&buf[4]);
    const uint32_t generation = tdb_u32_wire_read(&buf[8]);
    const uint16_t index_block_count = tdb_u16_wire_read(&buf[12]);
    const uint16_t inline_count = tdb_u16_wire_read(&buf[14]);

    const size_t need = (size_t)TDB_BLOB_DESC_HDR_SIZE + (size_t)inline_count * 4u;
    if (need != len && need > len)
        return TDB_ERR_CORRUPT;
    if (need > len)
        return TDB_ERR_CORRUPT;

    out_desc->version = version;
    out_desc->reserved = 0;
    out_desc->chunk_size = chunk_size;
    out_desc->logical_size = logical_size;
    out_desc->generation = generation;
    out_desc->index_block_count = index_block_count;
    out_desc->inline_pgno_count = inline_count;

    const uint16_t cap = *inout_inline_pgno_count;
    if (inline_count > cap) {
        *inout_inline_pgno_count = inline_count;
        return TDB_ERR_INVALID_ARG;
    }
    if (inline_count && !out_inline_pgnos)
        return TDB_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < inline_count; i++) {
        out_inline_pgnos[i] = tdb_u32_wire_read(&buf[16u + (size_t)i * 4u]);
    }
    *inout_inline_pgno_count = inline_count;
    return TDB_OK;
}

/**
 * @brief tdb_index_block_encode.
 * @param out Output buffer pointer.
 * @param out_cap Output pointer receiving cap.
 * @param pgnos Input pointer.
 * @param count Element count.
 * @param out_len Output pointer receiving len.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_index_block_encode(uint8_t* out, size_t out_cap, const uint32_t* pgnos,
                                  uint16_t count, size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!out || (!pgnos && count != 0) || !out_len)
        return TDB_ERR_INVALID_ARG;
    const size_t need = (size_t)TDB_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need > out_cap)
        return TDB_ERR_INVALID_ARG;
    tdb_u16_wire_write(&out[0], count);
    for (uint16_t i = 0; i < count; i++) {
        tdb_u32_wire_write(&out[2u + (size_t)i * 4u], pgnos[i]);
    }
    *out_len = need;
    return TDB_OK;
}

/**
 * @brief tdb_index_block_decode.
 * @param buf Input pointer.
 * @param len Length in bytes.
 * @param out_pgnos Output pointer receiving pgnos.
 * @param inout_count Element count.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_index_block_decode(const uint8_t* buf, size_t len, uint32_t* out_pgnos,
                                  uint16_t* inout_count) {
    if (!buf || !inout_count)
        return TDB_ERR_INVALID_ARG;
    if (len < TDB_INDEX_BLOCK_HDR_SIZE)
        return TDB_ERR_CORRUPT;
    const uint16_t count = tdb_u16_wire_read(&buf[0]);
    const size_t need = (size_t)TDB_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need != len && need > len)
        return TDB_ERR_CORRUPT;
    if (need > len)
        return TDB_ERR_CORRUPT;
    const uint16_t cap = *inout_count;
    if (count > cap) {
        *inout_count = count;
        return TDB_ERR_INVALID_ARG;
    }
    if (count && !out_pgnos)
        return TDB_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < count; i++) {
        out_pgnos[i] = tdb_u32_wire_read(&buf[2u + (size_t)i * 4u]);
    }
    *inout_count = count;
    return TDB_OK;
}

/* ============== Fast header peeks (avoid double decode + malloc churn) ============== */

/**
 * @brief tdb_blob_desc_peek.
 * @param buf Input pointer.
 * @param len Length in bytes.
 * @param out_desc Output pointer receiving desc.
 * @param out_inline_count Output pointer receiving inline.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_desc_peek(const uint8_t* buf, size_t len, tdb_blob_desc_t* out_desc,
                                     uint16_t* out_inline_count) {
    if (out_desc)
        memset(out_desc, 0, sizeof(*out_desc));
    if (out_inline_count)
        *out_inline_count = 0;
    if (!buf || !out_desc || !out_inline_count)
        return TDB_ERR_INVALID_ARG;
    if (len < TDB_BLOB_DESC_HDR_SIZE)
        return TDB_ERR_CORRUPT;
    if (buf[0] != TDB_BLOB_DESC_VERSION)
        return TDB_ERR_CORRUPT;
    if (buf[1] != 0)
        return TDB_ERR_CORRUPT;

    const uint16_t inline_count = tdb_u16_wire_read(&buf[14]);
    const size_t need = (size_t)TDB_BLOB_DESC_HDR_SIZE + (size_t)inline_count * 4u;
    if (need != len)
        return TDB_ERR_CORRUPT;

    out_desc->version = buf[0];
    out_desc->reserved = 0;
    out_desc->chunk_size = tdb_u16_wire_read(&buf[2]);
    out_desc->logical_size = tdb_u32_wire_read(&buf[4]);
    out_desc->generation = tdb_u32_wire_read(&buf[8]);
    out_desc->index_block_count = tdb_u16_wire_read(&buf[12]);
    out_desc->inline_pgno_count = inline_count;
    *out_inline_count = inline_count;
    return TDB_OK;
}

/**
 * @brief tdb_index_block_peek_count.
 * @param buf Input pointer.
 * @param len Length in bytes.
 * @param out_count Output pointer receiving count.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_index_block_peek_count(const uint8_t* buf, size_t len, uint16_t* out_count) {
    if (out_count)
        *out_count = 0;
    if (!buf || !out_count)
        return TDB_ERR_INVALID_ARG;
    if (len < TDB_INDEX_BLOCK_HDR_SIZE)
        return TDB_ERR_CORRUPT;
    const uint16_t count = tdb_u16_wire_read(&buf[0]);
    const size_t need = (size_t)TDB_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need != len)
        return TDB_ERR_CORRUPT;
    *out_count = count;
    return TDB_OK;
}

/* ============== Blob writer (opaque) ============== */

struct tdb_blob_writer {
    tdb_kv_t* kv;
    uint8_t* user_key;
    size_t user_key_len;

    uint32_t old_generation;
    uint16_t old_index_block_count;
    uint32_t generation;

    uint16_t chunk_size;
    uint16_t inline_cap;
    uint16_t idx_cap;

    uint32_t logical_size; /* total bytes written */

    uint32_t* inline_pgnos;
    uint16_t inline_count;

    uint32_t* idx_pgnos; /* current index block pgno list */
    uint16_t idx_count;
    uint16_t index_block_count;
    uint32_t block_i;

    /* Current output page (pinned in mpool until flushed). */
    uint8_t* cur_page; /* mp->pagesize bytes */
    pgno_t cur_pgno;
    size_t cur_fill;
};

/**
 * @brief tdb_blob_next_generation.
 * @param old_gen Generation to remove.
 * @return Computed 32-bit value.
 */
static uint32_t tdb_blob_next_generation(uint32_t old_gen) {
    uint32_t g = old_gen + 1u;
    if (g == 0u)
        g = 1u;
    return g;
}

/**
 * @brief tdb_blob_read_meta_header.
 * @param kv KV database handle.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param out_desc Output pointer receiving desc.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_read_meta_header(tdb_kv_t* kv, const void* user_key, size_t user_key_len,
                                            tdb_blob_desc_t* out_desc) {
    if (out_desc)
        memset(out_desc, 0, sizeof(*out_desc));
    if (!kv || (!user_key && user_key_len != 0) || !out_desc)
        return TDB_ERR_INVALID_ARG;

    tdb_tmp_buf_t meta_key = {0};
    tdb_status_t st = tdb_blob_build_meta_key(user_key, user_key_len, &meta_key);
    if (st != TDB_OK)
        return st;

    tdb_blob_t v = {0};
    st = tdb_kv_get(kv, meta_key.p, meta_key.len, &v);
    tdb_tmp_buf_free(&meta_key);
    if (st != TDB_OK)
        return st;

    uint16_t inline_count = 0;
    return tdb_blob_desc_peek((const uint8_t*)v.data, v.len, out_desc, &inline_count);
}

/**
 * @brief tdb_blob_read_meta_full.
 * @param kv KV database handle.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param out_desc Output pointer receiving desc.
 * @param out_inline_pgnos Output pointer receiving inline pgnos.
 * @param out_inline_count Output pointer receiving inline.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_read_meta_full(tdb_kv_t* kv, const void* user_key, size_t user_key_len,
                                          tdb_blob_desc_t* out_desc, uint32_t** out_inline_pgnos,
                                          uint16_t* out_inline_count) {
    if (out_inline_pgnos)
        *out_inline_pgnos = NULL;
    if (out_inline_count)
        *out_inline_count = 0;
    if (!kv || (!user_key && user_key_len != 0) || !out_desc || !out_inline_pgnos ||
        !out_inline_count) {
        return TDB_ERR_INVALID_ARG;
    }

    tdb_tmp_buf_t meta_key = {0};
    tdb_status_t st = tdb_blob_build_meta_key(user_key, user_key_len, &meta_key);
    if (st != TDB_OK)
        return st;

    tdb_blob_t v = {0};
    st = tdb_kv_get(kv, meta_key.p, meta_key.len, &v);
    tdb_tmp_buf_free(&meta_key);
    if (st != TDB_OK)
        return st;

    uint16_t inline_count = 0;
    st = tdb_blob_desc_peek((const uint8_t*)v.data, v.len, out_desc, &inline_count);
    if (st != TDB_OK)
        return st;
    if (inline_count == 0) {
        *out_inline_pgnos = NULL;
        *out_inline_count = 0;
        return TDB_OK;
    }

    uint32_t* p = (uint32_t*)malloc((size_t)inline_count * sizeof(uint32_t));
    if (!p)
        return TDB_ERR_NO_MEMORY;
    uint16_t cap = inline_count;
    st = tdb_blob_desc_decode((const uint8_t*)v.data, v.len, out_desc, p, &cap);
    if (st != TDB_OK || cap != inline_count) {
        free(p);
        return (st != TDB_OK) ? st : TDB_ERR_CORRUPT;
    }
    *out_inline_pgnos = p;
    *out_inline_count = inline_count;
    return TDB_OK;
}

/**
 * @brief tdb_blob_write_index_block.
 * @param kv KV database handle.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param generation Blob generation number.
 * @param block_i Index block number.
 * @param pgnos Input pointer.
 * @param count Element count.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_write_index_block(tdb_kv_t* kv, const void* user_key, size_t user_key_len,
                                             uint32_t generation, uint32_t block_i,
                                             const uint32_t* pgnos, uint16_t count) {
    if (!kv || (!user_key && user_key_len != 0) || (!pgnos && count != 0))
        return TDB_ERR_INVALID_ARG;

    tdb_tmp_buf_t idx_key = {0};
    tdb_status_t st = tdb_blob_build_index_key(user_key, user_key_len, generation, block_i, &idx_key);
    if (st != TDB_OK)
        return st;

    /* Compute max value length and encode accordingly. */
    size_t max_val = 0;
    st = tdb_kv_max_val_len(kv, idx_key.len, &max_val);
    if (st != TDB_OK) {
        tdb_tmp_buf_free(&idx_key);
        return st;
    }

    const size_t need = (size_t)TDB_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need > max_val) {
        tdb_tmp_buf_free(&idx_key);
        return TDB_ERR_INVALID_ARG;
    }

    uint8_t* buf = (uint8_t*)malloc(need);
    if (!buf) {
        tdb_tmp_buf_free(&idx_key);
        return TDB_ERR_NO_MEMORY;
    }
    size_t enc_len = 0;
    st = tdb_index_block_encode(buf, need, pgnos, count, &enc_len);
    if (st != TDB_OK) {
        free(buf);
        tdb_tmp_buf_free(&idx_key);
        return st;
    }

    st = tdb_kv_put(kv, idx_key.p, idx_key.len, buf, enc_len);
    free(buf);
    tdb_tmp_buf_free(&idx_key);
    return st;
}

/**
 * @brief tdb_blob_delete_generation.
 * @param kv KV database handle.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param generation Blob generation number.
 * @param index_block_count Element count.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_delete_generation(tdb_kv_t* kv, const void* user_key, size_t user_key_len,
                                             uint32_t generation, uint16_t index_block_count) {
    if (!kv || (!user_key && user_key_len != 0))
        return TDB_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < index_block_count; i++) {
        tdb_tmp_buf_t k = {0};
        tdb_status_t st =
            tdb_blob_build_index_key(user_key, user_key_len, generation, (uint32_t)i, &k);
        if (st != TDB_OK)
            return st;
        (void)tdb_kv_del(kv, k.p, k.len); /* best-effort */
        tdb_tmp_buf_free(&k);
    }
    return TDB_OK;
}

/**
 * @brief tdb_blob_collect_all_pgnos.
 * @param kv KV database handle.
 * @param user_key Key bytes.
 * @param user_key_len Key length in bytes.
 * @param desc Input pointer.
 * @param inline_pgnos Input pointer.
 * @param inline_count Element count.
 * @param out_all Output pointer receiving all.
 * @param out_count Output pointer receiving count.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_collect_all_pgnos(tdb_kv_t* kv, const void* user_key, size_t user_key_len,
                                             const tdb_blob_desc_t* desc,
                                             const uint32_t* inline_pgnos, uint16_t inline_count,
                                             uint32_t** out_all, size_t* out_count) {
    if (out_all)
        *out_all = NULL;
    if (out_count)
        *out_count = 0;
    if (!kv || !desc || (!inline_pgnos && inline_count != 0) || !out_all || !out_count)
        return TDB_ERR_INVALID_ARG;

    const size_t chunk = (size_t)desc->chunk_size;
    if (chunk == 0)
        return TDB_ERR_CORRUPT;

    const size_t pages_needed =
        (desc->logical_size == 0) ? 0 : (((size_t)desc->logical_size + chunk - 1u) / chunk);

    if ((size_t)inline_count > pages_needed)
        return TDB_ERR_CORRUPT;

    size_t total = (size_t)inline_count;
    uint32_t* all = (uint32_t*)malloc((pages_needed ? pages_needed : 1u) * sizeof(uint32_t));
    if (!all)
        return TDB_ERR_NO_MEMORY;
    for (uint16_t i = 0; i < inline_count; i++)
        all[i] = inline_pgnos[i];

    /* Reuse a single decode buffer sized to the largest index block seen. */
    uint32_t* tmp = NULL;
    uint16_t tmp_cap = 0;

    for (uint16_t bi = 0; bi < desc->index_block_count; bi++) {
        tdb_tmp_buf_t idx_key = {0};
        tdb_status_t st = tdb_blob_build_index_key(user_key, user_key_len, desc->generation,
                                                 (uint32_t)bi, &idx_key);
        if (st != TDB_OK) {
            free(tmp);
            free(all);
            return st;
        }

        tdb_blob_t v = {0};
        st = tdb_kv_get(kv, idx_key.p, idx_key.len, &v);
        tdb_tmp_buf_free(&idx_key);
        if (st != TDB_OK) {
            free(tmp);
            free(all);
            return st;
        }

        uint16_t got = 0;
        st = tdb_index_block_peek_count((const uint8_t*)v.data, v.len, &got);
        if (st != TDB_OK || got == 0) {
            free(tmp);
            free(all);
            return TDB_ERR_CORRUPT;
        }

        if (got > tmp_cap) {
            uint32_t* np = (uint32_t*)realloc(tmp, (size_t)got * sizeof(uint32_t));
            if (!np) {
                free(tmp);
                free(all);
                return TDB_ERR_NO_MEMORY;
            }
            tmp = np;
            tmp_cap = got;
        }
        uint16_t cap = tmp_cap;
        st = tdb_index_block_decode((const uint8_t*)v.data, v.len, tmp, &cap);
        if (st != TDB_OK || cap != got) {
            free(tmp);
            free(all);
            return (st != TDB_OK) ? st : TDB_ERR_CORRUPT;
        }

        if (total + got > pages_needed) {
            free(tmp);
            free(all);
            return TDB_ERR_CORRUPT;
        }
        memcpy(&all[total], tmp, (size_t)got * sizeof(uint32_t));
        total += got;
    }

    free(tmp);

    /* Validate that the descriptor matches the pgno list length. */
    if (total != pages_needed) {
        free(all);
        return TDB_ERR_CORRUPT;
    }

    *out_all = all;
    *out_count = total;
    return TDB_OK;
}

/* ============== Public API ============== */

/**
 * @brief tdb_blob_chunk_size.
 * @param kv KV database handle.
 * @param out_chunk_size Output pointer receiving chunk.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_chunk_size(tdb_kv_t* kv, size_t* out_chunk_size) {
    if (out_chunk_size)
        *out_chunk_size = 0;
    if (!kv || !out_chunk_size)
        return TDB_ERR_INVALID_ARG;
    MPOOL* mp = tdb_kv_get_mpool(kv);
    if (!mp)
        return TDB_ERR_IO;
    *out_chunk_size = (size_t)mp->pagesize;
    return (*out_chunk_size > 0) ? TDB_OK : TDB_ERR_IO;
}

/**
 * @brief tdb_blob_writer_init.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out_writer Output pointer receiving writer.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_writer_init(tdb_kv_t* kv, const void* key, size_t key_len,
                                tdb_blob_writer_t** out_writer) {
    if (!out_writer)
        return TDB_ERR_INVALID_ARG;
    *out_writer = NULL;
    if (!kv || (!key && key_len != 0))
        return TDB_ERR_INVALID_ARG;

    MPOOL* mp = tdb_kv_get_mpool(kv);
    if (!mp)
        return TDB_ERR_IO;

    /* Read existing meta (optional) - header only (no inline list allocation). */
    tdb_blob_desc_t old_desc = {0};
    uint32_t old_gen = 0;
    uint16_t old_index_blocks = 0;
    tdb_status_t st_meta = tdb_blob_read_meta_header(kv, key, key_len, &old_desc);
    if (st_meta == TDB_OK) {
        old_gen = old_desc.generation;
        old_index_blocks = old_desc.index_block_count;
    }

    /* Compute capacities from KV limits. */
    tdb_tmp_buf_t meta_key = {0};
    tdb_status_t st = tdb_blob_build_meta_key(key, key_len, &meta_key);
    if (st != TDB_OK) {
        return st;
    }
    const size_t meta_key_len = meta_key.len;

    size_t max_meta_val = 0;
    st = tdb_kv_max_val_len(kv, meta_key_len, &max_meta_val);
    tdb_tmp_buf_free(&meta_key);
    if (st != TDB_OK) {
        return st;
    }
    if (max_meta_val < TDB_BLOB_DESC_HDR_SIZE) {
        return TDB_ERR_INVALID_ARG;
    }
    const uint16_t inline_cap = (uint16_t)((max_meta_val - TDB_BLOB_DESC_HDR_SIZE) / 4u);

    tdb_tmp_buf_t idx_key = {0};
    st = tdb_blob_build_index_key(key, key_len, 1u, 0u, &idx_key);
    if (st != TDB_OK) {
        return st;
    }
    const size_t idx_key_len = idx_key.len;
    size_t max_idx_val = 0;
    st = tdb_kv_max_val_len(kv, idx_key_len, &max_idx_val);
    tdb_tmp_buf_free(&idx_key);
    if (st != TDB_OK) {
        return st;
    }
    if (max_idx_val < TDB_INDEX_BLOCK_HDR_SIZE) {
        return TDB_ERR_INVALID_ARG;
    }
    size_t tmp_cap = (max_idx_val - TDB_INDEX_BLOCK_HDR_SIZE) / 4u;
    if (tmp_cap > 0xFFFFu)
        tmp_cap = 0xFFFFu;
    const uint16_t idx_cap = (uint16_t)tmp_cap;
    if (idx_cap == 0) {
        return TDB_ERR_INVALID_ARG;
    }

    /* Allocate writer */
    tdb_blob_writer_t* w = (tdb_blob_writer_t*)calloc(1, sizeof(*w));
    if (!w) {
        return TDB_ERR_NO_MEMORY;
    }

    w->kv = kv;
    w->user_key = (uint8_t*)malloc(key_len);
    if (key_len && !w->user_key) {
        free(w);
        return TDB_ERR_NO_MEMORY;
    }
    if (key_len)
        memcpy(w->user_key, key, key_len);
    w->user_key_len = key_len;

    w->old_generation = (st_meta == TDB_OK) ? old_gen : 0u;
    w->old_index_block_count = (st_meta == TDB_OK) ? old_index_blocks : 0u;
    w->generation = tdb_blob_next_generation((st_meta == TDB_OK) ? old_gen : 0u);

    const uint32_t pagesize = mp->pagesize;
    if (pagesize == 0 || pagesize > 0xFFFFu) {
        free(w->user_key);
        free(w);
        return TDB_ERR_UNSUPPORTED;
    }
    w->chunk_size = (uint16_t)pagesize;
    w->inline_cap = inline_cap;
    w->idx_cap = idx_cap;

    w->inline_pgnos = (uint32_t*)malloc((size_t)inline_cap * sizeof(uint32_t));
    w->idx_pgnos = (uint32_t*)malloc((size_t)idx_cap * sizeof(uint32_t));
    if (!w->inline_pgnos || !w->idx_pgnos) {
        free(w->idx_pgnos);
        free(w->inline_pgnos);
        free(w->user_key);
        free(w);
        return TDB_ERR_NO_MEMORY;
    }
    *out_writer = w;
    return TDB_OK;
}

/**
 * @brief tdb_blob_writer_record_pgno.
 * @param w Blob writer state.
 * @param pgno Page number.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_writer_record_pgno(tdb_blob_writer_t* w, pgno_t pgno) {
    if (!w)
        return TDB_ERR_INVALID_ARG;

    /* Record pgno */
    if (w->inline_count < w->inline_cap) {
        w->inline_pgnos[w->inline_count++] = (uint32_t)pgno;
    } else {
        w->idx_pgnos[w->idx_count++] = (uint32_t)pgno;
        if (w->idx_count == w->idx_cap) {
            tdb_status_t st =
                tdb_blob_write_index_block(w->kv, w->user_key, w->user_key_len, w->generation,
                                          w->block_i, w->idx_pgnos, w->idx_count);
            if (st != TDB_OK)
                return st;
            w->index_block_count++;
            w->block_i++;
            w->idx_count = 0;
        }
    }
    return TDB_OK;
}

/**
 * @brief tdb_blob_writer_flush_current_page.
 * @param w Blob writer state.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_writer_flush_current_page(tdb_blob_writer_t* w) {
    if (!w)
        return TDB_ERR_INVALID_ARG;
    if (!w->cur_page)
        return TDB_OK;

    MPOOL* mp = tdb_kv_get_mpool(w->kv);
    if (!mp)
        return TDB_ERR_IO;

    /* Zero padding is already present because we memset() the page on allocation. */
    if (mpool_put(mp, w->cur_page, MPOOL_DIRTY) != 0)
        return TDB_ERR_IO;
    w->cur_page = NULL;
    w->cur_pgno = PGNO_INVALID;
    w->cur_fill = 0;
    return TDB_OK;
}

/**
 * @brief tdb_blob_writer_ensure_page.
 * @param w Blob writer state.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_blob_writer_ensure_page(tdb_blob_writer_t* w) {
    if (!w)
        return TDB_ERR_INVALID_ARG;
    if (w->cur_page)
        return TDB_OK;

    MPOOL* mp = tdb_kv_get_mpool(w->kv);
    if (!mp)
        return TDB_ERR_IO;

    pgno_t pgno = PGNO_INVALID;
    uint8_t* page = (uint8_t*)mpool_new(mp, &pgno);
    if (!page || pgno == PGNO_INVALID) {
        errno = ENOMEM;
        return TDB_ERR_NO_MEMORY;
    }
    memset(page, 0, mp->pagesize);

    tdb_status_t st = tdb_blob_writer_record_pgno(w, pgno);
    if (st != TDB_OK) {
        /* Best-effort: don't leak a pinned page on failure. */
        (void)mpool_put(mp, page, 0);
        return st;
    }

    w->cur_page = page;
    w->cur_pgno = pgno;
    w->cur_fill = 0;
    return TDB_OK;
}

/**
 * @brief tdb_blob_write.
 * @param w Blob writer state.
 * @param data Value bytes.
 * @param len Length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_write(tdb_blob_writer_t* w, const void* data, size_t len) {
    if (!w || (!data && len != 0))
        return TDB_ERR_INVALID_ARG;
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        tdb_status_t st = tdb_blob_writer_ensure_page(w);
        if (st != TDB_OK)
            return st;

        const size_t space = (size_t)w->chunk_size - w->cur_fill;
        const size_t take = (len < space) ? len : space;
        memcpy(w->cur_page + w->cur_fill, p, take);
        w->cur_fill += take;
        p += take;
        len -= take;
        w->logical_size += (uint32_t)take;
        if (w->cur_fill == (size_t)w->chunk_size) {
            st = tdb_blob_writer_flush_current_page(w);
            if (st != TDB_OK)
                return st;
        }
    }
    return TDB_OK;
}

/**
 * @brief tdb_blob_writer_free.
 * @param w Blob writer state.
 */
static void tdb_blob_writer_free(tdb_blob_writer_t* w) {
    if (!w)
        return;
    free(w->idx_pgnos);
    free(w->inline_pgnos);
    free(w->user_key);
    free(w);
}

/**
 * @brief tdb_blob_abort.
 * @param w Blob writer state.
 */
void tdb_blob_abort(tdb_blob_writer_t* w) {
    /* No commit: data pages are unreferenced (may be reclaimed by block-level GC if implemented).
     */
    tdb_blob_writer_free(w);
}

/**
 * @brief tdb_blob_finish.
 * @param w Blob writer state.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_finish(tdb_blob_writer_t* w) {
    if (!w)
        return TDB_ERR_INVALID_ARG;

    /* Snapshot previous generation's referenced pages (before we overwrite meta). */
    uint32_t* old_all = NULL;
    size_t old_all_count = 0;
    uint32_t old_gen = w->old_generation;
    uint16_t old_index_block_count = w->old_index_block_count;
    if (old_gen) {
        tdb_blob_desc_t old_desc = {0};
        uint32_t* old_inline = NULL;
        uint16_t old_inline_count = 0;
        tdb_status_t st_old = tdb_blob_read_meta_full(w->kv, w->user_key, w->user_key_len, &old_desc,
                                                    &old_inline, &old_inline_count);
        if (st_old == TDB_OK && old_desc.generation == old_gen) {
            (void)tdb_blob_collect_all_pgnos(w->kv, w->user_key, w->user_key_len, &old_desc,
                                            old_inline, old_inline_count, &old_all, &old_all_count);
        }
        free(old_inline);
    }

    /* Flush tail page if needed. */
    if (w->cur_page) {
        tdb_status_t st = tdb_blob_writer_flush_current_page(w);
        if (st != TDB_OK) {
            free(old_all);
            tdb_blob_writer_free(w);
            return st;
        }
    }

    /* Flush tail index block. */
    if (w->idx_count) {
        tdb_status_t st =
            tdb_blob_write_index_block(w->kv, w->user_key, w->user_key_len, w->generation,
                                      w->block_i, w->idx_pgnos, w->idx_count);
        if (st != TDB_OK) {
            free(old_all);
            tdb_blob_writer_free(w);
            return st;
        }
        w->index_block_count++;
        w->block_i++;
        w->idx_count = 0;
    }

    /* Write Meta-Key (commit point). */
    tdb_tmp_buf_t meta_key = {0};
    tdb_status_t st = tdb_blob_build_meta_key(w->user_key, w->user_key_len, &meta_key);
    if (st != TDB_OK) {
        free(old_all);
        tdb_blob_writer_free(w);
        return st;
    }
    const size_t meta_key_len = meta_key.len;

    size_t max_meta_val = 0;
    st = tdb_kv_max_val_len(w->kv, meta_key_len, &max_meta_val);
    if (st != TDB_OK) {
        tdb_tmp_buf_free(&meta_key);
        free(old_all);
        tdb_blob_writer_free(w);
        return st;
    }

    const size_t meta_need = (size_t)TDB_BLOB_DESC_HDR_SIZE + (size_t)w->inline_count * 4u;
    if (meta_need > max_meta_val) {
        tdb_tmp_buf_free(&meta_key);
        free(old_all);
        tdb_blob_writer_free(w);
        return TDB_ERR_INVALID_ARG;
    }
    uint8_t* meta_val = (uint8_t*)malloc(meta_need);
    if (!meta_val) {
        tdb_tmp_buf_free(&meta_key);
        free(old_all);
        tdb_blob_writer_free(w);
        return TDB_ERR_NO_MEMORY;
    }

    tdb_blob_desc_t desc = {
        .version = (uint8_t)TDB_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = w->chunk_size,
        .logical_size = w->logical_size,
        .generation = w->generation,
        .index_block_count = w->index_block_count,
        .inline_pgno_count = w->inline_count,
    };
    size_t enc_len = 0;
    st =
        tdb_blob_desc_encode(meta_val, meta_need, &desc, w->inline_pgnos, w->inline_count, &enc_len);
    if (st == TDB_OK)
        st = tdb_kv_put(w->kv, meta_key.p, meta_key_len, meta_val, enc_len);
    free(meta_val);
    tdb_tmp_buf_free(&meta_key);
    if (st != TDB_OK) {
        free(old_all);
        tdb_blob_writer_free(w);
        return st;
    }

    /* Best-effort cleanup of previous generation (keys + data pages). */
    if (old_gen) {
        if (old_all) {
            MPOOL* mp = tdb_kv_get_mpool(w->kv);
            if (mp) {
                for (size_t i = 0; i < old_all_count; i++) {
                    (void)mpool_free_pgno(mp, (pgno_t)old_all[i]);
                }
            }
            free(old_all);
        }
        (void)tdb_blob_delete_generation(w->kv, w->user_key, w->user_key_len, old_gen,
                                        old_index_block_count);
    }

    tdb_blob_writer_free(w);
    return TDB_OK;
}

/**
 * @brief tdb_blob_put.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param data Value bytes.
 * @param len Length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_put(tdb_kv_t* kv, const void* key, size_t key_len, const void* data,
                        size_t len) {
    tdb_blob_writer_t* w = NULL;
    tdb_status_t st = tdb_blob_writer_init(kv, key, key_len, &w);
    if (st != TDB_OK)
        return st;
    st = tdb_blob_write(w, data, len);
    if (st != TDB_OK) {
        tdb_blob_abort(w);
        return st;
    }
    return tdb_blob_finish(w);
}

/**
 * @brief tdb_blob_get_len.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out_len Output pointer receiving len.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_get_len(tdb_kv_t* kv, const void* key, size_t key_len, size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!kv || (!key && key_len != 0) || !out_len)
        return TDB_ERR_INVALID_ARG;
    tdb_blob_desc_t desc = {0};
    tdb_status_t st = tdb_blob_read_meta_header(kv, key, key_len, &desc);
    if (st != TDB_OK)
        return st;
    *out_len = (size_t)desc.logical_size;
    return TDB_OK;
}

/**
 * @brief tdb_blob_stream.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param cb Callback function.
 * @param arg Callback/user context.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_stream(tdb_kv_t* kv, const void* key, size_t key_len, tdb_blob_stream_cb cb,
                           void* arg) {
    if (!kv || (!key && key_len != 0) || !cb)
        return TDB_ERR_INVALID_ARG;

    tdb_blob_desc_t desc = {0};
    uint32_t* inline_pgnos = NULL;
    uint16_t inline_count = 0;
    tdb_status_t st = tdb_blob_read_meta_full(kv, key, key_len, &desc, &inline_pgnos, &inline_count);
    if (st != TDB_OK)
        return st;

    MPOOL* mp = tdb_kv_get_mpool(kv);
    if (!mp) {
        free(inline_pgnos);
        return TDB_ERR_IO;
    }
    const size_t chunk = desc.chunk_size;
    size_t remaining = (size_t)desc.logical_size;

    /* Stream inline pages first. */
    for (uint16_t i = 0; i < inline_count && remaining; i++) {
        void* page = mpool_get(mp, (pgno_t)inline_pgnos[i], 0);
        if (!page) {
            free(inline_pgnos);
            return TDB_ERR_IO;
        }
        const size_t take = (remaining < chunk) ? remaining : chunk;
        tdb_status_t rc = cb(page, take, arg);
        (void)mpool_put(mp, page, 0);
        if (rc != TDB_OK) {
            free(inline_pgnos);
            return rc;
        }
        remaining -= take;
    }
    free(inline_pgnos);

    /* Stream index blocks. */
    uint32_t* pgnos = NULL;
    uint16_t pgnos_cap = 0;
    for (uint16_t bi = 0; bi < desc.index_block_count && remaining; bi++) {
        tdb_tmp_buf_t idx_key = {0};
        st = tdb_blob_build_index_key(key, key_len, desc.generation, (uint32_t)bi, &idx_key);
        if (st != TDB_OK)
            return st;

        tdb_blob_t v = {0};
        st = tdb_kv_get(kv, idx_key.p, idx_key.len, &v);
        tdb_tmp_buf_free(&idx_key);
        if (st != TDB_OK)
            return st;

        uint16_t got = 0;
        st = tdb_index_block_peek_count((const uint8_t*)v.data, v.len, &got);
        if (st != TDB_OK || got == 0)
            return TDB_ERR_CORRUPT;

        if (got > pgnos_cap) {
            uint32_t* np = (uint32_t*)realloc(pgnos, (size_t)got * sizeof(uint32_t));
            if (!np) {
                free(pgnos);
                return TDB_ERR_NO_MEMORY;
            }
            pgnos = np;
            pgnos_cap = got;
        }
        uint16_t cap = pgnos_cap;
        st = tdb_index_block_decode((const uint8_t*)v.data, v.len, pgnos, &cap);
        if (st != TDB_OK || cap != got) {
            free(pgnos);
            return (st != TDB_OK) ? st : TDB_ERR_CORRUPT;
        }

        for (uint16_t i = 0; i < got && remaining; i++) {
            void* page = mpool_get(mp, (pgno_t)pgnos[i], 0);
            if (!page) {
                free(pgnos);
                return TDB_ERR_IO;
            }
            const size_t take = (remaining < chunk) ? remaining : chunk;
            tdb_status_t rc = cb(page, take, arg);
            (void)mpool_put(mp, page, 0);
            if (rc != TDB_OK) {
                free(pgnos);
                return rc;
            }
            remaining -= take;
        }
    }
    free(pgnos);
    return TDB_OK;
}

/**
 * @brief tdb_blob_get_into.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out Output buffer pointer.
 * @param out_len Output pointer receiving len.
 * @param saved_len_out Output pointer receiving previous length.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_get_into(tdb_kv_t* kv, const void* key, size_t key_len, void* out,
                             size_t out_len, size_t* saved_len_out) {
    if (saved_len_out)
        *saved_len_out = 0;
    if (!kv || (!key && key_len != 0) || (!out && out_len != 0))
        return TDB_ERR_INVALID_ARG;

    size_t total_len = 0;
    tdb_status_t st = tdb_blob_get_len(kv, key, key_len, &total_len);
    if (st != TDB_OK)
        return st;
    if (saved_len_out)
        *saved_len_out = total_len;
    if (!out || out_len == 0)
        return TDB_OK;

    tdb_blob_copy_ctx_t c = {.dst = (uint8_t*)out, .cap = out_len, .written = 0};
    return tdb_blob_stream(kv, key, key_len, tdb_blob_copy_cb, &c);
}

/**
 * @brief tdb_blob_truncate.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param new_size Length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_truncate(tdb_kv_t* kv, const void* key, size_t key_len, size_t new_size) {
    if (!kv || (!key && key_len != 0))
        return TDB_ERR_INVALID_ARG;

    tdb_blob_desc_t desc = {0};
    uint32_t* inline_pgnos = NULL;
    uint16_t inline_count = 0;
    tdb_status_t st = tdb_blob_read_meta_full(kv, key, key_len, &desc, &inline_pgnos, &inline_count);
    if (st != TDB_OK)
        return st;

    if (new_size >= (size_t)desc.logical_size) {
        free(inline_pgnos);
        return TDB_OK;
    }

    uint32_t* all = NULL;
    size_t all_count = 0;
    st = tdb_blob_collect_all_pgnos(kv, key, key_len, &desc, inline_pgnos, inline_count, &all,
                                   &all_count);
    free(inline_pgnos);
    if (st != TDB_OK)
        return st;

    const size_t chunk = (size_t)desc.chunk_size;
    const size_t keep_pages = (new_size == 0) ? 0 : ((new_size + chunk - 1) / chunk);
    if (keep_pages > all_count) {
        free(all);
        return TDB_ERR_CORRUPT;
    }

    /* Free dropped pages. */
    MPOOL* mp = tdb_kv_get_mpool(kv);
    if (!mp) {
        free(all);
        return TDB_ERR_IO;
    }
    for (size_t i = keep_pages; i < all_count; i++) {
        (void)mpool_free_pgno(mp, (pgno_t)all[i]);
    }

    /* Commit a new generation referencing the kept prefix. */
    tdb_tmp_buf_t meta_key = {0};
    st = tdb_blob_build_meta_key(key, key_len, &meta_key);
    if (st != TDB_OK) {
        free(all);
        return st;
    }
    const size_t meta_key_len = meta_key.len;
    size_t max_meta_val = 0;
    st = tdb_kv_max_val_len(kv, meta_key_len, &max_meta_val);
    tdb_tmp_buf_free(&meta_key);
    if (st != TDB_OK) {
        free(all);
        return st;
    }
    if (max_meta_val < TDB_BLOB_DESC_HDR_SIZE) {
        free(all);
        return TDB_ERR_INVALID_ARG;
    }
    const uint16_t inline_cap = (uint16_t)((max_meta_val - TDB_BLOB_DESC_HDR_SIZE) / 4u);
    const uint16_t new_inline_count =
        (keep_pages < (size_t)inline_cap) ? (uint16_t)keep_pages : inline_cap;

    tdb_tmp_buf_t tmp_idx_key = {0};
    st = tdb_blob_build_index_key(key, key_len, 1u, 0u, &tmp_idx_key);
    if (st != TDB_OK) {
        free(all);
        return st;
    }
    size_t max_idx_val = 0;
    st = tdb_kv_max_val_len(kv, tmp_idx_key.len, &max_idx_val);
    tdb_tmp_buf_free(&tmp_idx_key);
    if (st != TDB_OK) {
        free(all);
        return st;
    }
    if (max_idx_val < TDB_INDEX_BLOCK_HDR_SIZE) {
        free(all);
        return TDB_ERR_INVALID_ARG;
    }
    size_t tmp_cap = (max_idx_val - TDB_INDEX_BLOCK_HDR_SIZE) / 4u;
    if (tmp_cap > 0xFFFFu)
        tmp_cap = 0xFFFFu;
    const uint16_t idx_cap = (uint16_t)tmp_cap;
    if (idx_cap == 0) {
        free(all);
        return TDB_ERR_INVALID_ARG;
    }

    const uint32_t new_gen = tdb_blob_next_generation(desc.generation);
    uint16_t index_block_count = 0;
    uint32_t block_i = 0;

    size_t pos = (size_t)new_inline_count;
    while (pos < keep_pages) {
        const size_t left = keep_pages - pos;
        const uint16_t take = (left < (size_t)idx_cap) ? (uint16_t)left : idx_cap;
        st = tdb_blob_write_index_block(kv, key, key_len, new_gen, block_i, &all[pos], take);
        if (st != TDB_OK) {
            free(all);
            return st;
        }
        index_block_count++;
        block_i++;
        pos += take;
    }

    /* Write meta last (commit). */
    tdb_tmp_buf_t mk = {0};
    st = tdb_blob_build_meta_key(key, key_len, &mk);
    if (st != TDB_OK) {
        free(all);
        return st;
    }
    const size_t mk_len = mk.len;
    const size_t meta_need = (size_t)TDB_BLOB_DESC_HDR_SIZE + (size_t)new_inline_count * 4u;
    uint8_t* mv = (uint8_t*)malloc(meta_need);
    if (!mv) {
        tdb_tmp_buf_free(&mk);
        free(all);
        return TDB_ERR_NO_MEMORY;
    }
    tdb_blob_desc_t new_desc = {
        .version = (uint8_t)TDB_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = desc.chunk_size,
        .logical_size = (uint32_t)new_size,
        .generation = new_gen,
        .index_block_count = index_block_count,
        .inline_pgno_count = new_inline_count,
    };
    size_t enc_len = 0;
    st = tdb_blob_desc_encode(mv, meta_need, &new_desc, all, new_inline_count, &enc_len);
    if (st == TDB_OK)
        st = tdb_kv_put(kv, mk.p, mk_len, mv, enc_len);
    free(mv);
    tdb_tmp_buf_free(&mk);
    if (st != TDB_OK) {
        free(all);
        return st;
    }

    /* Delete old generation's index keys. */
    (void)tdb_blob_delete_generation(kv, key, key_len, desc.generation, desc.index_block_count);
    free(all);
    return TDB_OK;
}

/**
 * @brief tdb_blob_write_range.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param offset Offset value.
 * @param data Value bytes.
 * @param len Length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_blob_write_range(tdb_kv_t* kv, const void* key, size_t key_len, size_t offset,
                                const void* data, size_t len) {
    if (!kv || (!key && key_len != 0) || (!data && len != 0))
        return TDB_ERR_INVALID_ARG;

    tdb_blob_desc_t desc = {0};
    uint32_t* inline_pgnos = NULL;
    uint16_t inline_count = 0;
    tdb_status_t st = tdb_blob_read_meta_full(kv, key, key_len, &desc, &inline_pgnos, &inline_count);
    if (st != TDB_OK)
        return st;

    uint32_t* all = NULL;
    size_t all_count = 0;
    st = tdb_blob_collect_all_pgnos(kv, key, key_len, &desc, inline_pgnos, inline_count, &all,
                                   &all_count);
    free(inline_pgnos);
    if (st != TDB_OK)
        return st;

    MPOOL* mp = tdb_kv_get_mpool(kv);
    if (!mp) {
        free(all);
        return TDB_ERR_IO;
    }
    const size_t chunk = (size_t)desc.chunk_size;

    const size_t end = offset + len;
    const size_t old_size = (size_t)desc.logical_size;
    const size_t new_size = (end > old_size) ? end : old_size;
    const size_t old_pages = (old_size == 0) ? 0 : ((old_size + chunk - 1) / chunk);
    const size_t new_pages = (new_size == 0) ? 0 : ((new_size + chunk - 1) / chunk);

    if (old_pages > all_count) {
        free(all);
        return TDB_ERR_CORRUPT;
    }

    /* Extend pgno list if needed (new pages will be allocated as we write). */
    if (new_pages > all_count) {
        uint32_t* p = (uint32_t*)realloc(all, new_pages * sizeof(uint32_t));
        if (!p) {
            free(all);
            return TDB_ERR_NO_MEMORY;
        }
        all = p;
        for (size_t i = all_count; i < new_pages; i++)
            all[i] = 0;
        all_count = new_pages;
    }

    /* Track replaced old pages for freeing. */
    uint32_t* replaced = (uint32_t*)malloc(all_count * sizeof(uint32_t));
    size_t replaced_n = 0;
    if (!replaced && all_count) {
        free(all);
        return TDB_ERR_NO_MEMORY;
    }

    uint8_t* tmp = (uint8_t*)malloc(chunk);
    if (!tmp && chunk) {
        free(replaced);
        free(all);
        return TDB_ERR_NO_MEMORY;
    }

    const uint8_t* in = (const uint8_t*)data;
    size_t pos_in = 0;
    size_t cur = offset;
    while (pos_in < len) {
        const size_t pg_i = cur / chunk;
        const size_t pg_off = cur % chunk;
        const size_t take = ((len - pos_in) < (chunk - pg_off)) ? (len - pos_in) : (chunk - pg_off);

        memset(tmp, 0, chunk);
        if (pg_i < old_pages) {
            void* page = mpool_get(mp, (pgno_t)all[pg_i], 0);
            if (!page) {
                free(tmp);
                free(replaced);
                free(all);
                return TDB_ERR_IO;
            }
            memcpy(tmp, page, chunk);
            (void)mpool_put(mp, page, 0);
        }
        memcpy(tmp + pg_off, in + pos_in, take);

        /* Allocate new page with updated data. */
        pgno_t new_pgno = PGNO_INVALID;
        void* page2 = mpool_new(mp, &new_pgno);
        if (!page2 || new_pgno == PGNO_INVALID) {
            free(tmp);
            free(replaced);
            free(all);
            return TDB_ERR_NO_MEMORY;
        }
        memcpy(page2, tmp, chunk);
        if (mpool_put(mp, page2, MPOOL_DIRTY) != 0) {
            free(tmp);
            free(replaced);
            free(all);
            return TDB_ERR_IO;
        }

        if (pg_i < old_pages)
            replaced[replaced_n++] = all[pg_i];
        all[pg_i] = (uint32_t)new_pgno;

        pos_in += take;
        cur += take;
    }
    free(tmp);

    /* Commit new generation with updated pgno list and new_size. */
    tdb_tmp_buf_t meta_key = {0};
    st = tdb_blob_build_meta_key(key, key_len, &meta_key);
    if (st != TDB_OK) {
        free(replaced);
        free(all);
        return st;
    }
    const size_t meta_key_len = meta_key.len;
    size_t max_meta_val = 0;
    st = tdb_kv_max_val_len(kv, meta_key_len, &max_meta_val);
    tdb_tmp_buf_free(&meta_key);
    if (st != TDB_OK) {
        free(replaced);
        free(all);
        return st;
    }
    if (max_meta_val < TDB_BLOB_DESC_HDR_SIZE) {
        free(replaced);
        free(all);
        return TDB_ERR_INVALID_ARG;
    }
    const uint16_t inline_cap = (uint16_t)((max_meta_val - TDB_BLOB_DESC_HDR_SIZE) / 4u);
    const uint16_t new_inline_count =
        (new_pages < (size_t)inline_cap) ? (uint16_t)new_pages : inline_cap;

    tdb_tmp_buf_t tmp_idx_key = {0};
    st = tdb_blob_build_index_key(key, key_len, 1u, 0u, &tmp_idx_key);
    if (st != TDB_OK) {
        free(replaced);
        free(all);
        return st;
    }
    size_t max_idx_val = 0;
    st = tdb_kv_max_val_len(kv, tmp_idx_key.len, &max_idx_val);
    tdb_tmp_buf_free(&tmp_idx_key);
    if (st != TDB_OK) {
        free(replaced);
        free(all);
        return st;
    }
    if (max_idx_val < TDB_INDEX_BLOCK_HDR_SIZE) {
        free(replaced);
        free(all);
        return TDB_ERR_INVALID_ARG;
    }
    size_t tmp_cap = (max_idx_val - TDB_INDEX_BLOCK_HDR_SIZE) / 4u;
    if (tmp_cap > 0xFFFFu)
        tmp_cap = 0xFFFFu;
    const uint16_t idx_cap = (uint16_t)tmp_cap;
    if (idx_cap == 0) {
        free(replaced);
        free(all);
        return TDB_ERR_INVALID_ARG;
    }

    const uint32_t new_gen = tdb_blob_next_generation(desc.generation);
    uint16_t index_block_count = 0;
    uint32_t block_i = 0;
    size_t pos = (size_t)new_inline_count;
    while (pos < new_pages) {
        const size_t left = new_pages - pos;
        const uint16_t take = (left < (size_t)idx_cap) ? (uint16_t)left : idx_cap;
        st = tdb_blob_write_index_block(kv, key, key_len, new_gen, block_i, &all[pos], take);
        if (st != TDB_OK) {
            free(replaced);
            free(all);
            return st;
        }
        index_block_count++;
        block_i++;
        pos += take;
    }

    tdb_tmp_buf_t mk = {0};
    st = tdb_blob_build_meta_key(key, key_len, &mk);
    if (st != TDB_OK) {
        free(replaced);
        free(all);
        return st;
    }
    const size_t mk_len = mk.len;
    const size_t meta_need = (size_t)TDB_BLOB_DESC_HDR_SIZE + (size_t)new_inline_count * 4u;
    uint8_t* mv = (uint8_t*)malloc(meta_need);
    if (!mv) {
        tdb_tmp_buf_free(&mk);
        free(replaced);
        free(all);
        return TDB_ERR_NO_MEMORY;
    }
    tdb_blob_desc_t new_desc = {
        .version = (uint8_t)TDB_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = desc.chunk_size,
        .logical_size = (uint32_t)new_size,
        .generation = new_gen,
        .index_block_count = index_block_count,
        .inline_pgno_count = new_inline_count,
    };
    size_t enc_len = 0;
    st = tdb_blob_desc_encode(mv, meta_need, &new_desc, all, new_inline_count, &enc_len);
    if (st == TDB_OK)
        st = tdb_kv_put(kv, mk.p, mk_len, mv, enc_len);
    free(mv);
    tdb_tmp_buf_free(&mk);
    if (st != TDB_OK) {
        free(replaced);
        free(all);
        return st;
    }

    /* Delete old index keys and free replaced old pages. */
    (void)tdb_blob_delete_generation(kv, key, key_len, desc.generation, desc.index_block_count);
    for (size_t i = 0; i < replaced_n; i++) {
        (void)mpool_free_pgno(mp, (pgno_t)replaced[i]);
    }
    free(replaced);
    free(all);
    return TDB_OK;
}
