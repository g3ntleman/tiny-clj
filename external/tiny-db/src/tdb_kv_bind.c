// tdb_kv_bind.c - Single-threaded KV binding implementation.

#include "tdb_kv_bind.h"

static tdb_blockdev_t* g_ft_kv_bdev = 0;
static uint32_t g_ft_kv_base_offset = 0;

/**
 * @brief tdb_kv_bind.
 * @param bdev Block-device descriptor.
 * @param base_offset Offset value.
 */
void tdb_kv_bind(tdb_blockdev_t* bdev, uint32_t base_offset) {
    g_ft_kv_bdev = bdev;
    g_ft_kv_base_offset = base_offset;
}

/**
 * @brief tdb_kv_unbind.
 */
void tdb_kv_unbind(void) {
    g_ft_kv_bdev = 0;
    g_ft_kv_base_offset = 0;
}

/**
 * @brief tdb_kv_bound_bdev.
 * @return Pointer result, or NULL on failure.
 */
tdb_blockdev_t* tdb_kv_bound_bdev(void) {
    return g_ft_kv_bdev;
}

/**
 * @brief tdb_kv_bound_base_offset.
 * @return Computed 32-bit value.
 */
uint32_t tdb_kv_bound_base_offset(void) {
    return g_ft_kv_base_offset;
}
