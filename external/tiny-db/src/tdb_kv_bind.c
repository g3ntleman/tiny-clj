// tdb_kv_bind.c - Single-threaded KV binding implementation.

#include "tdb_kv_bind.h"

static tdb_blockdev_t* g_ft_kv_bdev = 0;
static uint32_t g_ft_kv_base_offset = 0;

void tdb_kv_bind(tdb_blockdev_t* bdev, uint32_t base_offset) {
    g_ft_kv_bdev = bdev;
    g_ft_kv_base_offset = base_offset;
}

void tdb_kv_unbind(void) {
    g_ft_kv_bdev = 0;
    g_ft_kv_base_offset = 0;
}

tdb_blockdev_t* tdb_kv_bound_bdev(void) {
    return g_ft_kv_bdev;
}

uint32_t tdb_kv_bound_base_offset(void) {
    return g_ft_kv_base_offset;
}
