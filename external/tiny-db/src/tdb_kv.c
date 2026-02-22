/*
 * tdb_kv.c - B-Tree KV-DB with Copy-on-Write Log Storage
 *
 * Uses BSD B-Tree for sorted key storage with our custom mpool
 * that writes pages append-only to flash (no Read-Modify-Write).
 *
 * Architecture:
 *   tdb_kv.c → BSD B-Tree (bt_*.c) → mpool.c → tdb_blockdev → Flash
 *                                      ↑
 *                           Copy-on-Write, append-only
 *
 * Key Features:
 * - Flash-friendly: no Read-Modify-Write operations
 * - Copy-on-Write: old versions remain valid during updates
 * - Cursor API: efficient prefix iteration
 * - Incremental GC: reclaim space from old page versions
 */

#include "tiny_db.h"
#include "tdb_blockdev.h"
#include "tdb_kv_bind.h"
#include "tdb_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __DBINTERFACE_PRIVATE
#define __DBINTERFACE_PRIVATE
#endif
#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"
#include "tdb_bsd_mpool.h"
#include "tdb_kv_internal.h"
#include "tdb_page_policy.h"

/* ============== KV Handle ============== */

struct tdb_kv_cursor {
    tdb_kv_t* kv;
    int started; /* Has iteration begun? */
    int exhausted;
    int has_current;

    /* Prefix filter */
    uint8_t* prefix;
    size_t prefix_len;

    /* Optional: start scanning from first key >= start_key. */
    uint8_t* start_key;
    size_t start_len;

    /* Current key/value from B-Tree */
    DBT cur_key;
    DBT cur_val;
};

/* ============== Status Conversion ============== */

/**
 * @brief tdb_from_bt_status.
 * @param rc Backend return code.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_from_bt_status(int rc) {
    if (rc == RET_SUCCESS)
        return TDB_OK;
    if (rc == RET_SPECIAL)
        return TDB_ERR_NOT_FOUND;
    if (rc == RET_ERROR) {
        if (errno == EINVAL || errno == E2BIG)
            return TDB_ERR_INVALID_ARG;
        if (errno == EFTYPE)
            return TDB_ERR_CORRUPT;
        return TDB_ERR_IO;
    }
    return TDB_ERR_IO;
}

/* ============== Persistent GC state (system keys) ============== */

#define TDB_GC_PERSIST_INTERVAL 100u

static const uint8_t TDB_SYSKEY_GC_CURSOR[] = {0x00, 'g', 'c', '_', 'c', 'u', 'r', 's', 'o', 'r'};
static const uint8_t TDB_SYSKEY_FREE_HEAD[] = {0x00, 'f', 'r', 'e', 'e', '_', 'h', 'e', 'a', 'd'};
static const uint8_t TDB_SYSKEY_ALLOC_NEXT[] = {0x00, 'a', 'l', 'l', 'o', 'c', '_', 'n', 'e', 'x', 't'};

