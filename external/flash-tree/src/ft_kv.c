/*
 * ft_kv.c - B-Tree KV-DB with Copy-on-Write Log Storage
 *
 * Uses BSD B-Tree for sorted key storage with our custom mpool
 * that writes pages append-only to flash (no Read-Modify-Write).
 *
 * Architecture:
 *   ft_kv.c → BSD B-Tree (bt_*.c) → mpool.c → ft_blockdev → Flash
 *                                      ↑
 *                           Copy-on-Write, append-only
 *
 * Key Features:
 * - Flash-friendly: no Read-Modify-Write operations
 * - Copy-on-Write: old versions remain valid during updates
 * - Cursor API: efficient prefix iteration
 * - Incremental GC: reclaim space from old page versions
 */

#include "flash_tree.h"
#include "ft_blockdev.h"
#include "ft_kv_bind.h"
#include "ft_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __DBINTERFACE_PRIVATE
#include "ft_bsd_db.h"
#include "ft_bsd_btree.h"
#include "ft_bsd_mpool.h"
#include "ft_kv_internal.h"
#include "ft_page_policy.h"

/* ============== KV Handle ============== */

struct ft_kv_cursor {
    ft_kv_t* kv;
    int started; /* Has iteration begun? */
    int exhausted;
    int has_current;

    /* Prefix filter */
    uint8_t* prefix;
    size_t prefix_len;

    /* Current key/value from B-Tree */
    DBT cur_key;
    DBT cur_val;
};

/* ============== Status Conversion ============== */

static ft_status_t ft_from_bt_status(int rc) {
    if (rc == RET_SUCCESS)
        return FT_OK;
    if (rc == RET_SPECIAL)
        return FT_ERR_NOT_FOUND;
    if (rc == RET_ERROR) {
        if (errno == EINVAL || errno == E2BIG)
            return FT_ERR_INVALID_ARG;
        if (errno == EFTYPE)
            return FT_ERR_CORRUPT;
        return FT_ERR_IO;
    }
    return FT_ERR_IO;
}

/* ============== Public API ============== */

MPOOL* ft_kv_get_mpool(ft_kv_t* kv) {
    if (!kv || !kv->bdb)
        return NULL;
    BTREE* t = (BTREE*)kv->bdb->internal;
    if (!t)
        return NULL;
    return t->bt_mp;
}

ft_status_t ft_kv_open(ft_kv_t** out_kv, ft_blockdev_t* bdev, const ft_kv_cfg_t* cfg) {
    if (!out_kv || !bdev)
        return FT_ERR_INVALID_ARG;

    ft_status_t st = ft_blockdev_validate(bdev);
    if (st != FT_OK)
        return st;

    uint32_t start_page = FT_KV_ROOT_PAGE;
    if (cfg)
        start_page = cfg->start_page;
    uint32_t eg = bdev->geom.erase_granularity ? bdev->geom.erase_granularity : 1u;
    uint32_t base_offset = start_page * eg;
    if (eg != 0 && start_page != 0 && base_offset / eg != start_page)
        return FT_ERR_INVALID_ARG;
    if (base_offset >= bdev->geom.total_size_bytes)
        return FT_ERR_INVALID_ARG;

    ft_kv_t* kv = (ft_kv_t*)calloc(1, sizeof(ft_kv_t));
    if (!kv)
        return FT_ERR_NO_MEMORY;
    kv->bdev = bdev;

    /* Bind blockdev for btree/mpool open only (single-threaded). */
    ft_kv_bind(bdev, base_offset);

    /* Open B-Tree with our log-based mpool */
    BTREEINFO bi;
    memset(&bi, 0, sizeof(bi));
    ft_page_policy_t pol = {0};
    ft_status_t pst = ft_page_policy_compute_variant_b(&bdev->geom, sizeof(ft_page_hdr_t), &pol);
    if (pst != FT_OK) {
        free(kv);
        return pst;
    }
    bi.psize = pol.page_size;
    bi.cachesize = 2u * pol.page_size; /* Still minimal; mpool has its own cache. */

    kv->bdb = __bt_open("ft_kv", O_RDWR | O_CREAT, 0600, &bi, 0);
    ft_kv_unbind();

    if (!kv->bdb) {
        ft_kv_unbind();
        free(kv);
        return FT_ERR_IO;
    }

    *out_kv = kv;
    return FT_OK;
}

