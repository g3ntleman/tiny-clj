// test_tsdb_agg.c - TSDB aggregate_f32 smoke.

#include "unity.h"

#include "flash_tree.h"
#include "ft_blockdev.h"

#include <string.h>

typedef struct {
    uint8_t* buf;
    size_t len;
} ramdev_t;

static ft_status_t ram_read(void* ctx, uint32_t addr, void* out, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memcpy(out, r->buf + addr, len);
    return FT_OK;
}

// Simulate NOR flash programming: bits can only go from 1 -> 0.
static ft_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    }
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

static void test_tsdb_aggregate_f32(void) {
    ft_tsdb_t* tsdb = NULL;
    static uint8_t storage[4096 * 8];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};
    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage), .read_granularity = 1, .prog_granularity = 1, .erase_granularity = 4096},
    };
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blockdev_validate(&bdev));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&tsdb, &bdev, NULL));

    float v1 = 1.0f;
    float v2 = 2.5f;
    float v3 = -3.0f;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, &v1, sizeof(v1), 10));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, &v2, sizeof(v2), 20));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, &v3, sizeof(v3), 30));

    ft_agg_f32_t agg = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_aggregate_f32(tsdb, 0, 100, FT_TSL_STATUS_OK, &agg));
    TEST_ASSERT_EQUAL_UINT64(3, agg.count);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, (1.0f + 2.5f - 3.0f), agg.sum);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -3.0f, agg.min);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, agg.max);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, ((1.0f + 2.5f - 3.0f) / 3.0f), agg.avg);

    ft_tsdb_deinit(tsdb);
}

void ft_register_tests_tsdb_agg(void) {
    RUN_TEST(test_tsdb_aggregate_f32);
}

