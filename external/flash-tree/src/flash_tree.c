/*
 * flash_tree.c - B-Tree KV-DB with Copy-on-Write Log Storage
 *
 * Uses BSD B-Tree for sorted key storage with our custom mpool
 * that writes pages append-only to flash (no Read-Modify-Write).
 *
 * Architecture:
 *   flash_tree.c → BSD B-Tree (bt_*.c) → mpool.c → ft_blockdev → Flash
 *                                         ↑
 *                              Copy-on-Write, append-only
 *
 * Key Features:
 * - Flash-friendly: no Read-Modify-Write operations
 * - Copy-on-Write: old versions remain valid during updates
 * - Cursor API: efficient prefix iteration
 * - Incremental GC: reclaim space from old page versions
 */

#include "flash_tree.h"
#include "ft_blockdev.h"
#include "ft_bsd_blockfile.h"
#include "ft_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#define __DBINTERFACE_PRIVATE
#include "db.h"
#include "btree.h"
#include "mpool.h"

/* ============== Database Handle ============== */

struct ft_db {
    ft_blockdev_t* bdev;
    DB* bdb;                  /* BSD B-Tree handle */
    
    /* Scratch buffer for ft_get */
    uint8_t* get_buf;
    size_t get_cap;
};

struct ft_cursor {
    ft_db_t* db;
    int started;              /* Has iteration begun? */
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
    if (rc == RET_SUCCESS) return FT_OK;
    if (rc == RET_SPECIAL) return FT_ERR_NOT_FOUND;
    if (rc == RET_ERROR) {
        if (errno == EINVAL) return FT_ERR_INVALID_ARG;
        return FT_ERR_IO;
    }
    return FT_ERR_IO;
}

/* ============== Public API ============== */

ft_status_t ft_db_init(ft_db_t** out_db, ft_blockdev_t* bdev, const ft_cfg_t* cfg) {
    (void)cfg;
    if (!out_db || !bdev) return FT_ERR_INVALID_ARG;
    
    ft_status_t st = ft_blockdev_validate(bdev);
    if (st != FT_OK) return st;
    
    ft_db_t* db = (ft_db_t*)calloc(1, sizeof(ft_db_t));
    if (!db) return FT_ERR_NO_MEMORY;
    db->bdev = bdev;
    
    /* Bind blockdev for mpool (stays bound for lifetime of db) */
    ft_bsd_blockfile_bind(bdev);
    
    /* Open B-Tree with our log-based mpool */
    BTREEINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.psize = 512;       /* Small pages for embedded */
    bi.cachesize = 1024;  /* Minimal cache (mpool handles this) */
    
    db->bdb = __bt_open("ft_kv", O_RDWR | O_CREAT, 0600, &bi, 0);
    /* Note: Don't unbind here - mpool needs bdev for all operations */
    
    if (!db->bdb) {
        ft_bsd_blockfile_unbind();
        free(db);
        return FT_ERR_IO;
    }
    
    *out_db = db;
    return FT_OK;
}

void ft_db_deinit(ft_db_t* db) {
    if (!db) return;
    if (db->bdb) (void)db->bdb->close(db->bdb);
    ft_bsd_blockfile_unbind();
    free(db->get_buf);
    free(db);
}

ft_status_t ft_put(ft_db_t* db, const void* key, size_t key_len,
                   const void* val, size_t val_len) {
    if (!db || (!key && key_len != 0) || (!val && val_len != 0)) {
        return FT_ERR_INVALID_ARG;
    }
    
    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {.data = (void*)val, .size = val_len};
    int rc = db->bdb->put(db->bdb, &k, &d, 0);
    return ft_from_bt_status(rc);
}

ft_status_t ft_get(ft_db_t* db, const void* key, size_t key_len, ft_blob_t* out) {
    if (!db || (!key && key_len != 0) || !out) return FT_ERR_INVALID_ARG;
    
    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = db->bdb->get(db->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS) return ft_from_bt_status(rc);
    
    /* Copy to scratch buffer (B-Tree's buffer is volatile) */
    if (d.size > db->get_cap) {
        size_t new_cap = (db->get_cap == 0) ? 64 : db->get_cap;
        while (new_cap < d.size) new_cap *= 2;
        uint8_t* p = (uint8_t*)realloc(db->get_buf, new_cap);
        if (!p) return FT_ERR_NO_MEMORY;
        db->get_buf = p;
        db->get_cap = new_cap;
    }
    if (d.size) memcpy(db->get_buf, d.data, d.size);
    out->data = db->get_buf;
    out->len = d.size;
    return FT_OK;
}

ft_status_t ft_get_len(ft_db_t* db, const void* key, size_t key_len, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!db || (!key && key_len != 0) || !out_len) return FT_ERR_INVALID_ARG;
    
    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = db->bdb->get(db->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS) return ft_from_bt_status(rc);
    *out_len = d.size;
    return FT_OK;
}

ft_status_t ft_get_into(ft_db_t* db, const void* key, size_t key_len,
                         void* out, size_t out_len, size_t* saved_len_out) {
    if (saved_len_out) *saved_len_out = 0;
    if (!db || (!key && key_len != 0)) return FT_ERR_INVALID_ARG;
    
    DBT k = {.data = (void*)key, .size = key_len};
    DBT d = {0};
    int rc = db->bdb->get(db->bdb, &k, &d, 0);
    if (rc != RET_SUCCESS) return ft_from_bt_status(rc);
    
    if (saved_len_out) *saved_len_out = d.size;
    if (out && out_len > 0) {
        size_t to_copy = (d.size < out_len) ? d.size : out_len;
        memcpy(out, d.data, to_copy);
    }
    return FT_OK;
}

