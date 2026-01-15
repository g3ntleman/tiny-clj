/*
 * FlashDB port context for Flash-Tree.
 *
 * This struct is passed as fdb_db_t.user_data and lets the FlashDB TSDB module
 * operate on a sub-region of an ft_blockdev_t (offset by base_offset).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

typedef struct ft_blockdev ft_blockdev_t;

typedef struct ft_fdb_port_ctx {
    ft_blockdev_t* bdev;
    uint32_t base_offset;
    uint32_t region_bytes;
} ft_fdb_port_ctx_t;

