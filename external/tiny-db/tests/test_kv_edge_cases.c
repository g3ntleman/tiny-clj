// test_kv_edge_cases.c - Edge cases for KV API (BSD-btree backend).

#include "unity.h"

#include "tiny_db.h"
#include "tdb_blockdev.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t* buf;
    size_t len;
} ramdev_t;

static tdb_status_t ram_read(void* ctx, uint32_t addr, void* out, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    memcpy(out, r->buf + addr, len);
    return TDB_OK;
}

static tdb_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++)
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    return TDB_OK;
}

static tdb_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return TDB_OK;
}

static void make_bdev(tdb_blockdev_t* bdev, ramdev_t* rd, uint8_t* storage, size_t storage_len,
                      int erase) {
    if (erase)
        memset(storage, 0xFF, storage_len);
    rd->buf = storage;
    rd->len = storage_len;
    bdev->ctx = rd;
    bdev->ops.read = ram_read;
    bdev->ops.prog = ram_prog;
    bdev->ops.erase = ram_erase;
    bdev->geom.total_size_bytes = (uint32_t)storage_len;
    bdev->geom.read_granularity = 1;
    bdev->geom.prog_granularity = 1;
    bdev->geom.erase_granularity = 4096;
}

/**
 * @brief test_invalid_args_are_rejected.
 */
static void test_invalid_args_are_rejected(void) {
    uint8_t storage[8192];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&db, &bdev, NULL));

    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_put(NULL, "k", 1, "v", 1));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_put(db, NULL, 1, "v", 1));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_put(db, "k", 1, NULL, 1));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_get(NULL, "k", 1, &(tdb_blob_t){0}));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_get(db, "k", 1, NULL));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_get(db, NULL, 1, &(tdb_blob_t){0}));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_del(NULL, "k", 1));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_del(db, NULL, 1));

    tdb_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_cursor_open_prefix(NULL, "a", 1, &cur));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_cursor_open_prefix(db, NULL, 1, &cur));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_cursor_next(NULL, &(int){0}));

    tdb_kv_close(db);
}

/**
 * @brief test_delete_missing_key_returns_not_found.
 */
static void test_delete_missing_key_returns_not_found(void) {
    uint8_t storage[8192];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&db, &bdev, NULL));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_NOT_FOUND, tdb_kv_del(db, "nope", 4));
    tdb_kv_close(db);
}

/**
 * @brief test_empty_db_and_empty_prefix_cursor.
 */
static void test_empty_db_and_empty_prefix_cursor(void) {
    uint8_t storage[8192];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&db, &bdev, NULL));

    // Empty DB.
    tdb_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_open_prefix(db, NULL, 0, &cur));
    int has = 1;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);
    tdb_kv_cursor_close(cur);

    // Insert keys and iterate with empty prefix (all keys).
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, "b", 1, "1", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, "a", 1, "2", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, "aa", 2, "3", 1));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_open_prefix(db, NULL, 0, &cur));
    tdb_blob_t k = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("a", k.data, 1);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)k.len);

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("aa", k.data, 2);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)k.len);

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("b", k.data, 1);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)k.len);

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);
    tdb_kv_cursor_close(cur);

    tdb_kv_close(db);
}

/**
 * @brief test_binary_keys_with_nul_bytes_roundtrip_and_prefix.
 */
