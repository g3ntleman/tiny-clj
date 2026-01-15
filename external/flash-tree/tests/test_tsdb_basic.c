// test_tsdb_basic.c - TSDB append + iter_by_time + query_count smoke.

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
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    memcpy(out, r->buf + addr, len);
    return FT_OK;
}

// Simulate NOR flash programming: bits can only go from 1 -> 0.
static ft_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    }
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

typedef struct {
    int calls;
    ft_time_t last_t;
} iter_ctx_t;

static ft_status_t iter_cb(ft_time_t t, const void* data, size_t len, ft_tsl_status_t status,
                           void* arg) {
    (void)data;
    (void)len;
    iter_ctx_t* ctx = (iter_ctx_t*)arg;
    TEST_ASSERT_EQUAL_INT(FT_TSL_STATUS_OK, status);
    ctx->calls++;
    ctx->last_t = t;
    return FT_OK;
}

static void test_tsdb_append_iter_count(void) {
    ft_tsdb_t* tsdb = NULL;

    static uint8_t storage[4096 * 8];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};
    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage),
                 .read_granularity = 1,
                 .prog_granularity = 1,
                 .erase_granularity = 4096},
    };

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blockdev_validate(&bdev));
    ft_tsdb_cfg_t cfg = {
        .range = {.base_offset = 0u, .len = (uint32_t)sizeof(storage)},
        .max_len = 0,
    };
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&tsdb, &bdev, &cfg));
    TEST_ASSERT_NOT_NULL(tsdb);

    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, a, sizeof(a), 10));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, b, sizeof(b), 20));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, b, sizeof(b), 30));

    iter_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_iter_by_time(tsdb, 15, 30, iter_cb, &ctx));
    TEST_ASSERT_EQUAL_INT(2, ctx.calls);
    TEST_ASSERT_EQUAL_UINT64(30, (uint64_t)ctx.last_t);

    uint64_t count = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_query_count(tsdb, 0, 100, FT_TSL_STATUS_OK, &count));
    TEST_ASSERT_EQUAL_UINT64(3, count);

    ft_tsdb_deinit(tsdb);
}

void ft_register_tests_tsdb_basic(void) {
    RUN_TEST(test_tsdb_append_iter_count);
}
