// ft_kv_bind.h - Single-threaded binding for KV backend.
//
// The 4.4BSD btree code expects to open a "file". In flash-tree, we bind a
// block device + base offset prior to opening the btree, and the lower layers
// (btree/mpool) use this binding to access flash.

#pragma once

#include <stdint.h>

#include "ft_blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

void ft_kv_bind(ft_blockdev_t* bdev, uint32_t base_offset);
void ft_kv_unbind(void);

ft_blockdev_t* ft_kv_bound_bdev(void);
uint32_t ft_kv_bound_base_offset(void);

#ifdef __cplusplus
} // extern "C"
#endif