ft_status_t ft_del(ft_db_t* db, const void* key, size_t key_len) {
    if (!db || (!key && key_len != 0)) return FT_ERR_INVALID_ARG;
    
    DBT k = {.data = (void*)key, .size = key_len};
    int rc = db->bdb->del(db->bdb, &k, 0);
    return ft_from_bt_status(rc);
}

/* ============== Cursor API ============== */

/**
 * Open a cursor for iterating over keys with a given prefix.
 */
ft_status_t ft_cursor_open_prefix(ft_db_t* db, const void* prefix, size_t prefix_len,
                                   ft_cursor_t** out_cur) {
    if (!db || (!prefix && prefix_len != 0) || !out_cur) return FT_ERR_INVALID_ARG;
    
    ft_cursor_t* cur = (ft_cursor_t*)calloc(1, sizeof(*cur));
    if (!cur) return FT_ERR_NO_MEMORY;
    
    cur->db = db;
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

ft_status_t ft_cursor_next(ft_cursor_t* cur, int* out_has_item) {
    if (!cur || !out_has_item) return FT_ERR_INVALID_ARG;
    *out_has_item = 0;
    cur->has_current = 0;
    
    if (cur->exhausted) return FT_OK;
    
    int rc;
    if (!cur->started) {
        /* First call: position at prefix or first key */
        if (cur->prefix_len) {
            cur->cur_key.data = cur->prefix;
            cur->cur_key.size = cur->prefix_len;
            rc = cur->db->bdb->seq(cur->db->bdb, &cur->cur_key, &cur->cur_val, R_CURSOR);
        } else {
            rc = cur->db->bdb->seq(cur->db->bdb, &cur->cur_key, &cur->cur_val, R_FIRST);
        }
        cur->started = 1;
    } else {
        /* Subsequent calls: advance to next */
        rc = cur->db->bdb->seq(cur->db->bdb, &cur->cur_key, &cur->cur_val, R_NEXT);
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
        if (!ft_has_prefix(cur->cur_key.data, cur->cur_key.size,
                          cur->prefix, cur->prefix_len)) {
            cur->exhausted = 1;
            return FT_OK;
        }
    }
    
    cur->has_current = 1;
    *out_has_item = 1;
    return FT_OK;
}

ft_status_t ft_cursor_key(const ft_cursor_t* cur, ft_blob_t* out_key) {
    if (!cur || !out_key) return FT_ERR_INVALID_ARG;
    if (!cur->has_current) return FT_ERR_NOT_FOUND;
    out_key->data = cur->cur_key.data;
    out_key->len = cur->cur_key.size;
    return FT_OK;
}

ft_status_t ft_cursor_val(const ft_cursor_t* cur, ft_blob_t* out_val) {
    if (!cur || !out_val) return FT_ERR_INVALID_ARG;
    if (!cur->has_current) return FT_ERR_NOT_FOUND;
    out_val->data = cur->cur_val.data;
    out_val->len = cur->cur_val.size;
    return FT_OK;
}

void ft_cursor_close(ft_cursor_t* cur) {
    if (!cur) return;
    free(cur->prefix);
    free(cur);
}

ft_status_t ft_iter_prefix(ft_db_t* db, const void* prefix, size_t prefix_len,
                            ft_key_cb cb, void* arg) {
    if (!db || (!prefix && prefix_len != 0) || !cb) return FT_ERR_INVALID_ARG;
    
    ft_cursor_t* cur = NULL;
    ft_status_t st = ft_cursor_open_prefix(db, prefix, prefix_len, &cur);
    if (st != FT_OK) return st;
    
    int has = 0;
    ft_blob_t k, v;
    while ((st = ft_cursor_next(cur, &has)) == FT_OK && has) {
        st = ft_cursor_key(cur, &k);
        if (st != FT_OK) break;
        st = ft_cursor_val(cur, &v);
        if (st != FT_OK) break;
        st = cb(k.data, k.len, v.data, v.len, arg);
        if (st != FT_OK) break;
    }
    
    ft_cursor_close(cur);
    return st;
}

ft_status_t ft_gc_step(ft_db_t* db, size_t budget_bytes) {
    if (!db || !db->bdb) return FT_ERR_INVALID_ARG;

    /* Sync B-Tree to flush all dirty pages to log */
    int rc = db->bdb->sync(db->bdb, 0);
    if (rc != RET_SUCCESS) return ft_from_bt_status(rc);
    
    /* Access mpool through B-Tree internal structure */
    BTREE* t = (BTREE*)db->bdb->internal;
    if (!t || !t->bt_mp) return FT_ERR_INVALID_ARG;
    
    /* Run incremental GC on mpool */
    int gc_rc = mpool_gc_step(t->bt_mp, budget_bytes);
    if (gc_rc < 0) return FT_ERR_IO;
    
    return FT_OK;
}

/* (TSDB functions are implemented in ft_tsdb_flashdb.c) */
