// ft_blockdev.h - Pluggable block device abstraction (test-first).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "flash_tree.h"

typedef struct ft_blockdev_ops {
    ft_status_t (*read)(void* ctx, uint32_t addr, void* out, size_t len);
    ft_status_t (*prog)(void* ctx, uint32_t addr, const void* data, size_t len);
    ft_status_t (*erase)(void* ctx, uint32_t addr, size_t len);
} ft_blockdev_ops_t;

typedef struct ft_blockdev_geom {
    uint32_t total_size_bytes;
    uint32_t read_granularity;
    uint32_t prog_granularity;
    uint32_t erase_granularity;
} ft_blockdev_geom_t;

struct ft_blockdev {
    void* ctx;
    ft_blockdev_ops_t ops;
    ft_blockdev_geom_t geom;
};

// Simple validators for tests.
ft_status_t ft_blockdev_validate(const ft_blockdev_t* bdev);

// Convenience wrappers that enforce geometry (bounds + granularity).
ft_status_t ft_blockdev_read(const ft_blockdev_t* bdev, uint32_t addr, void* out, size_t len);
ft_status_t ft_blockdev_prog(const ft_blockdev_t* bdev, uint32_t addr, const void* data,
                             size_t len);
ft_status_t ft_blockdev_erase(const ft_blockdev_t* bdev, uint32_t addr, size_t len);
