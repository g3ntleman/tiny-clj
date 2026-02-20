// test_mpool_cache_cfg.c - Tests for mpool cache configuration API.

#include "unity.h"

#include "tiny_db.h"

/**
 * @brief test_cache_api_set_get.
 */
static void test_cache_api_set_get(void) {
    const uint32_t old = tdb_mpool_get_cache_pagecount();

    TEST_ASSERT_TRUE(old >= 3);
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_mpool_set_cache_pagecount(2));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_mpool_set_cache_pagecount(1000000u));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_mpool_set_cache_pagecount(3));
    TEST_ASSERT_EQUAL_UINT32(3u, tdb_mpool_get_cache_pagecount());

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_mpool_set_cache_pagecount(old));
    TEST_ASSERT_EQUAL_UINT32(old, tdb_mpool_get_cache_pagecount());
}

/**
 * @brief test_psram_autosize_toggle_is_callable.
 */
static void test_psram_autosize_toggle_is_callable(void) {
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_mpool_enable_psram_autosize(0));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_mpool_enable_psram_autosize(1));
}

/**
 * @brief tdb_register_tests_mpool_cache_cfg.
 */
void tdb_register_tests_mpool_cache_cfg(void) {
    RUN_TEST(test_cache_api_set_get);
    RUN_TEST(test_psram_autosize_toggle_is_callable);
}

