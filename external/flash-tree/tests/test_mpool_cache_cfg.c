// test_mpool_cache_cfg.c - Tests for mpool cache configuration API.

#include "unity.h"

#include "flash_tree.h"

static void test_cache_api_set_get(void) {
    const uint32_t old = ft_mpool_get_cache_pagecount();

    TEST_ASSERT_TRUE(old >= 3);
    TEST_ASSERT_EQUAL_INT(FT_ERR_INVALID_ARG, ft_mpool_set_cache_pagecount(2));
    TEST_ASSERT_EQUAL_INT(FT_ERR_INVALID_ARG, ft_mpool_set_cache_pagecount(1000000u));

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_set_cache_pagecount(3));
    TEST_ASSERT_EQUAL_UINT32(3u, ft_mpool_get_cache_pagecount());

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_set_cache_pagecount(old));
    TEST_ASSERT_EQUAL_UINT32(old, ft_mpool_get_cache_pagecount());
}

static void test_psram_autosize_toggle_is_callable(void) {
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_enable_psram_autosize(0));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_enable_psram_autosize(1));
}

void ft_register_tests_mpool_cache_cfg(void) {
    RUN_TEST(test_cache_api_set_get);
    RUN_TEST(test_psram_autosize_toggle_is_callable);
}