/**
 * @brief tdb_kv_sys_get_u32.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out Output buffer pointer.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_kv_sys_get_u32(tdb_kv_t* kv, const uint8_t* key, size_t key_len,
                                     uint32_t* out) {
    if (out)
        *out = 0;
    if (!kv || !kv->bdb || !key || key_len == 0 || !out)
        return TDB_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return tdb_from_bt_status(rc);
    if (d.size != 4)
        return TDB_ERR_CORRUPT;
    *out = tdb_u32_wire_read((const uint8_t*)d.data);
    return TDB_OK;
}

/**
 * @brief tdb_kv_sys_put_u32.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param v 32-bit value to persist.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_kv_sys_put_u32(tdb_kv_t* kv, const uint8_t* key, size_t key_len,
                                     uint32_t v) {
    if (!kv || !kv->bdb || !key || key_len == 0)
        return TDB_ERR_INVALID_ARG;
    uint8_t tmp[4];
    tdb_u32_wire_write(tmp, v);
    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {.data = (void*)tmp, .size = sizeof(tmp)};
    int rc = kv->bdb->put(kv->bdb, &k, &d, 0);
    return tdb_from_bt_status(rc);
}

/**
 * @brief tdb_kv_load_gc_state.
 * @param kv KV database handle.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_kv_load_gc_state(tdb_kv_t* kv) {
    if (!kv || !kv->bdb)
        return TDB_ERR_INVALID_ARG;

    BTREE* t = (BTREE*)kv->bdb->internal;
    MPOOL* mp = t ? t->bt_mp : NULL;
    if (!mp)
        return TDB_ERR_INVALID_ARG;

    /* Defaults */
    kv->gc_cursor = 0;
    kv->free_head = (uint32_t)PGNO_INVALID;
    kv->alloc_next = (uint32_t)mp->npages;

    uint32_t v = 0;
    if (tdb_kv_sys_get_u32(kv, TDB_SYSKEY_GC_CURSOR, sizeof(TDB_SYSKEY_GC_CURSOR), &v) == TDB_OK) {
        kv->gc_cursor = v;
    }
    if (tdb_kv_sys_get_u32(kv, TDB_SYSKEY_FREE_HEAD, sizeof(TDB_SYSKEY_FREE_HEAD), &v) == TDB_OK) {
        kv->free_head = v;
    }
    if (tdb_kv_sys_get_u32(kv, TDB_SYSKEY_ALLOC_NEXT, sizeof(TDB_SYSKEY_ALLOC_NEXT), &v) == TDB_OK) {
        kv->alloc_next = v;
    }

    /* Apply to mpool. */
    mp->gc_next_pgno = (pgno_t)kv->gc_cursor;
    mp->free_head = (pgno_t)kv->free_head;
    if ((pgno_t)kv->alloc_next > mp->npages)
        mp->npages = (pgno_t)kv->alloc_next;

    kv->gc_dirty = 0;
    kv->gc_persist_counter = 0;
    return TDB_OK;
}

/**
 * @brief tdb_kv_persist_gc_state.
 * @param kv KV database handle.
 * @return Status code (TDB_OK on success).
 */
static tdb_status_t tdb_kv_persist_gc_state(tdb_kv_t* kv) {
    if (!kv || !kv->bdb)
        return TDB_ERR_INVALID_ARG;

    BTREE* t = (BTREE*)kv->bdb->internal;
    MPOOL* mp = t ? t->bt_mp : NULL;
    if (!mp)
        return TDB_ERR_INVALID_ARG;

    /*
     * Persist the KV handle's view of GC state. This is the canonical state
     * for the higher layer (tests and periodic persistence).
     *
     * Keep mpool consistent with what we persist.
     */
    mp->gc_next_pgno = (pgno_t)kv->gc_cursor;
    mp->free_head = (pgno_t)kv->free_head;
    if ((pgno_t)kv->alloc_next > mp->npages)
        mp->npages = (pgno_t)kv->alloc_next;

    tdb_status_t st = TDB_OK;
    st = tdb_kv_sys_put_u32(kv, TDB_SYSKEY_GC_CURSOR, sizeof(TDB_SYSKEY_GC_CURSOR), kv->gc_cursor);
    if (st != TDB_OK)
        return st;
    st = tdb_kv_sys_put_u32(kv, TDB_SYSKEY_FREE_HEAD, sizeof(TDB_SYSKEY_FREE_HEAD), kv->free_head);
    if (st != TDB_OK)
        return st;
    st = tdb_kv_sys_put_u32(kv, TDB_SYSKEY_ALLOC_NEXT, sizeof(TDB_SYSKEY_ALLOC_NEXT), kv->alloc_next);
    if (st != TDB_OK)
        return st;

    kv->gc_dirty = 0;
    return TDB_OK;
}

/* ============== Public API ============== */

/**
 * @brief tdb_kv_get_mpool.
 * @param kv KV database handle.
 * @return Memory-pool handle, or NULL on failure.
 */
MPOOL* tdb_kv_get_mpool(tdb_kv_t* kv) {
    if (!kv || !kv->bdb)
        return NULL;
    BTREE* t = (BTREE*)kv->bdb->internal;
    if (!t)
        return NULL;
    return t->bt_mp;
}

