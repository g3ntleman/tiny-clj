// ft_blockdev.c - Block device abstraction implementation.
//
// Provides validation and wrapper functions for block device operations,
// enforcing alignment and bounds checking according to device geometry.

#include "ft_blockdev.h"
#include "ft_utils.h"

/**
 * Validate block device geometry and operation pointers.
 * Ensures all granularities are power-of-2 and required ops are present.
 */
ft_status_t ft_blockdev_validate(const ft_blockdev_t* bdev) {
    if (!bdev)
        return FT_ERR_INVALID_ARG;
    if (!bdev->ops.read || !bdev->ops.prog || !bdev->ops.erase) {
        return FT_ERR_INVALID_ARG;
    }
    if (bdev->geom.total_size_bytes == 0)
        return FT_ERR_INVALID_ARG;

    /* All granularities must be power-of-2 for efficient alignment checks */
    if (!ft_is_pow2(bdev->geom.read_granularity) || !ft_is_pow2(bdev->geom.prog_granularity) ||
        !ft_is_pow2(bdev->geom.erase_granularity)) {
        return FT_ERR_INVALID_ARG;
    }
    return FT_OK;
}

/**
 * Check if operation is within device bounds.
 */
static ft_status_t ft_bounds_check(const ft_blockdev_t* bdev, uint32_t addr, size_t len) {
    if (!bdev)
        return FT_ERR_INVALID_ARG;
    if ((uint64_t)addr + (uint64_t)len > (uint64_t)bdev->geom.total_size_bytes) {
        return FT_ERR_IO;
    }
    return FT_OK;
}

/**
 * Read from block device with bounds and alignment checking.
 */
ft_status_t ft_blockdev_read(const ft_blockdev_t* bdev, uint32_t addr, void* out, size_t len) {
    if (!bdev || !out)
        return FT_ERR_INVALID_ARG;

    ft_status_t st = ft_bounds_check(bdev, addr, len);
    if (st != FT_OK)
        return st;

    if (!ft_is_aligned(addr, bdev->geom.read_granularity) ||
        !ft_is_len_aligned(len, bdev->geom.read_granularity)) {
        return FT_ERR_INVALID_ARG;
    }

    return bdev->ops.read(bdev->ctx, addr, out, len);
}

/**
 * Program (write) to block device with bounds and alignment checking.
 */
ft_status_t ft_blockdev_prog(const ft_blockdev_t* bdev, uint32_t addr, const void* data,
                             size_t len) {
    if (!bdev || (!data && len != 0))
        return FT_ERR_INVALID_ARG;

    ft_status_t st = ft_bounds_check(bdev, addr, len);
    if (st != FT_OK)
        return st;

    if (!ft_is_aligned(addr, bdev->geom.prog_granularity) ||
        !ft_is_len_aligned(len, bdev->geom.prog_granularity)) {
        return FT_ERR_INVALID_ARG;
    }

    return bdev->ops.prog(bdev->ctx, addr, data, len);
}

/**
 * Erase block device region with bounds and alignment checking.
 */
ft_status_t ft_blockdev_erase(const ft_blockdev_t* bdev, uint32_t addr, size_t len) {
    if (!bdev)
        return FT_ERR_INVALID_ARG;

    ft_status_t st = ft_bounds_check(bdev, addr, len);
    if (st != FT_OK)
        return st;

    if (!ft_is_aligned(addr, bdev->geom.erase_granularity) ||
        !ft_is_len_aligned(len, bdev->geom.erase_granularity)) {
        return FT_ERR_INVALID_ARG;
    }

    return bdev->ops.erase(bdev->ctx, addr, len);
}
