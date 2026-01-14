// ft_tsdb.c - FlashDB TSDB-backed implementation for flash-tree.
//
// This adapts FlashDB's TSDB module to the flash-tree public API. The core TSDB
// logic stays in upstream FlashDB sources; we provide glue + error mapping.
//
// All new comments in English (workspace rule).

#include "flash_tree.h"
#include "ft_blockdev.h"

#include "flash-tree.h"

#include <stdlib.h>
#include <string.h>

struct ft_tsdb {
    struct fdb_tsdb inner;
    ft_blockdev_t* bdev;
    size_t max_len;
};

static fdb_time_t tsdb_get_time_stub(void) {
    static fdb_time_t t = 0;
    return ++t;
}

static ft_status_t ft_from_fdb_err(fdb_err_t e) {
    switch (e) {
    case FDB_NO_ERR:
        return FT_OK;
    case FDB_READ_ERR:
    case FDB_WRITE_ERR:
    case FDB_ERASE_ERR:
        return FT_ERR_IO;
    case FDB_SAVED_FULL:
        return FT_ERR_UNSUPPORTED;
    case FDB_INIT_FAILED:
    default:
        return FT_ERR_CORRUPT;
    }
}

static ft_tsl_status_t ft_from_fdb_tsl_status(fdb_tsl_status_t s) {
    // flash_tree API is minimal: treat any non-WRITE as dropped.
    return (s == FDB_TSL_WRITE) ? FT_TSL_STATUS_OK : FT_TSL_STATUS_DROPPED;
}

ft_status_t ft_tsdb_init(ft_tsdb_t** out_tsdb, ft_blockdev_t* bdev, const ft_tsdb_cfg_t* cfg) {
    if (!out_tsdb || !bdev)
        return FT_ERR_INVALID_ARG;
    *out_tsdb = NULL;

    // Default max_len (must be < sector size). Allow override via cfg->reserved.
    size_t max_len = 256;
    if (cfg && cfg->reserved)
        max_len = (size_t)cfg->reserved;
    if (max_len == 0)
        return FT_ERR_INVALID_ARG;

    ft_tsdb_t* tsdb = (ft_tsdb_t*)calloc(1, sizeof(ft_tsdb_t));
    if (!tsdb)
        return FT_ERR_NO_MEMORY;
    tsdb->bdev = bdev;
    tsdb->max_len = max_len;

    // Initialize FlashDB TSDB. "name/path" are only for logging in our port.
    fdb_err_t r =
        fdb_tsdb_init(&tsdb->inner, "flash-tree", "ft", tsdb_get_time_stub, max_len, bdev);
    ft_status_t st = ft_from_fdb_err(r);
    if (st != FT_OK) {
        free(tsdb);
        return st;
    }

    *out_tsdb = tsdb;
    return FT_OK;
}

void ft_tsdb_deinit(ft_tsdb_t* tsdb) {
    if (!tsdb)
        return;
    (void)fdb_tsdb_deinit(&tsdb->inner);
    free(tsdb);
}

ft_status_t ft_tsl_append(ft_tsdb_t* tsdb, const void* data, size_t len, ft_time_t t) {
    if (!tsdb || (!data && len != 0))
        return FT_ERR_INVALID_ARG;
    if (len > tsdb->max_len)
        return FT_ERR_INVALID_ARG;
    struct fdb_blob blob;
    memset(&blob, 0, sizeof(blob));
    fdb_blob_make(&blob, data, len);
    fdb_err_t r = fdb_tsl_append_with_ts(&tsdb->inner, &blob, (fdb_time_t)t);
    return ft_from_fdb_err(r);
}

typedef struct ft_fdb_iter_ctx {
    ft_tsdb_t* tsdb;
    ft_tsl_cb cb;
    void* arg;
    uint8_t* buf;
    size_t cap;
    ft_status_t st;
} ft_fdb_iter_ctx_t;