/**
 * @brief tdb_kv_open.
 * @param out_kv KV database handle.
 * @param bdev Block-device descriptor.
 * @param cfg Input pointer.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_open(tdb_kv_t** out_kv, tdb_blockdev_t* bdev, const tdb_kv_cfg_t* cfg) {
    if (!out_kv || !bdev)
        return TDB_ERR_INVALID_ARG;

    tdb_status_t st = tdb_blockdev_validate(bdev);
    if (st != TDB_OK)
        return st;

    uint32_t start_page = TDB_KV_ROOT_PAGE;
    if (cfg)
        start_page = cfg->start_page;
    uint32_t eg = bdev->geom.erase_granularity ? bdev->geom.erase_granularity : 1u;
    uint32_t base_offset = start_page * eg;
    if (eg != 0 && start_page != 0 && base_offset / eg != start_page)
        return TDB_ERR_INVALID_ARG;
    if (base_offset >= bdev->geom.total_size_bytes)
        return TDB_ERR_INVALID_ARG;

    tdb_kv_t* kv = (tdb_kv_t*)calloc(1, sizeof(tdb_kv_t));
    if (!kv)
        return TDB_ERR_NO_MEMORY;
    kv->bdev = bdev;

    /* Bind blockdev for btree/mpool open only (single-threaded). */
    tdb_kv_bind(bdev, base_offset);

    /* Open B-Tree with our log-based mpool */
    BTREEINFO bi;
    memset(&bi, 0, sizeof(bi));
    /*
     * The mpool stores each B-Tree page in an append-only log record:
     *   record_size == erase_granularity
     *   record = header + page_payload
     *
     * Therefore the B-Tree page size must be (erase_granularity - header_size),
     * so that the on-flash record stays erase-aligned.
     *
     * Note: overflow pages are not supported (see tdb_bt_put.c). Keep minkeypage
     * at the minimum allowed by BSD btree (2) so the overflow cutoff is as
     * large as possible.
     */
    tdb_page_policy_t pol = {0};
    st = tdb_page_policy_compute_variant_b(&bdev->geom, sizeof(tdb_page_hdr_t), &pol);
    if (st != TDB_OK) {
        tdb_kv_unbind();
        free(kv);
        return st;
    }
    bi.psize = pol.page_size;
    bi.cachesize = 2u * bi.psize; /* Still minimal; mpool has its own cache. */
    bi.minkeypage = 2;
    /* Pin B-Tree on-disk byte order to the shared wire-endian configuration. */
#if (TDB_WIRE_ENDIAN == TDB_ENDIAN_LITTLE)
    bi.lorder = LITTLE_ENDIAN;
#else
    bi.lorder = BIG_ENDIAN;
#endif

    kv->bdb = __bt_open("tdb_kv", O_RDWR | O_CREAT, 0600, &bi, 0);
    tdb_kv_unbind();

    if (!kv->bdb) {
        tdb_kv_unbind();
        free(kv);
        return TDB_ERR_IO;
    }

    /* Wire back-pointer for mpool -> kv dirty tracking, and load persisted GC state. */
    {
        BTREE* t = (BTREE*)kv->bdb->internal;
        if (t && t->bt_mp) {
            t->bt_mp->owner_kv = kv;
            kv->alloc_next = (uint32_t)t->bt_mp->npages;
            kv->free_head = (uint32_t)t->bt_mp->free_head;
            kv->gc_cursor = (uint32_t)t->bt_mp->gc_next_pgno;
        } else {
            kv->alloc_next = 0;
            kv->free_head = (uint32_t)PGNO_INVALID;
            kv->gc_cursor = 0;
        }
        kv->gc_dirty = 0;
        kv->gc_persist_counter = 0;
        (void)tdb_kv_load_gc_state(kv);
    }

    *out_kv = kv;
    return TDB_OK;
}

/**
 * @brief tdb_kv_close.
 * @param kv KV database handle.
 */
void tdb_kv_close(tdb_kv_t* kv) {
    if (!kv)
        return;
    if (kv->gc_dirty) {
        (void)tdb_kv_persist_gc_state(kv);
    }
    if (kv->bdb)
        (void)kv->bdb->close(kv->bdb);
    free(kv->get_buf);
    free(kv);
}

