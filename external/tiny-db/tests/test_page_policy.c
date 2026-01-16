// test_page_policy.c - Unit tests for sizing policy derivation (Variant B).

#include "unity.h"

#include "tdb_page_policy.h"

static void test_variant_b_4k_erase_16b_header(void) {
    tdb_blockdev_geom_t geom = {
        .total_size_bytes = 1u << 20,
        .read_granularity = 1,
        .prog_granularity = 4,
        .erase_granularity = 4096,
    };

    tdb_page_policy_t pol = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_page_policy_compute_variant_b(&geom, 16, &pol));
    TEST_ASSERT_EQUAL_UINT32(16, pol.header_size);
    TEST_ASSERT_EQUAL_UINT32(4096, pol.record_size);
    TEST_ASSERT_EQUAL_UINT32(4080, pol.page_size);
}

static void test_variant_b_rejects_header_ge_erase(void) {
    tdb_blockdev_geom_t geom = {
        .total_size_bytes = 1u << 20,
        .read_granularity = 1,
        .prog_granularity = 4,
        .erase_granularity = 4096,
    };

    tdb_page_policy_t pol = {0};
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_page_policy_compute_variant_b(&geom, 4096, &pol));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_page_policy_compute_variant_b(&geom, 5000, &pol));
}

static void test_variant_b_rejects_unaligned_header_or_payload(void) {
    tdb_blockdev_geom_t geom = {
        .total_size_bytes = 1u << 20,
        .read_granularity = 4,
        .prog_granularity = 4,
        .erase_granularity = 4096,
    };

    tdb_page_policy_t pol = {0};
    // 2 is not 4-byte aligned.
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_page_policy_compute_variant_b(&geom, 2, &pol));

    // header 12 is aligned, but payload 4084 is also aligned, so this should pass.
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_page_policy_compute_variant_b(&geom, 12, &pol));
    TEST_ASSERT_EQUAL_UINT32(12, pol.header_size);
    TEST_ASSERT_EQUAL_UINT32(4084, pol.page_size);
}

void tdb_register_tests_page_policy(void) {
    RUN_TEST(test_variant_b_4k_erase_16b_header);
    RUN_TEST(test_variant_b_rejects_header_ge_erase);
    RUN_TEST(test_variant_b_rejects_unaligned_header_or_payload);
}