static bool ft_fdb_iter_cb(fdb_tsl_t tsl, void* arg) {
    ft_fdb_iter_ctx_t* c = (ft_fdb_iter_ctx_t*)arg;
    if (!c || !c->tsdb || !c->cb)
        return true; /* stop */
    if (!tsl) {
        c->st = FT_ERR_CORRUPT;
        return true;
    }

    struct fdb_blob blob;
    memset(&blob, 0, sizeof(blob));
    fdb_blob_make(&blob, c->buf, c->cap);
    fdb_tsl_to_blob(tsl, &blob);

    size_t n = fdb_blob_read((fdb_db_t)&c->tsdb->inner, &blob);
    if (n != blob.saved.len) {
        c->st = FT_ERR_IO;
        return true;
    }

    ft_tsl_status_t st = ft_from_fdb_tsl_status(tsl->status);
    ft_status_t r = c->cb((ft_time_t)tsl->time, c->buf, n, st, c->arg);
    if (r != FT_OK) {
        c->st = r;
        return true;
    }
    return false; /* continue */
}

ft_status_t ft_tsl_iter_by_time(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_cb cb,
                                void* arg) {
    if (!tsdb || !cb)
        return FT_ERR_INVALID_ARG;

    uint8_t* buf = NULL;
    if (tsdb->max_len) {
        buf = (uint8_t*)malloc(tsdb->max_len);
        if (!buf)
            return FT_ERR_NO_MEMORY;
    }

    ft_fdb_iter_ctx_t ctx = {
        .tsdb = tsdb, .cb = cb, .arg = arg, .buf = buf, .cap = tsdb->max_len, .st = FT_OK};
    fdb_tsl_iter_by_time(&tsdb->inner, (fdb_time_t)from, (fdb_time_t)to, ft_fdb_iter_cb, &ctx);

    free(buf);
    return ctx.st;
}

ft_status_t ft_tsl_query_count(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to,
                               ft_tsl_status_t status, uint64_t* out_count) {
    if (!tsdb || !out_count)
        return FT_ERR_INVALID_ARG;
    fdb_tsl_status_t want = (status == FT_TSL_STATUS_OK) ? FDB_TSL_WRITE : FDB_TSL_DELETED;
    size_t c = fdb_tsl_query_count(&tsdb->inner, (fdb_time_t)from, (fdb_time_t)to, want);
    *out_count = (uint64_t)c;
    return FT_OK;
}

typedef struct ft_agg_ctx {
    ft_tsl_status_t want_status;
    uint64_t count;
    float sum;
    float minv;
    float maxv;
} ft_agg_ctx_t;

static ft_status_t ft_agg_f32_cb(ft_time_t t, const void* data, size_t len, ft_tsl_status_t st,
                                 void* arg) {
    (void)t;
    ft_agg_ctx_t* aa = (ft_agg_ctx_t*)arg;
    if (!aa)
        return FT_ERR_INVALID_ARG;
    if (st != aa->want_status)
        return FT_OK;
    if (len != sizeof(float))
        return FT_ERR_INVALID_ARG;
    float v = 0.0f;
    memcpy(&v, data, sizeof(v));
    if (aa->count == 0) {
        aa->minv = v;
        aa->maxv = v;
    } else {
        if (v < aa->minv)
            aa->minv = v;
        if (v > aa->maxv)
            aa->maxv = v;
    }
    aa->sum += v;
    aa->count++;
    return FT_OK;
}

ft_status_t ft_tsl_aggregate_f32(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to,
                                 ft_tsl_status_t status, ft_agg_f32_t* out) {
    if (!tsdb || !out)
        return FT_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    ft_agg_ctx_t a = {.want_status = status, .count = 0, .sum = 0.0f, .minv = 0.0f, .maxv = 0.0f};
    ft_status_t st = ft_tsl_iter_by_time(tsdb, from, to, ft_agg_f32_cb, &a);
    if (st != FT_OK)
        return st;

    out->count = a.count;
    out->sum = a.sum;
    out->min = (a.count ? a.minv : 0.0f);
    out->max = (a.count ? a.maxv : 0.0f);
    out->avg = (a.count ? (a.sum / (float)a.count) : 0.0f);
    return FT_OK;
}
