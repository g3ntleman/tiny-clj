// test_gc_syskeys.c - System-key behavior used for persistent GC state.

#include "unity.h"

#include "tiny_db.h"
#include "tdb_blockdev.h"

#include <stdint.h>
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

static void make_bdev(tdb_blockdev_t* bdev, ramdev_t* rd, uint8_t* storage, size_t storage_len) {
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

static void test_syskey_roundtrip(void) {
    uint8_t storage[16384];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const uint8_t key[] = {0x00, 't', 'e', 's', 't'};
    const uint8_t val[] = {0xA5, 0x00, 0x5A};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, key, sizeof(key), val, sizeof(val)));

    tdb_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_get(kv, key, sizeof(key), &out));
    TEST_ASSERT_EQUAL_UINT32(sizeof(val), (uint32_t)out.len);
    TEST_ASSERT_EQUAL_MEMORY(val, out.data, sizeof(val));

    tdb_kv_close(kv);
}

static void test_syskeys_sort_before_user_keys(void) {
    uint8_t storage[16384];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const uint8_t sysk[] = {0x00, 's', 'y', 's'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, sysk, sizeof(sysk), "S", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, "user", 4, "U", 1));

    tdb_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_open_prefix(kv, NULL, 0, &cur));

    int has = 0;
    tdb_blob_t k = {0};

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(sizeof(sysk), (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY(sysk, k.data, sizeof(sysk));

    tdb_kv_cursor_close(cur);
    tdb_kv_close(kv);
}

static void test_syskey_prefix_iteration_only_returns_syskeys(void) {
    uint8_t storage[16384];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const uint8_t sys1[] = {0x00, 'a'};
    const uint8_t sys2[] = {0x00, 'b'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, sys1, sizeof(sys1), "1", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, sys2, sizeof(sys2), "2", 1));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, "user", 4, "U", 1));

    const uint8_t pfx[] = {0x00};
    tdb_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_open_prefix(kv, pfx, sizeof(pfx), &cur));

    int has = 0;
    tdb_blob_t k = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(sizeof(sys1), (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY(sys1, k.data, sizeof(sys1));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_UINT32(sizeof(sys2), (uint32_t)k.len);
    TEST_ASSERT_EQUAL_MEMORY(sys2, k.data, sizeof(sys2));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);

    tdb_kv_cursor_close(cur);
    tdb_kv_close(kv);
}

void tdb_register_tests_gc_syskeys(void) {
    RUN_TEST(test_syskey_roundtrip);
    RUN_TEST(test_syskeys_sort_before_user_keys);
    RUN_TEST(test_syskey_prefix_iteration_only_returns_syskeys);
}

