// test_tsdb_multi.c - Two TSDB instances on a shared blockdev (disjoint ranges).

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
    const ft_time_t* times;
    const float* vals;
    size_t n;
    size_t idx;
    ft_time_t last_t;
} iter_expect_ctx_t;

static ft_status_t iter_expect_cb(ft_time_t t, const void* data, size_t len, ft_tsl_status_t status,
                                  void* arg) {
    iter_expect_ctx_t* c = (iter_expect_ctx_t*)arg;
    TEST_ASSERT_EQUAL_INT(FT_TSL_STATUS_OK, status);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(c->idx < c->n);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)c->times[c->idx], (uint64_t)t);
    if (c->idx > 0) {
        TEST_ASSERT_TRUE(t > c->last_t);
    }
    c->last_t = t;

    TEST_ASSERT_EQUAL_UINT32(sizeof(float), (uint32_t)len);
    float v = 0.0f;
    memcpy(&v, data, sizeof(v));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, c->vals[c->idx], v);

    c->idx++;
    return FT_OK;
}

static void test_two_tsdbs_independent_on_same_blockdev(void) {
    const uint32_t erase_gran = 4096u;
    static uint8_t storage[4096 * 16];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};
    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage),
                 .read_granularity = 1,
                 .prog_granularity = 1,
                 .erase_granularity = erase_gran},
    };
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blockdev_validate(&bdev));

    ft_tsdb_cfg_t cfg_a = {
        .range = {.base_offset = 0u * erase_gran, .len = 4u * erase_gran},
        .max_len = 0,
    };
    ft_tsdb_cfg_t cfg_b = {
        .range = {.base_offset = 8u * erase_gran, .len = 4u * erase_gran},
        .max_len = 0,
    };

    ft_tsdb_t* a = NULL;
    ft_tsdb_t* b = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&a, &bdev, &cfg_a));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&b, &bdev, &cfg_b));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    float a1 = 1.0f, a2 = 2.0f, a3 = 3.0f;
    float b1 = -1.0f, b2 = -2.0f;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(a, &a1, sizeof(a1), 10));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(a, &a2, sizeof(a2), 20));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(a, &a3, sizeof(a3), 30));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(b, &b1, sizeof(b1), 15));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(b, &b2, sizeof(b2), 25));

    uint64_t count = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_query_count(a, 0, 100, FT_TSL_STATUS_OK, &count));
    TEST_ASSERT_EQUAL_UINT64(3, count);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_query_count(b, 0, 100, FT_TSL_STATUS_OK, &count));
    TEST_ASSERT_EQUAL_UINT64(2, count);

    const ft_time_t at[] = {10, 20, 30};
    const float av[] = {1.0f, 2.0f, 3.0f};
    iter_expect_ctx_t actx = {.times = at, .vals = av, .n = 3, .idx = 0, .last_t = 0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_iter_by_time(a, 0, 100, iter_expect_cb, &actx));
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)actx.idx);

    const ft_time_t bt[] = {15, 25};
    const float bv[] = {-1.0f, -2.0f};
    iter_expect_ctx_t bctx = {.times = bt, .vals = bv, .n = 2, .idx = 0, .last_t = 0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_iter_by_time(b, 0, 100, iter_expect_cb, &bctx));
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)bctx.idx);

    ft_agg_f32_t agg = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_aggregate_f32(a, 0, 100, FT_TSL_STATUS_OK, &agg));
    TEST_ASSERT_EQUAL_UINT64(3, agg.count);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.0f, agg.sum);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, agg.min);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, agg.max);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, agg.avg);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_aggregate_f32(b, 0, 100, FT_TSL_STATUS_OK, &agg));
    TEST_ASSERT_EQUAL_UINT64(2, agg.count);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -3.0f, agg.sum);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -2.0f, agg.min);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, agg.max);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.5f, agg.avg);

    ft_tsdb_deinit(a);
    ft_tsdb_deinit(b);
}

static void test_tsdb_init_fails_on_non_empty_region(void) {
    const uint32_t erase_gran = 4096u;
    static uint8_t storage[4096 * 8];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};
    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage),
                 .read_granularity = 1,
                 .prog_granularity = 1,
                 .erase_granularity = erase_gran},
    };
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blockdev_validate(&bdev));

    ft_tsdb_cfg_t cfg = {
        .range = {.base_offset = 0u, .len = (uint32_t)sizeof(storage)},
        .max_len = 0,
    };

    ft_tsdb_t* tsdb = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&tsdb, &bdev, &cfg));
    TEST_ASSERT_NOT_NULL(tsdb);

    float v = 1.0f;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsl_append(tsdb, &v, sizeof(v), 10));
    ft_tsdb_deinit(tsdb);

    // Same range is no longer erased: must fail the empty-region policy.
    tsdb = NULL;
    TEST_ASSERT_EQUAL_INT(FT_ERR_INVALID_ARG, ft_tsdb_init(&tsdb, &bdev, &cfg));
    TEST_ASSERT_NULL(tsdb);
}

void ft_register_tests_tsdb_multi(void) {
    RUN_TEST(test_two_tsdbs_independent_on_same_blockdev);
    RUN_TEST(test_tsdb_init_fails_on_non_empty_region);
}

