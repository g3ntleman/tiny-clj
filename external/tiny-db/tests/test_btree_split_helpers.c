// test_btree_split_helpers.c - Unit tests for internal split-space helpers.

#include "unity.h"

#define __DBINTERFACE_PRIVATE
#include "tdb_bsd_btree.h"

#include <string.h>

/**
 * @brief test_bt_would_split_empty_page.
 */
static void test_bt_would_split_empty_page(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    PAGE* h = (PAGE*)buf;
    h->lower = BTDATAOFF;
    h->upper = (indx_t)sizeof(buf);
    TEST_ASSERT_EQUAL_INT(0, __bt_would_split(h, 16));
}

/**
 * @brief test_bt_would_split_almost_full.
 */
static void test_bt_would_split_almost_full(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    PAGE* h = (PAGE*)buf;
    /* Exactly enough space => should NOT split (strictly '<' check). */
    size_t nbytes = 64;
    h->lower = 100;
    h->upper = (indx_t)(h->lower + nbytes + sizeof(indx_t));
    TEST_ASSERT_EQUAL_INT(0, __bt_would_split(h, nbytes));
}

/**
 * @brief test_bt_would_split_overflow.
 */
static void test_bt_would_split_overflow(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    PAGE* h = (PAGE*)buf;
    size_t nbytes = 64;
    /* One byte short => must split. */
    h->lower = 100;
    h->upper = (indx_t)(h->lower + nbytes + sizeof(indx_t) - 1);
    TEST_ASSERT_EQUAL_INT(1, __bt_would_split(h, nbytes));
}

/**
 * @brief tdb_register_tests_btree_split_helpers.
 */
void tdb_register_tests_btree_split_helpers(void) {
    RUN_TEST(test_bt_would_split_empty_page);
    RUN_TEST(test_bt_would_split_almost_full);
    RUN_TEST(test_bt_would_split_overflow);
}

