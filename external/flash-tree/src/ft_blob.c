// ft_blob.c - Large values via Meta-Key + Index-Keys + Data-Pages.
//
// This layer stores big payloads without BSD btree overflow pages by:
// - appending payload to mpool pages (pgno)
// - storing pgno lists in KV values (Meta + Index blocks)
//
// Atomicity model (single-writer):
// - write new data pages
// - write new index keys (generation-specific)
// - write meta key last (commit point)

#include "flash_tree.h"

#include "ft_blob.h"
#include "ft_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define __DBINTERFACE_PRIVATE
#include "ft_bsd_db.h"
#include "ft_bsd_btree.h"
#include "ft_bsd_mpool.h"
#include "ft_kv_internal.h"

/* ============== Endian helpers ============== */

static inline void ft_u16_be_write(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}
static inline uint16_t ft_u16_be_read(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static inline void ft_u32_be_write(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}
static inline uint32_t ft_u32_be_read(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ============== Small scratch buffers (avoid heap for small keys) ============== */

typedef struct ft_tmp_buf {
    uint8_t* p;
    size_t len;
    uint8_t stack[64];
} ft_tmp_buf_t;

static inline ft_status_t ft_tmp_buf_alloc(ft_tmp_buf_t* b, size_t n) {
    if (!b)
        return FT_ERR_INVALID_ARG;
    b->p = NULL;
    b->len = 0;
    if (n <= sizeof(b->stack)) {
        b->p = b->stack;
        b->len = n;
        return FT_OK;
    }
    b->p = (uint8_t*)malloc(n);
    if (!b->p)
        return FT_ERR_NO_MEMORY;
    b->len = n;
    return FT_OK;
}

static inline void ft_tmp_buf_free(ft_tmp_buf_t* b) {
    if (!b)
        return;
    if (b->p && b->p != b->stack)
        free(b->p);
    b->p = NULL;
    b->len = 0;
}

/* ============== Streaming helpers ============== */

typedef struct ft_blob_copy_ctx {
    uint8_t* dst;
    size_t cap;
    size_t written;
} ft_blob_copy_ctx_t;

static ft_status_t ft_blob_copy_cb(const void* data, size_t len, void* arg) {
    ft_blob_copy_ctx_t* c = (ft_blob_copy_ctx_t*)arg;
    if (!c)
        return FT_ERR_INVALID_ARG;
    if (c->written >= c->cap)
        return FT_OK;
    size_t space = c->cap - c->written;
    size_t take = (len < space) ? len : space;
    memcpy(c->dst + c->written, data, take);
    c->written += take;
    return FT_OK;
}

/* ============== Key helpers ============== */

static ft_status_t ft_blob_build_meta_key(const void* user_key, size_t user_key_len,
                                          ft_tmp_buf_t* out) {
    if (!out)
        return FT_ERR_INVALID_ARG;
    if (!user_key && user_key_len != 0)
        return FT_ERR_INVALID_ARG;
    const size_t n = user_key_len + 2u;
    ft_status_t st = ft_tmp_buf_alloc(out, n);
    if (st != FT_OK)
        return st;
    if (user_key_len)
        memcpy(out->p, user_key, user_key_len);
    out->p[user_key_len + 0] = 0x00;
    out->p[user_key_len + 1] = (uint8_t)'M';
    return FT_OK;
}

static ft_status_t ft_blob_build_index_key(const void* user_key, size_t user_key_len,
                                           uint32_t generation, uint32_t block_i,
                                           ft_tmp_buf_t* out) {
    if (!out)
        return FT_ERR_INVALID_ARG;
    if (!user_key && user_key_len != 0)
        return FT_ERR_INVALID_ARG;
    const size_t n = user_key_len + 1u + 1u + 4u + 4u;
    ft_status_t st = ft_tmp_buf_alloc(out, n);
    if (st != FT_OK)
        return st;
    if (user_key_len)
        memcpy(out->p, user_key, user_key_len);
    out->p[user_key_len + 0] = 0x00;
    out->p[user_key_len + 1] = (uint8_t)'C';
    ft_u32_be_write(&out->p[user_key_len + 2], generation);
    ft_u32_be_write(&out->p[user_key_len + 6], block_i);
    return FT_OK;
}

/* ============== Format helpers (public for tests via src/ft_blob.h) ============== */

ft_status_t ft_blob_desc_encode(uint8_t* out, size_t out_cap, const ft_blob_desc_t* desc,
                                const uint32_t* inline_pgnos, uint16_t inline_pgno_count,
                                size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!out || !desc || (!inline_pgnos && inline_pgno_count != 0) || !out_len)
        return FT_ERR_INVALID_ARG;

    if (desc->version != FT_BLOB_DESC_VERSION)
        return FT_ERR_INVALID_ARG;
    if (desc->reserved != 0)
        return FT_ERR_INVALID_ARG;
    if (desc->inline_pgno_count != inline_pgno_count)
        return FT_ERR_INVALID_ARG;

    const size_t need = (size_t)FT_BLOB_DESC_HDR_SIZE + (size_t)inline_pgno_count * 4u;
    if (need > out_cap)
        return FT_ERR_INVALID_ARG;

    out[0] = desc->version;
    out[1] = desc->reserved;
    ft_u16_be_write(&out[2], desc->chunk_size);
    ft_u32_be_write(&out[4], desc->logical_size);
    ft_u32_be_write(&out[8], desc->generation);
    ft_u16_be_write(&out[12], desc->index_block_count);
    ft_u16_be_write(&out[14], inline_pgno_count);
    for (uint16_t i = 0; i < inline_pgno_count; i++) {
        ft_u32_be_write(&out[16u + (size_t)i * 4u], inline_pgnos[i]);
    }

    *out_len = need;
    return FT_OK;
}

ft_status_t ft_blob_desc_decode(const uint8_t* buf, size_t len, ft_blob_desc_t* out_desc,
                                uint32_t* out_inline_pgnos, uint16_t* inout_inline_pgno_count) {
    if (out_desc)
        memset(out_desc, 0, sizeof(*out_desc));
    if (!buf || !out_desc || !inout_inline_pgno_count)
        return FT_ERR_INVALID_ARG;

    if (len < FT_BLOB_DESC_HDR_SIZE)
        return FT_ERR_CORRUPT;
    const uint8_t version = buf[0];
    const uint8_t reserved = buf[1];
    if (version != FT_BLOB_DESC_VERSION)
        return FT_ERR_CORRUPT;
    if (reserved != 0)
        return FT_ERR_CORRUPT;

    const uint16_t chunk_size = ft_u16_be_read(&buf[2]);
    const uint32_t logical_size = ft_u32_be_read(&buf[4]);
    const uint32_t generation = ft_u32_be_read(&buf[8]);
    const uint16_t index_block_count = ft_u16_be_read(&buf[12]);
    const uint16_t inline_count = ft_u16_be_read(&buf[14]);

    const size_t need = (size_t)FT_BLOB_DESC_HDR_SIZE + (size_t)inline_count * 4u;
    if (need != len && need > len)
        return FT_ERR_CORRUPT;
    if (need > len)
        return FT_ERR_CORRUPT;

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
        return FT_ERR_INVALID_ARG;
    }
    if (inline_count && !out_inline_pgnos)
        return FT_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < inline_count; i++) {
        out_inline_pgnos[i] = ft_u32_be_read(&buf[16u + (size_t)i * 4u]);
    }
    *inout_inline_pgno_count = inline_count;
    return FT_OK;
}