/**
 * @brief tdb_kv_put.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param val Value bytes.
 * @param val_len Value length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_put(tdb_kv_t* kv, const void* key, size_t key_len, const void* val,
                      size_t val_len) {
    if (!kv || (!key && key_len != 0) || (!val && val_len != 0)) {
        return TDB_ERR_INVALID_ARG;
    }

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {.data = (void*)val, .size = val_len};
    int rc = kv->bdb->put(kv->bdb, &k, &d, 0);
    return tdb_from_bt_status(rc);
}

/**
 * @brief tdb_kv_get.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out Output buffer pointer.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_get(tdb_kv_t* kv, const void* key, size_t key_len, tdb_blob_t* out) {
    if (!kv || (!key && key_len != 0) || !out)
        return TDB_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return tdb_from_bt_status(rc);

    /* Copy to scratch buffer (B-Tree's buffer is volatile) */
    if (d.size > kv->get_cap) {
        size_t new_cap = (kv->get_cap == 0) ? 64 : kv->get_cap;
        while (new_cap < d.size)
            new_cap *= 2;
        uint8_t* p = (uint8_t*)realloc(kv->get_buf, new_cap);
        if (!p)
            return TDB_ERR_NO_MEMORY;
        kv->get_buf = p;
        kv->get_cap = new_cap;
    }
    if (d.size)
        memcpy(kv->get_buf, d.data, d.size);
    out->data = kv->get_buf;
    out->len = d.size;
    return TDB_OK;
}

/**
 * @brief tdb_kv_get_len.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out_len Output pointer receiving len.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_get_len(tdb_kv_t* kv, const void* key, size_t key_len, size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!kv || (!key && key_len != 0) || !out_len)
        return TDB_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return tdb_from_bt_status(rc);
    *out_len = d.size;
    return TDB_OK;
}

/**
 * @brief tdb_kv_get_into.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param out Output buffer pointer.
 * @param out_len Output pointer receiving len.
 * @param saved_len_out Output pointer receiving previous length.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_get_into(tdb_kv_t* kv, const void* key, size_t key_len, void* out, size_t out_len,
                           size_t* saved_len_out) {
    if (saved_len_out)
        *saved_len_out = 0;
    if (!kv || (!key && key_len != 0))
        return TDB_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return tdb_from_bt_status(rc);

    if (saved_len_out)
        *saved_len_out = d.size;
    if (out && out_len > 0) {
        size_t to_copy = (d.size < out_len) ? d.size : out_len;
        memcpy(out, d.data, to_copy);
    }
    return TDB_OK;
}

/**
 * @brief tdb_kv_del.
 * @param kv KV database handle.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_del(tdb_kv_t* kv, const void* key, size_t key_len) {
    if (!kv || (!key && key_len != 0))
        return TDB_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    int rc = kv->bdb->del(kv->bdb, &k, 0);
    return tdb_from_bt_status(rc);
}

/**
 * @brief tdb_kv_sync.
 * @param kv KV database handle.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_sync(tdb_kv_t* kv) {
    if (!kv || !kv->bdb)
        return TDB_ERR_INVALID_ARG;
    int rc = kv->bdb->sync(kv->bdb, 0);
    return tdb_from_bt_status(rc);
}

/**
 * @brief tdb_kv_max_val_len.
 * @param kv KV database handle.
 * @param key_len Key length in bytes.
 * @param out_max_val_len Output pointer receiving max val.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_max_val_len(const tdb_kv_t* kv, size_t key_len, size_t* out_max_val_len) {
    if (out_max_val_len)
        *out_max_val_len = 0;
    if (!kv || !kv->bdb || !out_max_val_len)
        return TDB_ERR_INVALID_ARG;

    BTREE* t = (BTREE*)kv->bdb->internal;
    if (!t)
        return TDB_ERR_INVALID_ARG;

    const size_t cutoff = (size_t)t->bt_ovflsize;
    if (key_len >= cutoff) {
        *out_max_val_len = 0;
        return TDB_OK;
    }

    *out_max_val_len = cutoff - key_len;
    return TDB_OK;
}

/* ============== Cursor API ============== */

/**
 * Open a cursor for iterating over keys with a given prefix.
 */
tdb_status_t tdb_kv_cursor_open_prefix(tdb_kv_t* kv, const void* prefix, size_t prefix_len,
                                     tdb_kv_cursor_t** out_cur) {
    if (!kv || (!prefix && prefix_len != 0) || !out_cur)
        return TDB_ERR_INVALID_ARG;

    tdb_kv_cursor_t* cur = (tdb_kv_cursor_t*)calloc(1, sizeof(*cur));
    if (!cur)
        return TDB_ERR_NO_MEMORY;

    cur->kv = kv;
    cur->started = 0;
    cur->exhausted = 0;
    cur->has_current = 0;

    if (prefix_len) {
        cur->prefix = (uint8_t*)malloc(prefix_len);
        if (!cur->prefix) {
            free(cur);
            return TDB_ERR_NO_MEMORY;
        }
        memcpy(cur->prefix, prefix, prefix_len);
        cur->prefix_len = prefix_len;
    }

    *out_cur = cur;
    return TDB_OK;
}

