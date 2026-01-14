// ft_kv_internal.h - Internal helpers for KV implementation.
//
// Not part of the public API. Only used within flash-tree/src.
#pragma once

#include "flash_tree.h"

#include <stddef.h>
#include <stdint.h>

/* Internal btree/db types (do not pull in unguarded btree headers here). */
#define __DBINTERFACE_PRIVATE
#include "ft_bsd_db.h"

typedef struct MPOOL MPOOL;

/* Keep the KV handle definition in one place for internal users. */
struct ft_kv {
    ft_blockdev_t* bdev;
    DB* bdb; /* BSD B-Tree handle */

    /* Scratch buffer for ft_get */
    uint8_t* get_buf;
    size_t get_cap;
};

/* Implemented in ft_kv.c (needs BTREE definition). */
MPOOL* ft_kv_get_mpool(ft_kv_t* kv);
