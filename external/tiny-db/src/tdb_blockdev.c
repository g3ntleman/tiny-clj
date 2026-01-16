// tdb_blockdev.c - Block device abstraction implementation.
//
// Provides validation and wrapper functions for block device operations,
// enforcing alignment and bounds checking according to device geometry.

#include "tdb_blockdev.h"
#include "tdb_utils.h"

/**
 * Validate block device geometry and operation pointers.
 * Ensures all granularities are power-of-2 and required ops are present.
 */
tdb_status_t tdb_blockdev_validate(const tdb_blockdev_t* bdev) {
    if (!bdev)
        return TDB_ERR_INVALID_ARG;
    if (!bdev->ops.read || !bdev->ops.prog || !bdev->ops.erase) {
        return TDB_ERR_INVALID_ARG;
    }
    if (bdev->geom.total_size_bytes == 0)
        return TDB_ERR_INVALID_ARG;

    /* All granularities must be power-of-2 for efficient alignment checks */
    if (!tdb_is_pow2(bdev->geom.read_granularity) || !tdb_is_pow2(bdev->geom.prog_granularity) ||
        !tdb_is_pow2(bdev->geom.erase_granularity)) {
        return TDB_ERR_INVALID_ARG;
    }
    return TDB_OK;
}

/**
 * Check if operation is within device bounds.
 */
static tdb_status_t tdb_bounds_check(const tdb_blockdev_t* bdev, uint32_t addr, size_t len) {
    if (!bdev)
        return TDB_ERR_INVALID_ARG;
    if ((uint64_t)addr + (uint64_t)len > (uint64_t)bdev->geom.total_size_bytes) {
        return TDB_ERR_IO;
    }
    return TDB_OK;
}

/**
 * Read from block device with bounds and alignment checking.
 */
tdb_status_t tdb_blockdev_read(const tdb_blockdev_t* bdev, uint32_t addr, void* out, size_t len) {
    if (!bdev || !out)
        return TDB_ERR_INVALID_ARG;

    tdb_status_t st = tdb_bounds_check(bdev, addr, len);
    if (st != TDB_OK)
        return st;

    if (!tdb_is_aligned(addr, bdev->geom.read_granularity) ||
        !tdb_is_len_aligned(len, bdev->geom.read_granularity)) {
        return TDB_ERR_INVALID_ARG;
    }

    return bdev->ops.read(bdev->ctx, addr, out, len);
}

/**
 * Program (write) to block device with bounds and alignment checking.
 */
tdb_status_t tdb_blockdev_prog(const tdb_blockdev_t* bdev, uint32_t addr, const void* data,
                             size_t len) {
    if (!bdev || (!data && len != 0))
        return TDB_ERR_INVALID_ARG;

    tdb_status_t st = tdb_bounds_check(bdev, addr, len);
    if (st != TDB_OK)
        return st;

    if (!tdb_is_aligned(addr, bdev->geom.prog_granularity) ||
        !tdb_is_len_aligned(len, bdev->geom.prog_granularity)) {
        return TDB_ERR_INVALID_ARG;
    }

    return bdev->ops.prog(bdev->ctx, addr, data, len);
}

/**
 * Erase block device region with bounds and alignment checking.
 */
tdb_status_t tdb_blockdev_erase(const tdb_blockdev_t* bdev, uint32_t addr, size_t len) {
    if (!bdev)
        return TDB_ERR_INVALID_ARG;

    tdb_status_t st = tdb_bounds_check(bdev, addr, len);
    if (st != TDB_OK)
        return st;

    if (!tdb_is_aligned(addr, bdev->geom.erase_granularity) ||
        !tdb_is_len_aligned(len, bdev->geom.erase_granularity)) {
        return TDB_ERR_INVALID_ARG;
    }

    return bdev->ops.erase(bdev->ctx, addr, len);
}