void ft_kv_close(ft_kv_t* kv) {
    if (!kv)
        return;
    if (kv->bdb)
        (void)kv->bdb->close(kv->bdb);
    free(kv->get_buf);
    free(kv);
}

ft_status_t ft_kv_put(ft_kv_t* kv, const void* key, size_t key_len, const void* val,
                      size_t val_len) {
    if (!kv || (!key && key_len != 0) || (!val && val_len != 0)) {
        return FT_ERR_INVALID_ARG;
    }

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {.data = (void*)val, .size = val_len};
    int rc = kv->bdb->put(kv->bdb, &k, &d, 0);
    return ft_from_bt_status(rc);
}

ft_status_t ft_kv_get(ft_kv_t* kv, const void* key, size_t key_len, ft_blob_t* out) {
    if (!kv || (!key && key_len != 0) || !out)
        return FT_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return ft_from_bt_status(rc);

    /* Copy to scratch buffer (B-Tree's buffer is volatile) */
    if (d.size > kv->get_cap) {
        size_t new_cap = (kv->get_cap == 0) ? 64 : kv->get_cap;
        while (new_cap < d.size)
            new_cap *= 2;
        uint8_t* p = (uint8_t*)realloc(kv->get_buf, new_cap);
        if (!p)
            return FT_ERR_NO_MEMORY;
        kv->get_buf = p;
        kv->get_cap = new_cap;
    }
    if (d.size)
        memcpy(kv->get_buf, d.data, d.size);
    out->data = kv->get_buf;
    out->len = d.size;
    return FT_OK;
}

ft_status_t ft_kv_get_len(ft_kv_t* kv, const void* key, size_t key_len, size_t* out_len) {
    if (out_len)
        *out_len = 0;
    if (!kv || (!key && key_len != 0) || !out_len)
        return FT_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return ft_from_bt_status(rc);
    *out_len = d.size;
    return FT_OK;
}

ft_status_t ft_kv_get_into(ft_kv_t* kv, const void* key, size_t key_len, void* out, size_t out_len,
                           size_t* saved_len_out) {
    if (saved_len_out)
        *saved_len_out = 0;
    if (!kv || (!key && key_len != 0))
        return FT_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = kv->bdb->get(kv->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS)
        return ft_from_bt_status(rc);

    if (saved_len_out)
        *saved_len_out = d.size;
    if (out && out_len > 0) {
        size_t to_copy = (d.size < out_len) ? d.size : out_len;
        memcpy(out, d.data, to_copy);
    }
    return FT_OK;
}

ft_status_t ft_kv_del(ft_kv_t* kv, const void* key, size_t key_len) {
    if (!kv || (!key && key_len != 0))
        return FT_ERR_INVALID_ARG;

    DBT k = {.data = (void*)key, .size = key_len};
    int rc = kv->bdb->del(kv->bdb, &k, 0);
    return ft_from_bt_status(rc);
}

ft_status_t ft_kv_max_val_len(const ft_kv_t* kv, size_t key_len, size_t* out_max_val_len) {
    if (out_max_val_len)
        *out_max_val_len = 0;
    if (!kv || !kv->bdb || !out_max_val_len)
        return FT_ERR_INVALID_ARG;

    BTREE* t = (BTREE*)kv->bdb->internal;
    if (!t)
        return FT_ERR_INVALID_ARG;

    const size_t cutoff = (size_t)t->bt_ovflsize;
    if (key_len >= cutoff) {
        *out_max_val_len = 0;
        return FT_OK;
    }

    *out_max_val_len = cutoff - key_len;
    return FT_OK;
}

/* ============== Cursor API ============== */

/**
 * Open a cursor for iterating over keys with a given prefix.
 */
ft_status_t ft_kv_cursor_open_prefix(ft_kv_t* kv, const void* prefix, size_t prefix_len,
                                     ft_kv_cursor_t** out_cur) {
    if (!kv || (!prefix && prefix_len != 0) || !out_cur)
        return FT_ERR_INVALID_ARG;

    ft_kv_cursor_t* cur = (ft_kv_cursor_t*)calloc(1, sizeof(*cur));
    if (!cur)
        return FT_ERR_NO_MEMORY;

    cur->kv = kv;
    cur->started = 0;
    cur->exhausted = 0;
    cur->has_current = 0;

    if (prefix_len) {
        cur->prefix = (uint8_t*)malloc(prefix_len);
        if (!cur->prefix) {
            free(cur);
            return FT_ERR_NO_MEMORY;
        }
        memcpy(cur->prefix, prefix, prefix_len);
        cur->prefix_len = prefix_len;
    }

    *out_cur = cur;
    return FT_OK;
}

