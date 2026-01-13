// test_tsdb_basic.c - TSDB append + iter_by_time + query_count smoke.

#include "unity.h"

#include "flash_tree.h"
#include "ft_blockdev.h"

#include <string.h>

typedef struct {
    int calls;
    ft_time_t last_t;
} iter_ctx_t;

static ft_status_t iter_cb(ft_time_t t, const void* data, size_t len, ft_tsl_status_t status, void* arg) {
    (void)data; (void)len;
    iter_ctx_t* ctx = (iter_ctx_t*)arg;
    TEST_ASSERT_EQUAL_INT(FT_TSL_STATUS_OK, status);
    ctx->calls++;
    ctx->last_t = t;
    return FT_OK;
}

static void test_tsdb_append_iter_count(void) {
    ft_tsdb_t* tsdb = NULL;
    // blockdev is currently unused by the in-memory TSDB; pass a non-NULL dummy pointer.
    ft_blockdev_t dummy = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&tsdb, &dummy, NULL));
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