ft_status_t ft_index_block_encode(uint8_t* out, size_t out_cap, const uint32_t* pgnos,
                                  uint16_t count, size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!out || (!pgnos && count != 0) || !out_len)
        return FT_ERR_INVALID_ARG;
    const size_t need = (size_t)FT_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need > out_cap)
        return FT_ERR_INVALID_ARG;
    ft_u16_be_write(&out[0], count);
    for (uint16_t i = 0; i < count; i++) {
        ft_u32_be_write(&out[2u + (size_t)i * 4u], pgnos[i]);
    }
    *out_len = need;
    return FT_OK;
}

ft_status_t ft_index_block_decode(const uint8_t* buf, size_t len, uint32_t* out_pgnos,
                                  uint16_t* inout_count) {
    if (!buf || !inout_count)
        return FT_ERR_INVALID_ARG;
    if (len < FT_INDEX_BLOCK_HDR_SIZE)
        return FT_ERR_CORRUPT;
    const uint16_t count = ft_u16_be_read(&buf[0]);
    const size_t need = (size_t)FT_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need != len && need > len)
        return FT_ERR_CORRUPT;
    if (need > len)
        return FT_ERR_CORRUPT;
    const uint16_t cap = *inout_count;
    if (count > cap) {
        *inout_count = count;
        return FT_ERR_INVALID_ARG;
    }
    if (count && !out_pgnos)
        return FT_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < count; i++) {
        out_pgnos[i] = ft_u32_be_read(&buf[2u + (size_t)i * 4u]);
    }
    *inout_count = count;
    return FT_OK;
}

/* ============== Fast header peeks (avoid double decode + malloc churn) ============== */

static ft_status_t ft_blob_desc_peek(const uint8_t* buf, size_t len, ft_blob_desc_t* out_desc,
                                     uint16_t* out_inline_count) {
    if (out_desc)
        memset(out_desc, 0, sizeof(*out_desc));
    if (out_inline_count)
        *out_inline_count = 0;
    if (!buf || !out_desc || !out_inline_count)
        return FT_ERR_INVALID_ARG;
    if (len < FT_BLOB_DESC_HDR_SIZE)
        return FT_ERR_CORRUPT;
    if (buf[0] != FT_BLOB_DESC_VERSION)
        return FT_ERR_CORRUPT;
    if (buf[1] != 0)
        return FT_ERR_CORRUPT;

    const uint16_t inline_count = ft_u16_be_read(&buf[14]);
    const size_t need = (size_t)FT_BLOB_DESC_HDR_SIZE + (size_t)inline_count * 4u;
    if (need != len)
        return FT_ERR_CORRUPT;

    out_desc->version = buf[0];
    out_desc->reserved = 0;
    out_desc->chunk_size = ft_u16_be_read(&buf[2]);
    out_desc->logical_size = ft_u32_be_read(&buf[4]);
    out_desc->generation = ft_u32_be_read(&buf[8]);
    out_desc->index_block_count = ft_u16_be_read(&buf[12]);
    out_desc->inline_pgno_count = inline_count;
    *out_inline_count = inline_count;
    return FT_OK;
}

static ft_status_t ft_index_block_peek_count(const uint8_t* buf, size_t len, uint16_t* out_count) {
    if (out_count)
        *out_count = 0;
    if (!buf || !out_count)
        return FT_ERR_INVALID_ARG;
    if (len < FT_INDEX_BLOCK_HDR_SIZE)
        return FT_ERR_CORRUPT;
    const uint16_t count = ft_u16_be_read(&buf[0]);
    const size_t need = (size_t)FT_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need != len)
        return FT_ERR_CORRUPT;
    *out_count = count;
    return FT_OK;
}

/* ============== Blob writer (opaque) ============== */

