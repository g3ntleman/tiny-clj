// test_tsdb_agg.c - TSDB aggregate_f32 smoke.

#include "unity.h"

#include "flash_tree.h"
#include "ft_blockdev.h"

#include <string.h>

static void test_tsdb_aggregate_f32(void) {
    ft_tsdb_t* tsdb = NULL;
    ft_blockdev_t dummy = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_tsdb_init(&tsdb, &dummy, NULL));

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

