// test_btree_rw.c - put/get/del + overwrite semantics + snapshot cursor tests.

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

static ft_db_t* make_db(void) {
    static uint8_t storage[4096];
    static ramdev_t rd;
    rd.buf = storage;
    rd.len = sizeof(storage);

    static ft_blockdev_t bdev;
    bdev.ctx = &rd;
    bdev.ops.read = ram_read;
    bdev.ops.prog = ram_prog;
    bdev.ops.erase = ram_erase;
    bdev.geom.total_size_bytes = (uint32_t)sizeof(storage);
    bdev.geom.read_granularity = 1;
    bdev.geom.prog_granularity = 1;
    bdev.geom.erase_granularity = 16;

    ft_db_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_db_init(&db, &bdev, NULL));
    return db;
}

static void test_put_get_overwrite_del(void) {
    ft_db_t* db = make_db();

    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_ERR_NOT_FOUND, ft_get(db, "k", 1, &out));

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "k", 1, "v1", 2));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_get(db, "k", 1, &out));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)out.len);
    TEST_ASSERT_EQUAL_MEMORY("v1", out.data, 2);

    // overwrite
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "k", 1, "v2", 2));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_get(db, "k", 1, &out));
    TEST_ASSERT_EQUAL_MEMORY("v2", out.data, 2);

    // delete
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_del(db, "k", 1));
    TEST_ASSERT_EQUAL_INT(FT_ERR_NOT_FOUND, ft_get(db, "k", 1, &out));

    ft_db_deinit(db);
}

static void test_cursor_snapshot_is_stable(void) {
    ft_db_t* db = make_db();

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "a", 1, "1", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "aa", 2, "2", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "ab", 2, "3", 1));

    ft_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_open_prefix(db, "a", 1, &cur));
    TEST_ASSERT_NOT_NULL(cur);

    // Mutate after opening cursor; snapshot must not change.
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_put(db, "aa0", 3, "X", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_del(db, "ab", 2));

    int has = 0;
    ft_blob_t k = {0}, v = {0};

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_val(cur, &v));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY("a", k.data, 1);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_val(cur, &v));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY("aa", k.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY("ab", k.data, 2); // still present in snapshot

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);

    ft_cursor_close(cur);
    ft_db_deinit(db);
}

void ft_register_tests_btree_rw(void) {
    RUN_TEST(test_put_get_overwrite_del);
    RUN_TEST(test_cursor_snapshot_is_stable);
}

