// ft_blockdev.c - block device helpers.

#include "ft_blockdev.h"

static int is_pow2(uint32_t x) {
    return x && ((x & (x - 1u)) == 0);
}

ft_status_t ft_blockdev_validate(const ft_blockdev_t* bdev) {
    if (!bdev) return FT_ERR_INVALID_ARG;
    if (!bdev->ops.read || !bdev->ops.prog || !bdev->ops.erase) return FT_ERR_INVALID_ARG;
    if (bdev->geom.total_size_bytes == 0) return FT_ERR_INVALID_ARG;
    if (!is_pow2(bdev->geom.read_granularity) ||
        !is_pow2(bdev->geom.prog_granularity) ||
        !is_pow2(bdev->geom.erase_granularity)) {
        return FT_ERR_INVALID_ARG;
    }
    return FT_OK;
}

static int is_aligned(uint32_t addr, uint32_t gran) {
    return (gran == 0) ? 0 : ((addr % gran) == 0);
}

static int len_aligned(size_t len, uint32_t gran) {
    return (gran == 0) ? 0 : ((len % (size_t)gran) == 0);
}

static ft_status_t bounds_check(const ft_blockdev_t* bdev, uint32_t addr, size_t len) {
    if (!bdev) return FT_ERR_INVALID_ARG;
    if ((uint64_t)addr + (uint64_t)len > (uint64_t)bdev->geom.total_size_bytes) return FT_ERR_IO;
    return FT_OK;
}

ft_status_t ft_blockdev_read(const ft_blockdev_t* bdev, uint32_t addr, void* out, size_t len) {
    if (!bdev || !out) return FT_ERR_INVALID_ARG;
    ft_status_t st = bounds_check(bdev, addr, len);
    if (st != FT_OK) return st;
    if (!is_aligned(addr, bdev->geom.read_granularity) || !len_aligned(len, bdev->geom.read_granularity)) {
        return FT_ERR_INVALID_ARG;
    }
    return bdev->ops.read(bdev->ctx, addr, out, len);
}

ft_status_t ft_blockdev_prog(const ft_blockdev_t* bdev, uint32_t addr, const void* data, size_t len) {
    if (!bdev || (!data && len != 0)) return FT_ERR_INVALID_ARG;
    ft_status_t st = bounds_check(bdev, addr, len);
    if (st != FT_OK) return st;
    if (!is_aligned(addr, bdev->geom.prog_granularity) || !len_aligned(len, bdev->geom.prog_granularity)) {
        return FT_ERR_INVALID_ARG;
    }
    return bdev->ops.prog(bdev->ctx, addr, data, len);
}

ft_status_t ft_blockdev_erase(const ft_blockdev_t* bdev, uint32_t addr, size_t len) {
    if (!bdev) return FT_ERR_INVALID_ARG;
    ft_status_t st = bounds_check(bdev, addr, len);
    if (st != FT_OK) return st;
    if (!is_aligned(addr, bdev->geom.erase_granularity) || !len_aligned(len, bdev->geom.erase_granularity)) {
        return FT_ERR_INVALID_ARG;
    }
    return bdev->ops.erase(bdev->ctx, addr, len);
}