struct ft_blob_writer {
    ft_kv_t* kv;
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

static uint32_t ft_blob_next_generation(uint32_t old_gen) {
    uint32_t g = old_gen + 1u;
    if (g == 0u)
        g = 1u;
    return g;
}

static ft_status_t ft_blob_read_meta_header(ft_kv_t* kv, const void* user_key, size_t user_key_len,
                                            ft_blob_desc_t* out_desc) {
    if (out_desc)
        memset(out_desc, 0, sizeof(*out_desc));
    if (!kv || (!user_key && user_key_len != 0) || !out_desc)
        return FT_ERR_INVALID_ARG;

    ft_tmp_buf_t meta_key = {0};
    ft_status_t st = ft_blob_build_meta_key(user_key, user_key_len, &meta_key);
    if (st != FT_OK)
        return st;

    ft_blob_t v = {0};
    st = ft_kv_get(kv, meta_key.p, meta_key.len, &v);
    ft_tmp_buf_free(&meta_key);
    if (st != FT_OK)
        return st;

    uint16_t inline_count = 0;
    return ft_blob_desc_peek((const uint8_t*)v.data, v.len, out_desc, &inline_count);
}

static ft_status_t ft_blob_read_meta_full(ft_kv_t* kv, const void* user_key, size_t user_key_len,
                                          ft_blob_desc_t* out_desc, uint32_t** out_inline_pgnos,
                                          uint16_t* out_inline_count) {
    if (out_inline_pgnos)
        *out_inline_pgnos = NULL;
    if (out_inline_count)
        *out_inline_count = 0;
    if (!kv || (!user_key && user_key_len != 0) || !out_desc || !out_inline_pgnos ||
        !out_inline_count) {
        return FT_ERR_INVALID_ARG;
    }

    ft_tmp_buf_t meta_key = {0};
    ft_status_t st = ft_blob_build_meta_key(user_key, user_key_len, &meta_key);
    if (st != FT_OK)
        return st;

    ft_blob_t v = {0};
    st = ft_kv_get(kv, meta_key.p, meta_key.len, &v);
    ft_tmp_buf_free(&meta_key);
    if (st != FT_OK)
        return st;

    uint16_t inline_count = 0;
    st = ft_blob_desc_peek((const uint8_t*)v.data, v.len, out_desc, &inline_count);
    if (st != FT_OK)
        return st;
    if (inline_count == 0) {
        *out_inline_pgnos = NULL;
        *out_inline_count = 0;
        return FT_OK;
    }

    uint32_t* p = (uint32_t*)malloc((size_t)inline_count * sizeof(uint32_t));
    if (!p)
        return FT_ERR_NO_MEMORY;
    uint16_t cap = inline_count;
    st = ft_blob_desc_decode((const uint8_t*)v.data, v.len, out_desc, p, &cap);
    if (st != FT_OK || cap != inline_count) {
        free(p);
        return (st != FT_OK) ? st : FT_ERR_CORRUPT;
    }
    *out_inline_pgnos = p;
    *out_inline_count = inline_count;
    return FT_OK;
}

static ft_status_t ft_blob_write_index_block(ft_kv_t* kv, const void* user_key, size_t user_key_len,
                                             uint32_t generation, uint32_t block_i,
                                             const uint32_t* pgnos, uint16_t count) {
    if (!kv || (!user_key && user_key_len != 0) || (!pgnos && count != 0))
        return FT_ERR_INVALID_ARG;

    ft_tmp_buf_t idx_key = {0};
    ft_status_t st = ft_blob_build_index_key(user_key, user_key_len, generation, block_i, &idx_key);
    if (st != FT_OK)
        return st;

    /* Compute max value length and encode accordingly. */
    size_t max_val = 0;
    st = ft_kv_max_val_len(kv, idx_key.len, &max_val);
    if (st != FT_OK) {
        ft_tmp_buf_free(&idx_key);
        return st;
    }

    const size_t need = (size_t)FT_INDEX_BLOCK_HDR_SIZE + (size_t)count * 4u;
    if (need > max_val) {
        ft_tmp_buf_free(&idx_key);
        return FT_ERR_INVALID_ARG;
    }

    uint8_t* buf = (uint8_t*)malloc(need);
    if (!buf) {
        ft_tmp_buf_free(&idx_key);
        return FT_ERR_NO_MEMORY;
    }
    size_t enc_len = 0;
    st = ft_index_block_encode(buf, need, pgnos, count, &enc_len);
    if (st != FT_OK) {
        free(buf);
        ft_tmp_buf_free(&idx_key);
        return st;
    }

    st = ft_kv_put(kv, idx_key.p, idx_key.len, buf, enc_len);
    free(buf);
    ft_tmp_buf_free(&idx_key);
    return st;
}

static ft_status_t ft_blob_delete_generation(ft_kv_t* kv, const void* user_key, size_t user_key_len,
                                             uint32_t generation, uint16_t index_block_count) {
    if (!kv || (!user_key && user_key_len != 0))
        return FT_ERR_INVALID_ARG;
    for (uint16_t i = 0; i < index_block_count; i++) {
        ft_tmp_buf_t k = {0};
        ft_status_t st =
            ft_blob_build_index_key(user_key, user_key_len, generation, (uint32_t)i, &k);
        if (st != FT_OK)
            return st;
        (void)ft_kv_del(kv, k.p, k.len); /* best-effort */
        ft_tmp_buf_free(&k);
    }
    return FT_OK;
}

static ft_status_t ft_blob_collect_all_pgnos(ft_kv_t* kv, const void* user_key, size_t user_key_len,
                                             const ft_blob_desc_t* desc,
                                             const uint32_t* inline_pgnos, uint16_t inline_count,
                                             uint32_t** out_all, size_t* out_count) {
    if (out_all)
        *out_all = NULL;
    if (out_count)
        *out_count = 0;
    if (!kv || !desc || (!inline_pgnos && inline_count != 0) || !out_all || !out_count)
        return FT_ERR_INVALID_ARG;

    const size_t chunk = (size_t)desc->chunk_size;
    if (chunk == 0)
        return FT_ERR_CORRUPT;

    const size_t pages_needed =
        (desc->logical_size == 0) ? 0 : (((size_t)desc->logical_size + chunk - 1u) / chunk);

    if ((size_t)inline_count > pages_needed)
        return FT_ERR_CORRUPT;

    size_t total = (size_t)inline_count;
    uint32_t* all = (uint32_t*)malloc((pages_needed ? pages_needed : 1u) * sizeof(uint32_t));
    if (!all)
        return FT_ERR_NO_MEMORY;
    for (uint16_t i = 0; i < inline_count; i++)
        all[i] = inline_pgnos[i];

    /* Reuse a single decode buffer sized to the largest index block seen. */
    uint32_t* tmp = NULL;
    uint16_t tmp_cap = 0;

    for (uint16_t bi = 0; bi < desc->index_block_count; bi++) {
        ft_tmp_buf_t idx_key = {0};
        ft_status_t st = ft_blob_build_index_key(user_key, user_key_len, desc->generation,
                                                 (uint32_t)bi, &idx_key);
        if (st != FT_OK) {
            free(tmp);
            free(all);
            return st;
        }

        ft_blob_t v = {0};
        st = ft_kv_get(kv, idx_key.p, idx_key.len, &v);
        ft_tmp_buf_free(&idx_key);
        if (st != FT_OK) {
            free(tmp);
            free(all);
            return st;
        }

        uint16_t got = 0;
        st = ft_index_block_peek_count((const uint8_t*)v.data, v.len, &got);
        if (st != FT_OK || got == 0) {
            free(tmp);
            free(all);
            return FT_ERR_CORRUPT;
        }

        if (got > tmp_cap) {
            uint32_t* np = (uint32_t*)realloc(tmp, (size_t)got * sizeof(uint32_t));
            if (!np) {
                free(tmp);
                free(all);
                return FT_ERR_NO_MEMORY;
            }
            tmp = np;
            tmp_cap = got;
        }
        uint16_t cap = tmp_cap;
        st = ft_index_block_decode((const uint8_t*)v.data, v.len, tmp, &cap);
        if (st != FT_OK || cap != got) {
            free(tmp);
            free(all);
            return (st != FT_OK) ? st : FT_ERR_CORRUPT;
        }

        if (total + got > pages_needed) {
            free(tmp);
            free(all);
            return FT_ERR_CORRUPT;
        }
        memcpy(&all[total], tmp, (size_t)got * sizeof(uint32_t));
        total += got;
    }

    free(tmp);

    /* Validate that the descriptor matches the pgno list length. */
    if (total != pages_needed) {
        free(all);
        return FT_ERR_CORRUPT;
    }

    *out_all = all;
    *out_count = total;
    return FT_OK;
}

/* ============== Public API ============== */

ft_status_t ft_blob_chunk_size(ft_kv_t* kv, size_t* out_chunk_size) {
    if (out_chunk_size)
        *out_chunk_size = 0;
    if (!kv || !out_chunk_size)
        return FT_ERR_INVALID_ARG;
    MPOOL* mp = ft_kv_get_mpool(kv);
    if (!mp)
        return FT_ERR_IO;
    *out_chunk_size = (size_t)mp->pagesize;
    return (*out_chunk_size > 0) ? FT_OK : FT_ERR_IO;
}

ft_status_t ft_blob_writer_init(ft_kv_t* kv, const void* key, size_t key_len,
                                ft_blob_writer_t** out_writer) {
    if (!out_writer)
        return FT_ERR_INVALID_ARG;
    *out_writer = NULL;
    if (!kv || (!key && key_len != 0))
        return FT_ERR_INVALID_ARG;

    MPOOL* mp = ft_kv_get_mpool(kv);
    if (!mp)
        return FT_ERR_IO;

    /* Read existing meta (optional) - header only (no inline list allocation). */
    ft_blob_desc_t old_desc = {0};
    uint32_t old_gen = 0;
    uint16_t old_index_blocks = 0;
    ft_status_t st_meta = ft_blob_read_meta_header(kv, key, key_len, &old_desc);
    if (st_meta == FT_OK) {
        old_gen = old_desc.generation;
        old_index_blocks = old_desc.index_block_count;
    }

    /* Compute capacities from KV limits. */
    ft_tmp_buf_t meta_key = {0};
    ft_status_t st = ft_blob_build_meta_key(key, key_len, &meta_key);
    if (st != FT_OK) {
        return st;
    }
    const size_t meta_key_len = meta_key.len;

    size_t max_meta_val = 0;
    st = ft_kv_max_val_len(kv, meta_key_len, &max_meta_val);
    ft_tmp_buf_free(&meta_key);
    if (st != FT_OK) {
        return st;
    }
    if (max_meta_val < FT_BLOB_DESC_HDR_SIZE) {
        return FT_ERR_INVALID_ARG;
    }
    const uint16_t inline_cap = (uint16_t)((max_meta_val - FT_BLOB_DESC_HDR_SIZE) / 4u);

    ft_tmp_buf_t idx_key = {0};
    st = ft_blob_build_index_key(key, key_len, 1u, 0u, &idx_key);
    if (st != FT_OK) {
        return st;
    }
    const size_t idx_key_len = idx_key.len;
    size_t max_idx_val = 0;
    st = ft_kv_max_val_len(kv, idx_key_len, &max_idx_val);
    ft_tmp_buf_free(&idx_key);
    if (st != FT_OK) {
        return st;
    }
    if (max_idx_val < FT_INDEX_BLOCK_HDR_SIZE) {
        return FT_ERR_INVALID_ARG;
    }
    size_t tmp_cap = (max_idx_val - FT_INDEX_BLOCK_HDR_SIZE) / 4u;
    if (tmp_cap > 0xFFFFu)
        tmp_cap = 0xFFFFu;
    const uint16_t idx_cap = (uint16_t)tmp_cap;
    if (idx_cap == 0) {
        return FT_ERR_INVALID_ARG;
    }

    /* Allocate writer */
    ft_blob_writer_t* w = (ft_blob_writer_t*)calloc(1, sizeof(*w));
    if (!w) {
        return FT_ERR_NO_MEMORY;
    }

    w->kv = kv;
    w->user_key = (uint8_t*)malloc(key_len);
    if (key_len && !w->user_key) {
        free(w);
        return FT_ERR_NO_MEMORY;
    }
    if (key_len)
        memcpy(w->user_key, key, key_len);
    w->user_key_len = key_len;

    w->old_generation = (st_meta == FT_OK) ? old_gen : 0u;
    w->old_index_block_count = (st_meta == FT_OK) ? old_index_blocks : 0u;
    w->generation = ft_blob_next_generation((st_meta == FT_OK) ? old_gen : 0u);

    const uint32_t pagesize = mp->pagesize;
    if (pagesize == 0 || pagesize > 0xFFFFu) {
        free(w->user_key);
        free(w);
        return FT_ERR_UNSUPPORTED;
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
        return FT_ERR_NO_MEMORY;
    }
    *out_writer = w;
    return FT_OK;
}

static ft_status_t ft_blob_writer_record_pgno(ft_blob_writer_t* w, pgno_t pgno) {
    if (!w)
        return FT_ERR_INVALID_ARG;

    /* Record pgno */
    if (w->inline_count < w->inline_cap) {
        w->inline_pgnos[w->inline_count++] = (uint32_t)pgno;
    } else {
        w->idx_pgnos[w->idx_count++] = (uint32_t)pgno;
        if (w->idx_count == w->idx_cap) {
            ft_status_t st =
                ft_blob_write_index_block(w->kv, w->user_key, w->user_key_len, w->generation,
                                          w->block_i, w->idx_pgnos, w->idx_count);
            if (st != FT_OK)
                return st;
            w->index_block_count++;
            w->block_i++;
            w->idx_count = 0;
        }
    }
    return FT_OK;
}

static ft_status_t ft_blob_writer_flush_current_page(ft_blob_writer_t* w) {
    if (!w)
        return FT_ERR_INVALID_ARG;
    if (!w->cur_page)
        return FT_OK;

    MPOOL* mp = ft_kv_get_mpool(w->kv);
    if (!mp)
        return FT_ERR_IO;

    /* Zero padding is already present because we memset() the page on allocation. */
    if (mpool_put(mp, w->cur_page, MPOOL_DIRTY) != 0)
        return FT_ERR_IO;
    w->cur_page = NULL;
    w->cur_pgno = PGNO_INVALID;
    w->cur_fill = 0;
    return FT_OK;
}

static ft_status_t ft_blob_writer_ensure_page(ft_blob_writer_t* w) {
    if (!w)
        return FT_ERR_INVALID_ARG;
    if (w->cur_page)
        return FT_OK;

    MPOOL* mp = ft_kv_get_mpool(w->kv);
    if (!mp)
        return FT_ERR_IO;

    pgno_t pgno = PGNO_INVALID;
    uint8_t* page = (uint8_t*)mpool_new(mp, &pgno);
    if (!page || pgno == PGNO_INVALID) {
        errno = ENOMEM;
        return FT_ERR_NO_MEMORY;
    }
    memset(page, 0, mp->pagesize);

    ft_status_t st = ft_blob_writer_record_pgno(w, pgno);
    if (st != FT_OK) {
        /* Best-effort: don't leak a pinned page on failure. */
        (void)mpool_put(mp, page, 0);
        return st;
    }

    w->cur_page = page;
    w->cur_pgno = pgno;
    w->cur_fill = 0;
    return FT_OK;
}

ft_status_t ft_blob_write(ft_blob_writer_t* w, const void* data, size_t len) {
    if (!w || (!data && len != 0))
        return FT_ERR_INVALID_ARG;
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        ft_status_t st = ft_blob_writer_ensure_page(w);
        if (st != FT_OK)
            return st;

        const size_t space = (size_t)w->chunk_size - w->cur_fill;
        const size_t take = (len < space) ? len : space;
        memcpy(w->cur_page + w->cur_fill, p, take);
        w->cur_fill += take;
        p += take;
        len -= take;
        w->logical_size += (uint32_t)take;
        if (w->cur_fill == (size_t)w->chunk_size) {
            st = ft_blob_writer_flush_current_page(w);
            if (st != FT_OK)
                return st;
        }
    }
    return FT_OK;
}