/**
 * Open a cursor for iterating over keys with a given prefix, starting at the
 * first key >= start_key (if provided).
 */
tdb_status_t tdb_kv_cursor_open_ge(tdb_kv_t* kv,
                                  const void* prefix, size_t prefix_len,
                                  const void* start_key, size_t start_len,
                                  tdb_kv_cursor_t** out_cur) {
    if (!out_cur)
        return TDB_ERR_INVALID_ARG;

    /* Fall back to plain prefix cursor when no explicit start key is given. */
    if (!start_key || start_len == 0) {
        return tdb_kv_cursor_open_prefix(kv, prefix, prefix_len, out_cur);
    }

    if (!kv || (!prefix && prefix_len != 0))
        return TDB_ERR_INVALID_ARG;

    tdb_status_t st = tdb_kv_cursor_open_prefix(kv, prefix, prefix_len, out_cur);
    if (st != TDB_OK)
        return st;

    tdb_kv_cursor_t* cur = *out_cur;
    cur->start_key = (uint8_t*)malloc(start_len);
    if (!cur->start_key) {
        tdb_kv_cursor_close(cur);
        *out_cur = NULL;
        return TDB_ERR_NO_MEMORY;
    }
    memcpy(cur->start_key, start_key, start_len);
    cur->start_len = start_len;
    return TDB_OK;
}

/**
 * @brief tdb_kv_cursor_next.
 * @param cur KV cursor handle.
 * @param out_has_item Output pointer receiving has item.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_cursor_next(tdb_kv_cursor_t* cur, int* out_has_item) {
    if (!cur || !out_has_item)
        return TDB_ERR_INVALID_ARG;
    *out_has_item = 0;
    cur->has_current = 0;

    if (cur->exhausted)
        return TDB_OK;

    int rc;
    if (!cur->started) {
        /* First call: position at prefix or first key */
        if (cur->start_len) {
            cur->cur_key.data = cur->start_key;
            cur->cur_key.size = cur->start_len;
            rc = cur->kv->bdb->seq(cur->kv->bdb, &cur->cur_key, &cur->cur_val, R_CURSOR);
        } else if (cur->prefix_len) {
            cur->cur_key.data = cur->prefix;
            cur->cur_key.size = cur->prefix_len;
            rc = cur->kv->bdb->seq(cur->kv->bdb, &cur->cur_key, &cur->cur_val, R_CURSOR);
        } else {
            rc = cur->kv->bdb->seq(cur->kv->bdb, &cur->cur_key, &cur->cur_val, R_FIRST);
        }
        cur->started = 1;
    } else {
        /* Subsequent calls: advance to next */
        rc = cur->kv->bdb->seq(cur->kv->bdb, &cur->cur_key, &cur->cur_val, R_NEXT);
    }

    if (rc == RET_SPECIAL) {
        cur->exhausted = 1;
        return TDB_OK;
    }
    if (rc != RET_SUCCESS) {
        cur->exhausted = 1;
        return tdb_from_bt_status(rc);
    }

    /* Check prefix filter */
    if (cur->prefix_len) {
        if (!tdb_has_prefix(cur->cur_key.data, cur->cur_key.size, cur->prefix, cur->prefix_len)) {
            cur->exhausted = 1;
            return TDB_OK;
        }
    }

    cur->has_current = 1;
    *out_has_item = 1;
    return TDB_OK;
}

/**
 * @brief tdb_kv_cursor_key.
 * @param cur KV cursor handle.
 * @param out_key Output pointer receiving key.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_cursor_key(const tdb_kv_cursor_t* cur, tdb_blob_t* out_key) {
    if (!cur || !out_key)
        return TDB_ERR_INVALID_ARG;
    if (!cur->has_current)
        return TDB_ERR_NOT_FOUND;
    out_key->data = cur->cur_key.data;
    out_key->len = cur->cur_key.size;
    return TDB_OK;
}

/**
 * @brief tdb_kv_cursor_val.
 * @param cur KV cursor handle.
 * @param out_val Output pointer receiving val.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_cursor_val(const tdb_kv_cursor_t* cur, tdb_blob_t* out_val) {
    if (!cur || !out_val)
        return TDB_ERR_INVALID_ARG;
    if (!cur->has_current)
        return TDB_ERR_NOT_FOUND;
    out_val->data = cur->cur_val.data;
    out_val->len = cur->cur_val.size;
    return TDB_OK;
}

/**
 * @brief tdb_kv_cursor_close.
 * @param cur KV cursor handle.
 */
