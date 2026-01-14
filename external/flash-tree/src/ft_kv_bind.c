// ft_kv_bind.c - Single-threaded KV binding implementation.

#include "ft_kv_bind.h"

static ft_blockdev_t* g_ft_kv_bdev = 0;
static uint32_t g_ft_kv_base_offset = 0;

void ft_kv_bind(ft_blockdev_t* bdev, uint32_t base_offset) {
    g_ft_kv_bdev = bdev;
    g_ft_kv_base_offset = base_offset;
}

void ft_kv_unbind(void) {
    g_ft_kv_bdev = 0;
    g_ft_kv_base_offset = 0;
}

ft_blockdev_t* ft_kv_bound_bdev(void) {
    return g_ft_kv_bdev;
}

uint32_t ft_kv_bound_base_offset(void) {
    return g_ft_kv_base_offset;
}