static void ft_blob_writer_free(ft_blob_writer_t* w) {
    if (!w)
        return;
    free(w->idx_pgnos);
    free(w->inline_pgnos);
    free(w->user_key);
    free(w);
}

void ft_blob_abort(ft_blob_writer_t* w) {
    /* No commit: data pages are unreferenced (may be reclaimed by block-level GC if implemented).
     */
    ft_blob_writer_free(w);
}

ft_status_t ft_blob_finish(ft_blob_writer_t* w) {
    if (!w)
        return FT_ERR_INVALID_ARG;

    /* Snapshot previous generation's referenced pages (before we overwrite meta). */
    uint32_t* old_all = NULL;
    size_t old_all_count = 0;
    uint32_t old_gen = w->old_generation;
    uint16_t old_index_block_count = w->old_index_block_count;
    if (old_gen) {
        ft_blob_desc_t old_desc = {0};
        uint32_t* old_inline = NULL;
        uint16_t old_inline_count = 0;
        ft_status_t st_old = ft_blob_read_meta_full(w->kv, w->user_key, w->user_key_len, &old_desc,
                                                    &old_inline, &old_inline_count);
        if (st_old == FT_OK && old_desc.generation == old_gen) {
            (void)ft_blob_collect_all_pgnos(w->kv, w->user_key, w->user_key_len, &old_desc,
                                            old_inline, old_inline_count, &old_all, &old_all_count);
        }
        free(old_inline);
    }

    /* Flush tail page if needed. */
    if (w->cur_page) {
        ft_status_t st = ft_blob_writer_flush_current_page(w);
        if (st != FT_OK) {
            free(old_all);
            ft_blob_writer_free(w);
            return st;
        }
    }

    /* Flush tail index block. */
    if (w->idx_count) {
        ft_status_t st =
            ft_blob_write_index_block(w->kv, w->user_key, w->user_key_len, w->generation,
                                      w->block_i, w->idx_pgnos, w->idx_count);
        if (st != FT_OK) {
            free(old_all);
            ft_blob_writer_free(w);
            return st;
        }
        w->index_block_count++;
        w->block_i++;
        w->idx_count = 0;
    }

    /* Write Meta-Key (commit point). */
    ft_tmp_buf_t meta_key = {0};
    ft_status_t st = ft_blob_build_meta_key(w->user_key, w->user_key_len, &meta_key);
    if (st != FT_OK) {
        free(old_all);
        ft_blob_writer_free(w);
        return st;
    }
    const size_t meta_key_len = meta_key.len;

    size_t max_meta_val = 0;
    st = ft_kv_max_val_len(w->kv, meta_key_len, &max_meta_val);
    if (st != FT_OK) {
        ft_tmp_buf_free(&meta_key);
        free(old_all);
        ft_blob_writer_free(w);
        return st;
    }

    const size_t meta_need = (size_t)FT_BLOB_DESC_HDR_SIZE + (size_t)w->inline_count * 4u;
    if (meta_need > max_meta_val) {
        ft_tmp_buf_free(&meta_key);
        free(old_all);
        ft_blob_writer_free(w);
        return FT_ERR_INVALID_ARG;
    }
    uint8_t* meta_val = (uint8_t*)malloc(meta_need);
    if (!meta_val) {
        ft_tmp_buf_free(&meta_key);
        free(old_all);
        ft_blob_writer_free(w);
        return FT_ERR_NO_MEMORY;
    }

    ft_blob_desc_t desc = {
        .version = (uint8_t)FT_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = w->chunk_size,
        .logical_size = w->logical_size,
        .generation = w->generation,
        .index_block_count = w->index_block_count,
        .inline_pgno_count = w->inline_count,
    };
    size_t enc_len = 0;
    st =
        ft_blob_desc_encode(meta_val, meta_need, &desc, w->inline_pgnos, w->inline_count, &enc_len);
    if (st == FT_OK)
        st = ft_kv_put(w->kv, meta_key.p, meta_key_len, meta_val, enc_len);
    free(meta_val);
    ft_tmp_buf_free(&meta_key);
    if (st != FT_OK) {
        free(old_all);
        ft_blob_writer_free(w);
        return st;
    }

    /* Best-effort cleanup of previous generation (keys + data pages). */
    if (old_gen) {
        if (old_all) {
            MPOOL* mp = ft_kv_get_mpool(w->kv);
            if (mp) {
                for (size_t i = 0; i < old_all_count; i++) {
                    (void)mpool_free_pgno(mp, (pgno_t)old_all[i]);
                }
            }
            free(old_all);
        }
        (void)ft_blob_delete_generation(w->kv, w->user_key, w->user_key_len, old_gen,
                                        old_index_block_count);
    }

    ft_blob_writer_free(w);
    return FT_OK;
}