static void test_binary_keys_with_nul_bytes_roundtrip_and_prefix(void) {
    uint8_t storage[16384];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&db, &bdev, NULL));

    const uint8_t k1[] = {0x00, 0x01, 0x00, 0x02};
    const uint8_t k2[] = {0x00, 0x01, 0x00, 0x03};
    const uint8_t k3[] = {0x00, 0x02};

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, k1, sizeof(k1), "A", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, k2, sizeof(k2), "B", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, k3, sizeof(k3), "C", 1));

    tdb_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_get(db, k2, sizeof(k2), &out));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)out.len);
    TEST_ASSERT_EQUAL_MEMORY("B", out.data, 1);

    // Prefix {0x00,0x01,0x00} matches k1 and k2 (in lex order).
    const uint8_t pfx[] = {0x00, 0x01, 0x00};
    tdb_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_open_prefix(db, pfx, sizeof(pfx), &cur));
    int has = 0;
    tdb_blob_t k = {0};

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(sizeof(k1), (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY(k1, k.data, sizeof(k1));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(sizeof(k2), (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY(k2, k.data, sizeof(k2));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);
    tdb_kv_cursor_close(cur);

    tdb_kv_close(db);
}

/**
 * @brief test_get_len_and_get_into_truncation.
 */
static void test_get_len_and_get_into_truncation(void) {
    uint8_t storage[16384];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&db, &bdev, NULL));

    const char key[] = "k";
    const uint8_t val[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(db, key, 1, val, sizeof(val)));

    size_t len = 123;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_get_len(db, key, 1, &len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(val), (uint32_t)len);

    uint8_t small[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    size_t saved = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_get_into(db, key, 1, small, sizeof(small), &saved));
    TEST_ASSERT_EQUAL_UINT32(sizeof(val), (uint32_t)saved);
    TEST_ASSERT_EQUAL_MEMORY(val, small, sizeof(small));

    // Missing key.
    len = 999;
    TEST_ASSERT_EQUAL_INT(TDB_ERR_NOT_FOUND, tdb_kv_get_len(db, "no", 2, &len));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)len);

    tdb_kv_close(db);
}

/**
 * @brief test_large_value_is_rejected_without_overflow_pages.
 */
static void test_large_value_is_rejected_without_overflow_pages(void) {
    static uint8_t storage[262144];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    // Create a large value which previously required overflow pages.
    // tiny-db policy: values must fit inline (chunking is handled above this layer).
    const size_t n = 20000;
    uint8_t* val = (uint8_t*)malloc(n);
    TEST_ASSERT_NOT_NULL(val);
    for (size_t i = 0; i < n; i++)
        val[i] = (uint8_t)(i * 131u + 7u);

    tdb_kv_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&db, &bdev, NULL));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_put(db, "big", 3, val, n));

    tdb_kv_close(db);
    free(val);
}

/**
 * @brief test_kv_max_val_len_matches_runtime_rejection.
 */
static void test_kv_max_val_len_matches_runtime_rejection(void) {
    uint8_t storage[262144];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const size_t key_len = 12;
    size_t max_val = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_max_val_len(kv, key_len, &max_val));
    TEST_ASSERT_TRUE(max_val > 0);

    uint8_t* key = (uint8_t*)malloc(key_len);
    TEST_ASSERT_NOT_NULL(key);
    memset(key, 'K', key_len);

    uint8_t* v_ok = (uint8_t*)malloc(max_val);
    TEST_ASSERT_NOT_NULL(v_ok);
    memset(v_ok, 0xA5, max_val);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, key, key_len, v_ok, max_val));

    uint8_t* v_too_big = (uint8_t*)malloc(max_val + 1);
    TEST_ASSERT_NOT_NULL(v_too_big);
    memset(v_too_big, 0x5A, max_val + 1);
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_kv_put(kv, key, key_len, v_too_big, max_val + 1));

    free(v_too_big);
    free(v_ok);
    free(key);
    tdb_kv_close(kv);
}

/**
 * @brief test_kv_gc_step_more_reports_no_work_on_fresh_db.
 */
static void test_kv_gc_step_more_reports_no_work_on_fresh_db(void) {
    uint8_t storage[262144];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage), 1);

    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    int rc = tdb_kv_gc_step_more(kv, 0);
    TEST_ASSERT_EQUAL_INT(0, rc);

    tdb_kv_close(kv);
}

/**
 * @brief tdb_register_tests_kv_edge_cases.
 */
void tdb_register_tests_kv_edge_cases(void) {
    RUN_TEST(test_invalid_args_are_rejected);
    RUN_TEST(test_delete_missing_key_returns_not_found);
    RUN_TEST(test_empty_db_and_empty_prefix_cursor);
    RUN_TEST(test_binary_keys_with_nul_bytes_roundtrip_and_prefix);
    RUN_TEST(test_get_len_and_get_into_truncation);
    RUN_TEST(test_large_value_is_rejected_without_overflow_pages);
    RUN_TEST(test_kv_max_val_len_matches_runtime_rejection);
    RUN_TEST(test_kv_gc_step_more_reports_no_work_on_fresh_db);
}
