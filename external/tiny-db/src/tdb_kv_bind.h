// tdb_kv_bind.h - Single-threaded binding for KV backend.
//
// The 4.4BSD btree code expects to open a "file". In tiny-db, we bind a
// block device + base offset prior to opening the btree, and the lower layers
// (btree/mpool) use this binding to access flash.

#pragma once

#include <stdint.h>

#include "tdb_blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

void tdb_kv_bind(tdb_blockdev_t* bdev, uint32_t base_offset);
void tdb_kv_unbind(void);

tdb_blockdev_t* tdb_kv_bound_bdev(void);
uint32_t tdb_kv_bound_base_offset(void);

#ifdef __cplusplus
} // extern "C"
#endif
