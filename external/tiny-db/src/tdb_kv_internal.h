// tdb_kv_internal.h - Internal helpers for KV implementation.
//
// Not part of the public API. Only used within tiny-db/src.
#pragma once

#include "tiny_db.h"

/* Some compilation units set this via compiler flags; avoid macro-redefinition warnings. */
#ifndef __DBINTERFACE_PRIVATE
#define __DBINTERFACE_PRIVATE
#endif

#include "tdb_bsd_mpool.h"

#include <stddef.h>
#include <stdint.h>

/* Keep the KV handle definition in one place for internal users. */
struct tdb_kv {
    tdb_blockdev_t* bdev;
    DB* bdb; /* BSD B-Tree handle */

    /* Scratch buffer for tdb_get */
    uint8_t* get_buf;
    size_t get_cap;

    /* Persistent GC state (stored as system keys) */
    uint32_t gc_cursor;  /* Next page index for incremental GC */
    uint32_t free_head;  /* Head of mpool tombstone free-list (pgno or PGNO_INVALID) */
    uint32_t alloc_next; /* Next new pgno (monotonic) */

    /* Periodic persistence */
    uint32_t gc_persist_counter;
    int gc_dirty;
};

/* Implemented in tdb_kv.c (needs BTREE definition). */
MPOOL* tdb_kv_get_mpool(tdb_kv_t* kv);
