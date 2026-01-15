/*
 * FlashDB low-level port for Flash-Tree.
 *
 * We reuse FlashDB TSDB logic "as-is" and provide minimal glue so it can run
 * on an ft_blockdev_t (RAM backend in tests, real flash backends later).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ft_blockdev.h"

#include "flash-tree.h"
#include "ft_low_lvl.h"
#include "ft_port_ctx.h"

#include <string.h>

static const ft_fdb_port_ctx_t* db_ctx(fdb_db_t db) {
    return (const ft_fdb_port_ctx_t*)db->user_data;
}

static const ft_blockdev_t* db_bdev(fdb_db_t db) {
    const ft_fdb_port_ctx_t* c = db_ctx(db);
    return c ? c->bdev : NULL;
}

static uint32_t db_base(fdb_db_t db) {
    const ft_fdb_port_ctx_t* c = db_ctx(db);
    return c ? c->base_offset : 0u;
}

static uint32_t db_region_bytes(fdb_db_t db) {
    const ft_fdb_port_ctx_t* c = db_ctx(db);
    return c ? c->region_bytes : 0u;
}

static uint32_t align_down_u32(uint32_t x, uint32_t a) {
    if (a == 0)
        return 0;
    return x - (x % a);
}

static int is_pow2_u32(uint32_t x) {
    return x && ((x & (x - 1u)) == 0);
}

/* Minimal init used by fdb_tsdb_init. */
fdb_err_t _fdb_init_ex(fdb_db_t db, const char* name, const char* path, fdb_db_type type,
                       void* user_data) {
    (void)path;
    if (!db || !name || !user_data)
        return FDB_INIT_FAILED;
    if (db->init_ok)
        return FDB_NO_ERR;

    db->name = name;
    db->type = type;
    db->user_data = user_data;

    /* Force file_mode so FlashDB uses _fdb_file_* entrypoints. */
    db->file_mode = true;

    const ft_blockdev_t* bdev = db_bdev(db);
    if (!bdev)
        return FDB_INIT_FAILED;
    if (ft_blockdev_validate(bdev) != FT_OK)
        return FDB_INIT_FAILED;

    /* Sector size comes from erase granularity (must be power of two). */
    db->sec_size = bdev->geom.erase_granularity;
    if (!is_pow2_u32(db->sec_size))
        return FDB_INIT_FAILED;

    /* Max size is the largest erase-aligned region starting at base_offset. */
    const uint32_t base = db_base(db);
    if (base >= bdev->geom.total_size_bytes)
        return FDB_INIT_FAILED;
    uint32_t region = db_region_bytes(db);
    if (region == 0u) {
        region = bdev->geom.total_size_bytes - base;
    } else {
        /* region is relative to base_offset and must be within the blockdev. */
        if (region > (bdev->geom.total_size_bytes - base))
            return FDB_INIT_FAILED;
    }
    db->max_size = align_down_u32(region, db->sec_size);
    if (db->max_size == 0)
        return FDB_INIT_FAILED;
    if ((db->max_size / db->sec_size) < 2u)
        return FDB_INIT_FAILED;

    return FDB_NO_ERR;
}

void _fdb_init_finish(fdb_db_t db, fdb_err_t result) {
    if (!db)
        return;
    if (result == FDB_NO_ERR) {
        db->init_ok = true;
    } else {
        db->init_ok = false;
    }
}

void _fdb_deinit(fdb_db_t db) {
    if (!db)
        return;
    db->init_ok = false;
}

const char* _fdb_db_path(fdb_db_t db) {
    (void)db;
    return "ft_blockdev";
}

/* File-mode flash operations (backed by ft_blockdev). */
fdb_err_t _fdb_file_read(fdb_db_t db, uint32_t addr, void* buf, size_t size) {
    if (!db || (!buf && size != 0))
        return FDB_READ_ERR;
    if ((uint64_t)addr + (uint64_t)size > (uint64_t)db->max_size)
        return FDB_READ_ERR;
    const ft_blockdev_t* bdev = db_bdev(db);
    if (!bdev)
        return FDB_READ_ERR;
    const uint32_t base = db_base(db);
    if ((uint64_t)base + (uint64_t)addr + (uint64_t)size > (uint64_t)bdev->geom.total_size_bytes)
        return FDB_READ_ERR;
    ft_status_t st = ft_blockdev_read(bdev, base + addr, buf, size);
    return (st == FT_OK) ? FDB_NO_ERR : FDB_READ_ERR;
}

fdb_err_t _fdb_file_write(fdb_db_t db, uint32_t addr, const void* buf, size_t size, bool sync) {
    (void)sync;
    if (!db || (!buf && size != 0))
        return FDB_WRITE_ERR;
    if ((uint64_t)addr + (uint64_t)size > (uint64_t)db->max_size)
        return FDB_WRITE_ERR;
    const ft_blockdev_t* bdev = db_bdev(db);
    if (!bdev)
        return FDB_WRITE_ERR;
    const uint32_t base = db_base(db);
    if ((uint64_t)base + (uint64_t)addr + (uint64_t)size > (uint64_t)bdev->geom.total_size_bytes)
        return FDB_WRITE_ERR;
    ft_status_t st = ft_blockdev_prog(bdev, base + addr, buf, size);
    return (st == FT_OK) ? FDB_NO_ERR : FDB_WRITE_ERR;
}

fdb_err_t _fdb_file_erase(fdb_db_t db, uint32_t addr, size_t size) {
    if (!db)
        return FDB_ERASE_ERR;
    if ((uint64_t)addr + (uint64_t)size > (uint64_t)db->max_size)
        return FDB_ERASE_ERR;
    const ft_blockdev_t* bdev = db_bdev(db);
    if (!bdev)
        return FDB_ERASE_ERR;
    const uint32_t base = db_base(db);
    if ((uint64_t)base + (uint64_t)addr + (uint64_t)size > (uint64_t)bdev->geom.total_size_bytes)
        return FDB_ERASE_ERR;
    ft_status_t st = ft_blockdev_erase(bdev, base + addr, size);
    return (st == FT_OK) ? FDB_NO_ERR : FDB_ERASE_ERR;
}
