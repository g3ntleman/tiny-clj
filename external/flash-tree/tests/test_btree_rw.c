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
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    memcpy(out, r->buf + addr, len);
    return FT_OK;
}

static ft_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++)
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

static ft_kv_t* make_kv(void) {
    static uint8_t storage[8192];
    static ramdev_t rd;
    rd.buf = storage;
    rd.len = sizeof(storage);
    /* Fresh flash starts erased (0xFF). */
    memset(storage, 0xFF, sizeof(storage));

    static ft_blockdev_t bdev;
    bdev.ctx = &rd;
    bdev.ops.read = ram_read;
    bdev.ops.prog = ram_prog;
    bdev.ops.erase = ram_erase;
    bdev.geom.total_size_bytes = (uint32_t)sizeof(storage);
    bdev.geom.read_granularity = 1;
    bdev.geom.prog_granularity = 1;
    /* FlashDB KVDB uses erase granularity as its "sector size". */
    bdev.geom.erase_granularity = 4096;

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    return kv;
}

static void test_put_get_overwrite_del(void) {
    ft_kv_t* kv = make_kv();

    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_ERR_NOT_FOUND, ft_kv_get(kv, "k", 1, &out));

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "k", 1, "v1", 2));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv, "k", 1, &out));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)out.len);
    TEST_ASSERT_EQUAL_MEMORY("v1", out.data, 2);

    // overwrite
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "k", 1, "v2", 2));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv, "k", 1, &out));
    TEST_ASSERT_EQUAL_MEMORY("v2", out.data, 2);

    // delete
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_del(kv, "k", 1));
    TEST_ASSERT_EQUAL_INT(FT_ERR_NOT_FOUND, ft_kv_get(kv, "k", 1, &out));

    ft_kv_close(kv);
}

static void test_cursor_snapshot_is_stable(void) {
    TEST_IGNORE_MESSAGE("MVCC not supported - no snapshot isolation");
    ft_kv_t* kv = make_kv();

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "a", 1, "1", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "aa", 2, "2", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "ab", 2, "3", 1));

    ft_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_open_prefix(kv, "a", 1, &cur));
    TEST_ASSERT_NOT_NULL(cur);

    // Mutate after opening cursor; snapshot must not change.
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "aa0", 3, "X", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_del(kv, "ab", 2));

    int has = 0;
    ft_blob_t k = {0}, v = {0};

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_val(cur, &v));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY("a", k.data, 1);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_val(cur, &v));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY("aa", k.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY("ab", k.data, 2); // still present in snapshot

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);

    ft_kv_cursor_close(cur);
    ft_kv_close(kv);
}

static void test_two_cursors_are_independent_snapshots(void) {
    TEST_IGNORE_MESSAGE("MVCC not supported - no snapshot isolation");
    ft_kv_t* kv = make_kv();

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "a", 1, "1", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "aa", 2, "2", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "ab", 2, "3", 1));

    ft_kv_cursor_t* a = NULL;
    ft_kv_cursor_t* b = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_open_prefix(kv, "a", 1, &a));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_open_prefix(kv, "a", 1, &b));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    // Mutate after opening cursors; both snapshots must remain unchanged.
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "aa0", 3, "X", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_del(kv, "ab", 2));

    int has_a = 0, has_b = 0;
    ft_blob_t ka = {0}, kb = {0};

    // Advance A once, then B twice, then A to completion (interleaved).
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(a, &has_a));
    TEST_ASSERT_TRUE(has_a);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(a, &ka));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)ka.len);
    TEST_ASSERT_EQUAL_MEMORY("a", ka.data, 1);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(b, &has_b));
    TEST_ASSERT_TRUE(has_b);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(b, &kb));
    TEST_ASSERT_EQUAL_MEMORY("a", kb.data, 1);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(b, &has_b));
    TEST_ASSERT_TRUE(has_b);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(b, &kb));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)kb.len);
    TEST_ASSERT_EQUAL_MEMORY("aa", kb.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(a, &has_a));
    TEST_ASSERT_TRUE(has_a);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(a, &ka));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)ka.len);
    TEST_ASSERT_EQUAL_MEMORY("aa", ka.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(a, &has_a));
    TEST_ASSERT_TRUE(has_a);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(a, &ka));
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)ka.len);
    TEST_ASSERT_EQUAL_MEMORY("ab", ka.data, 2); // still present in snapshot

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(a, &has_a));
    TEST_ASSERT_FALSE(has_a);

    // B should still have one remaining item: "ab".
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(b, &has_b));
    TEST_ASSERT_TRUE(has_b);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(b, &kb));
    TEST_ASSERT_EQUAL_MEMORY("ab", kb.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(b, &has_b));
    TEST_ASSERT_FALSE(has_b);

    ft_kv_cursor_close(a);
    ft_kv_cursor_close(b);
    ft_kv_close(kv);
}

void ft_register_tests_btree_rw(void) {
    RUN_TEST(test_put_get_overwrite_del);
    RUN_TEST(test_cursor_snapshot_is_stable);
    RUN_TEST(test_two_cursors_are_independent_snapshots);
}