ft_status_t ft_blob_put(ft_kv_t* kv, const void* key, size_t key_len, const void* data,
                        size_t len) {
    ft_blob_writer_t* w = NULL;
    ft_status_t st = ft_blob_writer_init(kv, key, key_len, &w);
    if (st != FT_OK)
        return st;
    st = ft_blob_write(w, data, len);
    if (st != FT_OK) {
        ft_blob_abort(w);
        return st;
    }
    return ft_blob_finish(w);
}

ft_status_t ft_blob_get_len(ft_kv_t* kv, const void* key, size_t key_len, size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!kv || (!key && key_len != 0) || !out_len)
        return FT_ERR_INVALID_ARG;
    ft_blob_desc_t desc = {0};
    ft_status_t st = ft_blob_read_meta_header(kv, key, key_len, &desc);
    if (st != FT_OK)
        return st;
    *out_len = (size_t)desc.logical_size;
    return FT_OK;
}

ft_status_t ft_blob_stream(ft_kv_t* kv, const void* key, size_t key_len, ft_blob_stream_cb cb,
                           void* arg) {
    if (!kv || (!key && key_len != 0) || !cb)
        return FT_ERR_INVALID_ARG;

    ft_blob_desc_t desc = {0};
    uint32_t* inline_pgnos = NULL;
    uint16_t inline_count = 0;
    ft_status_t st = ft_blob_read_meta_full(kv, key, key_len, &desc, &inline_pgnos, &inline_count);
    if (st != FT_OK)
        return st;

    MPOOL* mp = ft_kv_get_mpool(kv);
    if (!mp) {
        free(inline_pgnos);
        return FT_ERR_IO;
    }
    const size_t chunk = desc.chunk_size;
    size_t remaining = (size_t)desc.logical_size;

    /* Stream inline pages first. */
    for (uint16_t i = 0; i < inline_count && remaining; i++) {
        void* page = mpool_get(mp, (pgno_t)inline_pgnos[i], 0);
        if (!page) {
            free(inline_pgnos);
            return FT_ERR_IO;
        }
        const size_t take = (remaining < chunk) ? remaining : chunk;
        ft_status_t rc = cb(page, take, arg);
        (void)mpool_put(mp, page, 0);
        if (rc != FT_OK) {
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
        ft_tmp_buf_t idx_key = {0};
        st = ft_blob_build_index_key(key, key_len, desc.generation, (uint32_t)bi, &idx_key);
        if (st != FT_OK)
            return st;

        ft_blob_t v = {0};
        st = ft_kv_get(kv, idx_key.p, idx_key.len, &v);
        ft_tmp_buf_free(&idx_key);
        if (st != FT_OK)
            return st;

        uint16_t got = 0;
        st = ft_index_block_peek_count((const uint8_t*)v.data, v.len, &got);
        if (st != FT_OK || got == 0)
            return FT_ERR_CORRUPT;

        if (got > pgnos_cap) {
            uint32_t* np = (uint32_t*)realloc(pgnos, (size_t)got * sizeof(uint32_t));
            if (!np) {
                free(pgnos);
                return FT_ERR_NO_MEMORY;
            }
            pgnos = np;
            pgnos_cap = got;
        }
        uint16_t cap = pgnos_cap;
        st = ft_index_block_decode((const uint8_t*)v.data, v.len, pgnos, &cap);
        if (st != FT_OK || cap != got) {
            free(pgnos);
            return (st != FT_OK) ? st : FT_ERR_CORRUPT;
        }

        for (uint16_t i = 0; i < got && remaining; i++) {
            void* page = mpool_get(mp, (pgno_t)pgnos[i], 0);
            if (!page) {
                free(pgnos);
                return FT_ERR_IO;
            }
            const size_t take = (remaining < chunk) ? remaining : chunk;
            ft_status_t rc = cb(page, take, arg);
            (void)mpool_put(mp, page, 0);
            if (rc != FT_OK) {
                free(pgnos);
                return rc;
            }
            remaining -= take;
        }
    }
    free(pgnos);
    return FT_OK;
}

ft_status_t ft_blob_get_into(ft_kv_t* kv, const void* key, size_t key_len, void* out,
                             size_t out_len, size_t* saved_len_out) {
    if (saved_len_out)
        *saved_len_out = 0;
    if (!kv || (!key && key_len != 0) || (!out && out_len != 0))
        return FT_ERR_INVALID_ARG;

    size_t total_len = 0;
    ft_status_t st = ft_blob_get_len(kv, key, key_len, &total_len);
    if (st != FT_OK)
        return st;
    if (saved_len_out)
        *saved_len_out = total_len;
    if (!out || out_len == 0)
        return FT_OK;

    ft_blob_copy_ctx_t c = {.dst = (uint8_t*)out, .cap = out_len, .written = 0};
    return ft_blob_stream(kv, key, key_len, ft_blob_copy_cb, &c);
}

ft_status_t ft_blob_truncate(ft_kv_t* kv, const void* key, size_t key_len, size_t new_size) {
    if (!kv || (!key && key_len != 0))
        return FT_ERR_INVALID_ARG;

    ft_blob_desc_t desc = {0};
    uint32_t* inline_pgnos = NULL;
    uint16_t inline_count = 0;
    ft_status_t st = ft_blob_read_meta_full(kv, key, key_len, &desc, &inline_pgnos, &inline_count);
    if (st != FT_OK)
        return st;

    if (new_size >= (size_t)desc.logical_size) {
        free(inline_pgnos);
        return FT_OK;
    }

    uint32_t* all = NULL;
    size_t all_count = 0;
    st = ft_blob_collect_all_pgnos(kv, key, key_len, &desc, inline_pgnos, inline_count, &all,
                                   &all_count);
    free(inline_pgnos);
    if (st != FT_OK)
        return st;

    const size_t chunk = (size_t)desc.chunk_size;
    const size_t keep_pages = (new_size == 0) ? 0 : ((new_size + chunk - 1) / chunk);
    if (keep_pages > all_count) {
        free(all);
        return FT_ERR_CORRUPT;
    }

    /* Free dropped pages. */
    MPOOL* mp = ft_kv_get_mpool(kv);
    if (!mp) {
        free(all);
        return FT_ERR_IO;
    }
    for (size_t i = keep_pages; i < all_count; i++) {
        (void)mpool_free_pgno(mp, (pgno_t)all[i]);
    }

    /* Commit a new generation referencing the kept prefix. */
    ft_tmp_buf_t meta_key = {0};
    st = ft_blob_build_meta_key(key, key_len, &meta_key);
    if (st != FT_OK) {
        free(all);
        return st;
    }
    const size_t meta_key_len = meta_key.len;
    size_t max_meta_val = 0;
    st = ft_kv_max_val_len(kv, meta_key_len, &max_meta_val);
    ft_tmp_buf_free(&meta_key);
    if (st != FT_OK) {
        free(all);
        return st;
    }
    if (max_meta_val < FT_BLOB_DESC_HDR_SIZE) {
        free(all);
        return FT_ERR_INVALID_ARG;
    }
    const uint16_t inline_cap = (uint16_t)((max_meta_val - FT_BLOB_DESC_HDR_SIZE) / 4u);
    const uint16_t new_inline_count =
        (keep_pages < (size_t)inline_cap) ? (uint16_t)keep_pages : inline_cap;

    ft_tmp_buf_t tmp_idx_key = {0};
    st = ft_blob_build_index_key(key, key_len, 1u, 0u, &tmp_idx_key);
    if (st != FT_OK) {
        free(all);
        return st;
    }
    size_t max_idx_val = 0;
    st = ft_kv_max_val_len(kv, tmp_idx_key.len, &max_idx_val);
    ft_tmp_buf_free(&tmp_idx_key);
    if (st != FT_OK) {
        free(all);
        return st;
    }
    if (max_idx_val < FT_INDEX_BLOCK_HDR_SIZE) {
        free(all);
        return FT_ERR_INVALID_ARG;
    }
    size_t tmp_cap = (max_idx_val - FT_INDEX_BLOCK_HDR_SIZE) / 4u;
    if (tmp_cap > 0xFFFFu)
        tmp_cap = 0xFFFFu;
    const uint16_t idx_cap = (uint16_t)tmp_cap;
    if (idx_cap == 0) {
        free(all);
        return FT_ERR_INVALID_ARG;
    }

    const uint32_t new_gen = ft_blob_next_generation(desc.generation);
    uint16_t index_block_count = 0;
    uint32_t block_i = 0;

    size_t pos = (size_t)new_inline_count;
    while (pos < keep_pages) {
        const size_t left = keep_pages - pos;
        const uint16_t take = (left < (size_t)idx_cap) ? (uint16_t)left : idx_cap;
        st = ft_blob_write_index_block(kv, key, key_len, new_gen, block_i, &all[pos], take);
        if (st != FT_OK) {
            free(all);
            return st;
        }
        index_block_count++;
        block_i++;
        pos += take;
    }

    /* Write meta last (commit). */
    ft_tmp_buf_t mk = {0};
    st = ft_blob_build_meta_key(key, key_len, &mk);
    if (st != FT_OK) {
        free(all);
        return st;
    }
    const size_t mk_len = mk.len;
    const size_t meta_need = (size_t)FT_BLOB_DESC_HDR_SIZE + (size_t)new_inline_count * 4u;
    uint8_t* mv = (uint8_t*)malloc(meta_need);
    if (!mv) {
        ft_tmp_buf_free(&mk);
        free(all);
        return FT_ERR_NO_MEMORY;
    }
    ft_blob_desc_t new_desc = {
        .version = (uint8_t)FT_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = desc.chunk_size,
        .logical_size = (uint32_t)new_size,
        .generation = new_gen,
        .index_block_count = index_block_count,
        .inline_pgno_count = new_inline_count,
    };
    size_t enc_len = 0;
    st = ft_blob_desc_encode(mv, meta_need, &new_desc, all, new_inline_count, &enc_len);
    if (st == FT_OK)
        st = ft_kv_put(kv, mk.p, mk_len, mv, enc_len);
    free(mv);
    ft_tmp_buf_free(&mk);
    if (st != FT_OK) {
        free(all);
        return st;
    }

    /* Delete old generation's index keys. */
    (void)ft_blob_delete_generation(kv, key, key_len, desc.generation, desc.index_block_count);
    free(all);
    return FT_OK;
}

ft_status_t ft_blob_write_range(ft_kv_t* kv, const void* key, size_t key_len, size_t offset,
                                const void* data, size_t len) {
    if (!kv || (!key && key_len != 0) || (!data && len != 0))
        return FT_ERR_INVALID_ARG;

    ft_blob_desc_t desc = {0};
    uint32_t* inline_pgnos = NULL;
    uint16_t inline_count = 0;
    ft_status_t st = ft_blob_read_meta_full(kv, key, key_len, &desc, &inline_pgnos, &inline_count);
    if (st != FT_OK)
        return st;

    uint32_t* all = NULL;
    size_t all_count = 0;
    st = ft_blob_collect_all_pgnos(kv, key, key_len, &desc, inline_pgnos, inline_count, &all,
                                   &all_count);
    free(inline_pgnos);
    if (st != FT_OK)
        return st;

    MPOOL* mp = ft_kv_get_mpool(kv);
    if (!mp) {
        free(all);
        return FT_ERR_IO;
    }
    const size_t chunk = (size_t)desc.chunk_size;

    const size_t end = offset + len;
    const size_t old_size = (size_t)desc.logical_size;
    const size_t new_size = (end > old_size) ? end : old_size;
    const size_t old_pages = (old_size == 0) ? 0 : ((old_size + chunk - 1) / chunk);
    const size_t new_pages = (new_size == 0) ? 0 : ((new_size + chunk - 1) / chunk);

    if (old_pages > all_count) {
        free(all);
        return FT_ERR_CORRUPT;
    }

    /* Extend pgno list if needed (new pages will be allocated as we write). */
    if (new_pages > all_count) {
        uint32_t* p = (uint32_t*)realloc(all, new_pages * sizeof(uint32_t));
        if (!p) {
            free(all);
            return FT_ERR_NO_MEMORY;
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
        return FT_ERR_NO_MEMORY;
    }

    uint8_t* tmp = (uint8_t*)malloc(chunk);
    if (!tmp && chunk) {
        free(replaced);
        free(all);
        return FT_ERR_NO_MEMORY;
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
                return FT_ERR_IO;
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
            return FT_ERR_NO_MEMORY;
        }
        memcpy(page2, tmp, chunk);
        if (mpool_put(mp, page2, MPOOL_DIRTY) != 0) {
            free(tmp);
            free(replaced);
            free(all);
            return FT_ERR_IO;
        }

        if (pg_i < old_pages)
            replaced[replaced_n++] = all[pg_i];
        all[pg_i] = (uint32_t)new_pgno;

        pos_in += take;
        cur += take;
    }
    free(tmp);

    /* Commit new generation with updated pgno list and new_size. */
    ft_tmp_buf_t meta_key = {0};
    st = ft_blob_build_meta_key(key, key_len, &meta_key);
    if (st != FT_OK) {
        free(replaced);
        free(all);
        return st;
    }
    const size_t meta_key_len = meta_key.len;
    size_t max_meta_val = 0;
    st = ft_kv_max_val_len(kv, meta_key_len, &max_meta_val);
    ft_tmp_buf_free(&meta_key);
    if (st != FT_OK) {
        free(replaced);
        free(all);
        return st;
    }
    if (max_meta_val < FT_BLOB_DESC_HDR_SIZE) {
        free(replaced);
        free(all);
        return FT_ERR_INVALID_ARG;
    }
    const uint16_t inline_cap = (uint16_t)((max_meta_val - FT_BLOB_DESC_HDR_SIZE) / 4u);
    const uint16_t new_inline_count =
        (new_pages < (size_t)inline_cap) ? (uint16_t)new_pages : inline_cap;

    ft_tmp_buf_t tmp_idx_key = {0};
    st = ft_blob_build_index_key(key, key_len, 1u, 0u, &tmp_idx_key);
    if (st != FT_OK) {
        free(replaced);
        free(all);
        return st;
    }
    size_t max_idx_val = 0;
    st = ft_kv_max_val_len(kv, tmp_idx_key.len, &max_idx_val);
    ft_tmp_buf_free(&tmp_idx_key);
    if (st != FT_OK) {
        free(replaced);
        free(all);
        return st;
    }
    if (max_idx_val < FT_INDEX_BLOCK_HDR_SIZE) {
        free(replaced);
        free(all);
        return FT_ERR_INVALID_ARG;
    }
    size_t tmp_cap = (max_idx_val - FT_INDEX_BLOCK_HDR_SIZE) / 4u;
    if (tmp_cap > 0xFFFFu)
        tmp_cap = 0xFFFFu;
    const uint16_t idx_cap = (uint16_t)tmp_cap;
    if (idx_cap == 0) {
        free(replaced);
        free(all);
        return FT_ERR_INVALID_ARG;
    }

    const uint32_t new_gen = ft_blob_next_generation(desc.generation);
    uint16_t index_block_count = 0;
    uint32_t block_i = 0;
    size_t pos = (size_t)new_inline_count;
    while (pos < new_pages) {
        const size_t left = new_pages - pos;
        const uint16_t take = (left < (size_t)idx_cap) ? (uint16_t)left : idx_cap;
        st = ft_blob_write_index_block(kv, key, key_len, new_gen, block_i, &all[pos], take);
        if (st != FT_OK) {
            free(replaced);
            free(all);
            return st;
        }
        index_block_count++;
        block_i++;
        pos += take;
    }

    ft_tmp_buf_t mk = {0};
    st = ft_blob_build_meta_key(key, key_len, &mk);
    if (st != FT_OK) {
        free(replaced);
        free(all);
        return st;
    }
    const size_t mk_len = mk.len;
    const size_t meta_need = (size_t)FT_BLOB_DESC_HDR_SIZE + (size_t)new_inline_count * 4u;
    uint8_t* mv = (uint8_t*)malloc(meta_need);
    if (!mv) {
        ft_tmp_buf_free(&mk);
        free(replaced);
        free(all);
        return FT_ERR_NO_MEMORY;
    }
    ft_blob_desc_t new_desc = {
        .version = (uint8_t)FT_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = desc.chunk_size,
        .logical_size = (uint32_t)new_size,
        .generation = new_gen,
        .index_block_count = index_block_count,
        .inline_pgno_count = new_inline_count,
    };
    size_t enc_len = 0;
    st = ft_blob_desc_encode(mv, meta_need, &new_desc, all, new_inline_count, &enc_len);
    if (st == FT_OK)
        st = ft_kv_put(kv, mk.p, mk_len, mv, enc_len);
    free(mv);
    ft_tmp_buf_free(&mk);
    if (st != FT_OK) {
        free(replaced);
        free(all);
        return st;
    }

    /* Delete old index keys and free replaced old pages. */
    (void)ft_blob_delete_generation(kv, key, key_len, desc.generation, desc.index_block_count);
    for (size_t i = 0; i < replaced_n; i++) {
        (void)mpool_free_pgno(mp, (pgno_t)replaced[i]);
    }
    free(replaced);
    free(all);
    return FT_OK;
}
