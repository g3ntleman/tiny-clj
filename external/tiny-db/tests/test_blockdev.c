// test_blockdev.c - Blockdev interface tests (RAM backend).

#include "unity.h"

#include "tdb_blockdev.h"

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

// Simulate NOR flash programming: bits can only go from 1 -> 0.
static tdb_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    }
    return TDB_OK;
}

static tdb_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return TDB_OK;
}

static tdb_blockdev_t make_ram_bdev(uint8_t* storage, size_t storage_len, uint32_t read_g,
                                   uint32_t prog_g, uint32_t erase_g) {
    ramdev_t* ctx = (ramdev_t*)malloc(sizeof(ramdev_t));
    ctx->buf = storage;
    ctx->len = storage_len;

    tdb_blockdev_t bdev = {
        .ctx = ctx,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)storage_len,
                 .read_granularity = read_g,
                 .prog_granularity = prog_g,
                 .erase_granularity = erase_g},
    };
    return bdev;
}

static void free_ram_bdev(tdb_blockdev_t* bdev) {
    if (bdev && bdev->ctx) {
        free(bdev->ctx);
        bdev->ctx = NULL;
    }
}

static void test_erase_sets_ff(void) {
    uint8_t storage[128];
    memset(storage, 0x00, sizeof(storage));
    tdb_blockdev_t bdev = make_ram_bdev(storage, sizeof(storage), 1, 1, 16);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_validate(&bdev));

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_erase(&bdev, 0, 32));
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, storage[i]);
    }

    free_ram_bdev(&bdev);
}

static void test_prog_is_one_to_zero_only(void) {
    uint8_t storage[64];
    memset(storage, 0xFF, sizeof(storage));
    tdb_blockdev_t bdev = make_ram_bdev(storage, sizeof(storage), 1, 1, 16);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_validate(&bdev));

    const uint8_t a[4] = {0x0F, 0xF0, 0xAA, 0x55};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_prog(&bdev, 0, a, sizeof(a)));
    TEST_ASSERT_EQUAL_HEX8(0x0F, storage[0]);

    // Attempt to "set" bits back to 1 should not work: AND keeps 0 bits.
    const uint8_t b[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_prog(&bdev, 0, b, sizeof(b)));
    TEST_ASSERT_EQUAL_HEX8(0x0F, storage[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF0, storage[1]);

    free_ram_bdev(&bdev);
}

static void test_granularity_enforced(void) {
    uint8_t storage[64];
    memset(storage, 0xFF, sizeof(storage));
    tdb_blockdev_t bdev = make_ram_bdev(storage, sizeof(storage), 4, 4, 16);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_validate(&bdev));

    uint8_t tmp[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_blockdev_prog(&bdev, 2, tmp, 4));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_blockdev_prog(&bdev, 4, tmp, 2));
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG, tdb_blockdev_erase(&bdev, 0, 8));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_erase(&bdev, 0, 16));

    free_ram_bdev(&bdev);
}

void tdb_register_tests_blockdev(void) {
    RUN_TEST(test_erase_sets_ff);
    RUN_TEST(test_prog_is_one_to_zero_only);
    RUN_TEST(test_granularity_enforced);
}