void tdb_kv_cursor_close(tdb_kv_cursor_t* cur) {
    if (!cur)
        return;
    free(cur->prefix);
    free(cur->start_key);
    free(cur);
}

/**
 * @brief tdb_kv_iter_prefix.
 * @param kv KV database handle.
 * @param prefix Input pointer.
 * @param prefix_len Length in bytes.
 * @param cb Callback function.
 * @param arg Callback/user context.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_iter_prefix(tdb_kv_t* kv, const void* prefix, size_t prefix_len, tdb_key_cb cb,
                              void* arg) {
    if (!kv || (!prefix && prefix_len != 0) || !cb)
        return TDB_ERR_INVALID_ARG;

    tdb_kv_cursor_t* cur = NULL;
    tdb_status_t st = tdb_kv_cursor_open_prefix(kv, prefix, prefix_len, &cur);
    if (st != TDB_OK)
        return st;

    int has = 0;
    tdb_blob_t k, v;
    while ((st = tdb_kv_cursor_next(cur, &has)) == TDB_OK && has) {
        st = tdb_kv_cursor_key(cur, &k);
        if (st != TDB_OK)
            break;
        st = tdb_kv_cursor_val(cur, &v);
        if (st != TDB_OK)
            break;
        st = cb(k.data, k.len, v.data, v.len, arg);
        if (st != TDB_OK)
            break;
    }

    tdb_kv_cursor_close(cur);
    return st;
}

/**
 * @brief tdb_kv_gc_step.
 * @param kv KV database handle.
 * @param budget_bytes Length in bytes.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_kv_gc_step(tdb_kv_t* kv, size_t budget_bytes) {
    int rc = tdb_kv_gc_step_more(kv, budget_bytes);
    if (rc < 0)
        return (tdb_status_t)rc;
    return TDB_OK;
}

/**
 * @brief tdb_kv_gc_step_more.
 * @param kv KV database handle.
 * @param budget_bytes Length in bytes.
 * @return 1 if more GC work remains, 0 if idle, negative TDB_ERR_* on error.
 */
int tdb_kv_gc_step_more(tdb_kv_t* kv, size_t budget_bytes) {
    if (!kv || !kv->bdb)
        return TDB_ERR_INVALID_ARG;

    /* Sync B-Tree to flush all dirty pages to log */
    int rc = kv->bdb->sync(kv->bdb, 0);
    if (rc != RET_SUCCESS)
        return (int)tdb_from_bt_status(rc);

    /* Access mpool through B-Tree internal structure */
    BTREE* t = (BTREE*)kv->bdb->internal;
    if (!t || !t->bt_mp)
        return TDB_ERR_INVALID_ARG;

    /* Run incremental GC on mpool */
    int gc_rc = mpool_gc_step(t->bt_mp, budget_bytes);
    if (gc_rc < 0)
        return TDB_ERR_IO;

    /* Track progress and persist periodically while GC is active. */
    kv->gc_cursor = (uint32_t)t->bt_mp->gc_next_pgno;
    kv->free_head = (uint32_t)t->bt_mp->free_head;
    kv->alloc_next = (uint32_t)t->bt_mp->npages;
    if (t->bt_mp->gc_in_progress || gc_rc > 0) {
        kv->gc_dirty = 1;
        kv->gc_persist_counter++;
        if (kv->gc_persist_counter >= TDB_GC_PERSIST_INTERVAL) {
            (void)tdb_kv_persist_gc_state(kv);
            kv->gc_persist_counter = 0;
        }
    } else if (kv->gc_dirty) {
        /* If GC just finished, persist once so reboot won't restart from scratch. */
        (void)tdb_kv_persist_gc_state(kv);
        kv->gc_persist_counter = 0;
    }
    return (gc_rc > 0) ? 1 : 0;
}

/* TSDB support removed (RRD supersedes it in tiny-clj). */
