// test_mpool_o1ram.c - Tests for TDB_MPOOL_O1_RAM (scan-on-demand mapping).

#include "unity.h"

#include "tdb_blockdev.h"
#include "tdb_kv_bind.h"

#define __DBINTERFACE_PRIVATE
#include "tdb_bsd_mpool.h"

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

/**
 * @brief test_o1ram_scan_miss_roundtrip_two_pages.
 */
static void test_o1ram_scan_miss_roundtrip_two_pages(void) {
    static uint8_t storage[131072];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_bind(&bdev, 0);
    MPOOL* mp = mpool_open(NULL, 0, 4080, 0);
    TEST_ASSERT_NOT_NULL(mp);

    /* Write two pages so a tiny cache (entries=1) will miss for at least one. */
    pgno_t p1 = PGNO_INVALID;
    uint8_t* a = (uint8_t*)mpool_new(mp, &p1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_EQUAL_UINT32(PGNO_INVALID, (uint32_t)p1);
    memset(a, 0x11, mp->pagesize);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, a, MPOOL_DIRTY));

    pgno_t p2 = PGNO_INVALID;
    uint8_t* b = (uint8_t*)mpool_new(mp, &p2);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL_UINT32(PGNO_INVALID, (uint32_t)p2);
    memset(b, 0x22, mp->pagesize);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, b, MPOOL_DIRTY));

    TEST_ASSERT_EQUAL_INT(0, mpool_sync(mp));
    TEST_ASSERT_EQUAL_INT(0, mpool_close(mp));
    tdb_kv_unbind();

    /* Re-open: force lookup via scan-on-demand. */
    tdb_kv_bind(&bdev, 0);
    mp = mpool_open(NULL, 0, 4080, 0);
    TEST_ASSERT_NOT_NULL(mp);

    uint8_t* r1 = (uint8_t*)mpool_get(mp, p1, 0);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_UINT8(0x11, r1[0]);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, r1, 0));

    uint8_t* r2 = (uint8_t*)mpool_get(mp, p2, 0);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_UINT8(0x22, r2[0]);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, r2, 0));

    TEST_ASSERT_EQUAL_INT(0, mpool_close(mp));
    tdb_kv_unbind();
}

/**
 * @brief test_o1ram_tombstone_makes_pgno_unreadable_after_reopen.
 */
static void test_o1ram_tombstone_makes_pgno_unreadable_after_reopen(void) {
    static uint8_t storage[131072];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_bind(&bdev, 0);
    MPOOL* mp = mpool_open(NULL, 0, 4080, 0);
    TEST_ASSERT_NOT_NULL(mp);

    pgno_t p = PGNO_INVALID;
    uint8_t* page = (uint8_t*)mpool_new(mp, &p);
    TEST_ASSERT_NOT_NULL(page);
    memset(page, 0x33, mp->pagesize);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, page, MPOOL_DIRTY));
    TEST_ASSERT_EQUAL_INT(0, mpool_sync(mp));

    TEST_ASSERT_EQUAL_INT(0, mpool_free_pgno(mp, p));
    TEST_ASSERT_EQUAL_INT(0, mpool_close(mp));
    tdb_kv_unbind();

    tdb_kv_bind(&bdev, 0);
    mp = mpool_open(NULL, 0, 4080, 0);
    TEST_ASSERT_NOT_NULL(mp);

    TEST_ASSERT_NULL(mpool_get(mp, p, 0));
    TEST_ASSERT_EQUAL_INT(0, mpool_close(mp));
    tdb_kv_unbind();
}

/**
 * @brief test_o1ram_recovery_stops_at_torn_last_record.
 */
static void test_o1ram_recovery_stops_at_torn_last_record(void) {
    static uint8_t storage[131072];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_bind(&bdev, 0);
    MPOOL* mp = mpool_open(NULL, 0, 4080, 0);
    TEST_ASSERT_NOT_NULL(mp);

    pgno_t p = PGNO_INVALID;
    uint8_t* page = (uint8_t*)mpool_new(mp, &p);
    TEST_ASSERT_NOT_NULL(page);
    memset(page, 0x44, mp->pagesize);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, page, MPOOL_DIRTY));
    TEST_ASSERT_EQUAL_INT(0, mpool_sync(mp));

    const uint32_t good_write_off = mp->write_off;
    TEST_ASSERT_TRUE(good_write_off > 0);

    TEST_ASSERT_EQUAL_INT(0, mpool_close(mp));
    tdb_kv_unbind();

    /* Inject a torn record at good_write_off (non-0xFF, magic set, CRC wrong). */
    TEST_ASSERT_TRUE((size_t)good_write_off + 16 < sizeof(storage));
    storage[good_write_off + 0] = 'F';
    storage[good_write_off + 1] = 'T';
    storage[good_write_off + 2] = 'G';
    storage[good_write_off + 3] = 'P';
    /* Leave the rest as-is (will not match CRC). */

    tdb_kv_bind(&bdev, 0);
    mp = mpool_open(NULL, 0, 4080, 0);
    TEST_ASSERT_NOT_NULL(mp);

    TEST_ASSERT_EQUAL_UINT32(good_write_off, mp->write_off);
    TEST_ASSERT_EQUAL_INT(0, mpool_close(mp));
    tdb_kv_unbind();
}

/**
 * @brief test_o1ram_is_enabled_for_dogfood_build.
 */
static void test_o1ram_is_enabled_for_dogfood_build(void) {
    /* This test just ensures the file is compiled+run in the dogfood build. */
    TEST_ASSERT_TRUE(TDB_MPOOL_O1_RAM == 1);
}

/**
 * @brief tdb_register_tests_mpool_o1ram.
 */
void tdb_register_tests_mpool_o1ram(void) {
    RUN_TEST(test_o1ram_is_enabled_for_dogfood_build);
#if TDB_MPOOL_O1_RAM
    RUN_TEST(test_o1ram_scan_miss_roundtrip_two_pages);
    RUN_TEST(test_o1ram_tombstone_makes_pgno_unreadable_after_reopen);
    RUN_TEST(test_o1ram_recovery_stops_at_torn_last_record);
#endif
}

