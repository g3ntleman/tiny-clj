// test_gc.c - GC step guardrails (no compaction yet).

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

static ft_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

static void make_bdev(ft_blockdev_t* bdev, ramdev_t* rd, uint8_t* storage, size_t storage_len) {
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
    /* FlashDB KVDB uses erase granularity as its "sector size". */
    bdev->geom.erase_granularity = 4096;
}

static void test_gc_allows_open_cursor_and_preserves_snapshot(void) {
    static uint8_t storage[16384];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_db_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_db_init(&db, &bdev, NULL));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "a", 1, "1", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "aa", 2, "2", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "ab", 2, "3", 1));

    ft_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_open_prefix(db, "a", 1, &cur));
    TEST_ASSERT_NOT_NULL(cur);

    /* GC must not disturb the active cursor. */
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_gc_step(db, 0));

    int has = 0;
    ft_blob_t k = {0};

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("a", k.data, 1);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("aa", k.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("ab", k.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);

    ft_cursor_close(cur);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_gc_step(db, 0));

    ft_db_deinit(db);
}

static void test_recovery_persists_across_reinit(void) {
    static uint8_t storage[16384];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_db_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_db_init(&db, &bdev, NULL));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "k", 1, "v", 1));
    ft_db_deinit(db);

    /* Re-open on the same backing store, without erasing. */
    db = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_db_init(&db, &bdev, NULL));
    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_get(db, "k", 1, &out));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)out.len);
    TEST_ASSERT_EQUAL_MEMORY("v", out.data, 1);
    ft_db_deinit(db);
}

static void test_gc_preserves_latest_value_after_many_updates(void) {
    static uint8_t storage[65536];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_db_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_db_init(&db, &bdev, NULL));

    /* Create garbage by rewriting the same key many times. */
    for (int i = 0; i < 200; i++) {
        char v = (char)('A' + (i % 26));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "k", 1, &v, 1));
    }

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_gc_step(db, 0));

    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_get(db, "k", 1, &out));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)out.len);
    /* Last written value (i=199). */
    char expect = (char)('A' + (199 % 26));
    TEST_ASSERT_EQUAL_MEMORY(&expect, out.data, 1);

    ft_db_deinit(db);
}

void ft_register_tests_gc(void) {
    RUN_TEST(test_gc_allows_open_cursor_and_preserves_snapshot);
    RUN_TEST(test_recovery_persists_across_reinit);
    RUN_TEST(test_gc_preserves_latest_value_after_many_updates);
}

