// tdb_blockdev.h - Pluggable block device abstraction (test-first).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tiny_db.h"

typedef struct tdb_blockdev_ops {
    tdb_status_t (*read)(void* ctx, uint32_t addr, void* out, size_t len);
    tdb_status_t (*prog)(void* ctx, uint32_t addr, const void* data, size_t len);
    tdb_status_t (*erase)(void* ctx, uint32_t addr, size_t len);
} tdb_blockdev_ops_t;

typedef struct tdb_blockdev_geom {
    uint32_t total_size_bytes;
    uint32_t read_granularity;
    uint32_t prog_granularity;
    uint32_t erase_granularity;
} tdb_blockdev_geom_t;

struct tdb_blockdev {
    void* ctx;
    tdb_blockdev_ops_t ops;
    tdb_blockdev_geom_t geom;
};

// Simple validators for tests.
tdb_status_t tdb_blockdev_validate(const tdb_blockdev_t* bdev);

// Convenience wrappers that enforce geometry (bounds + granularity).
tdb_status_t tdb_blockdev_read(const tdb_blockdev_t* bdev, uint32_t addr, void* out, size_t len);
tdb_status_t tdb_blockdev_prog(const tdb_blockdev_t* bdev, uint32_t addr, const void* data,
                             size_t len);
tdb_status_t tdb_blockdev_erase(const tdb_blockdev_t* bdev, uint32_t addr, size_t len);