ft_status_t ft_kv_cursor_next(ft_kv_cursor_t* cur, int* out_has_item) {
    if (!cur || !out_has_item)
        return FT_ERR_INVALID_ARG;
    *out_has_item = 0;
    cur->has_current = 0;

    if (cur->exhausted)
        return FT_OK;

    int rc;
    if (!cur->started) {
        /* First call: position at prefix or first key */
        if (cur->prefix_len) {
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
        return FT_OK;
    }
    if (rc != RET_SUCCESS) {
        cur->exhausted = 1;
        return ft_from_bt_status(rc);
    }

    /* Check prefix filter */
    if (cur->prefix_len) {
        if (!ft_has_prefix(cur->cur_key.data, cur->cur_key.size, cur->prefix, cur->prefix_len)) {
            cur->exhausted = 1;
            return FT_OK;
        }
    }

    cur->has_current = 1;
    *out_has_item = 1;
    return FT_OK;
}

ft_status_t ft_kv_cursor_key(const ft_kv_cursor_t* cur, ft_blob_t* out_key) {
    if (!cur || !out_key)
        return FT_ERR_INVALID_ARG;
    if (!cur->has_current)
        return FT_ERR_NOT_FOUND;
    out_key->data = cur->cur_key.data;
    out_key->len = cur->cur_key.size;
    return FT_OK;
}

ft_status_t ft_kv_cursor_val(const ft_kv_cursor_t* cur, ft_blob_t* out_val) {
    if (!cur || !out_val)
        return FT_ERR_INVALID_ARG;
    if (!cur->has_current)
        return FT_ERR_NOT_FOUND;
    out_val->data = cur->cur_val.data;
    out_val->len = cur->cur_val.size;
    return FT_OK;
}

void ft_kv_cursor_close(ft_kv_cursor_t* cur) {
    if (!cur)
        return;
    free(cur->prefix);
    free(cur);
}

ft_status_t ft_kv_iter_prefix(ft_kv_t* kv, const void* prefix, size_t prefix_len, ft_key_cb cb,
                              void* arg) {
    if (!kv || (!prefix && prefix_len != 0) || !cb)
        return FT_ERR_INVALID_ARG;

    ft_kv_cursor_t* cur = NULL;
    ft_status_t st = ft_kv_cursor_open_prefix(kv, prefix, prefix_len, &cur);
    if (st != FT_OK)
        return st;

    int has = 0;
    ft_blob_t k, v;
    while ((st = ft_kv_cursor_next(cur, &has)) == FT_OK && has) {
        st = ft_kv_cursor_key(cur, &k);
        if (st != FT_OK)
            break;
        st = ft_kv_cursor_val(cur, &v);
        if (st != FT_OK)
            break;
        st = cb(k.data, k.len, v.data, v.len, arg);
        if (st != FT_OK)
            break;
    }

    ft_kv_cursor_close(cur);
    return st;
}

ft_status_t ft_kv_gc_step(ft_kv_t* kv, size_t budget_bytes) {
    int rc = ft_kv_gc_step_more(kv, budget_bytes);
    if (rc < 0)
        return (ft_status_t)rc;
    return FT_OK;
}

int ft_kv_gc_step_more(ft_kv_t* kv, size_t budget_bytes) {
    if (!kv || !kv->bdb)
        return FT_ERR_INVALID_ARG;

    /* Sync B-Tree to flush all dirty pages to log */
    int rc = kv->bdb->sync(kv->bdb, 0);
    if (rc != RET_SUCCESS)
        return (int)ft_from_bt_status(rc);

    /* Access mpool through B-Tree internal structure */
    BTREE* t = (BTREE*)kv->bdb->internal;
    if (!t || !t->bt_mp)
        return FT_ERR_INVALID_ARG;

    /* Run incremental GC on mpool */
    int gc_rc = mpool_gc_step(t->bt_mp, budget_bytes);
    if (gc_rc < 0)
        return FT_ERR_IO;
    return (gc_rc > 0) ? 1 : 0;
}

/* (TSDB functions are implemented in ft_tsdb.c) */
