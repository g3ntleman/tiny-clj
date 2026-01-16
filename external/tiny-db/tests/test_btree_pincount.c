// test_btree_pincount.c - Verify mpool peak pinned pages for top-down split.

#include "unity.h"

#include "tdb_blockdev.h"
#include "tdb_kv_bind.h"

#define __DBINTERFACE_PRIVATE
#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"
#include "tdb_bsd_mpool.h"

#include <fcntl.h>
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

static void test_btree_peak_pinned_pages_current(void) {
    static uint8_t storage[8 * 1024 * 1024];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_bind(&bdev, 0);

    BTREEINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.psize = 512;               /* Minimum supported by BSD btree. */
    bi.cachesize = 2u * bi.psize; /* Not used by mpool, keep minimal. */
    bi.minkeypage = 2;

    DB* db = __bt_open("pincount", O_RDWR | O_CREAT, 0600, &bi, 0);
    TEST_ASSERT_NOT_NULL(db);

    BTREE* t = (BTREE*)db->internal;
    TEST_ASSERT_NOT_NULL(t);
    MPOOL* mp = t->bt_mp;
    TEST_ASSERT_NOT_NULL(mp);

    uint32_t x = 0x12345678u;
    uint8_t kbuf[32];
    uint8_t vbuf[32];
    DBT k = {.data = kbuf, .size = sizeof(kbuf)};
    DBT d = {.data = vbuf, .size = sizeof(vbuf)};

    memset(vbuf, 0xA5, sizeof(vbuf));

    /* Pseudo-random inserts to avoid sorted-data split fast-path. */
    for (int i = 0; i < 8000; i++) {
        x = x * 1664525u + 1013904223u;
        kbuf[0] = (uint8_t)(x & 0xFF);
        kbuf[1] = (uint8_t)((x >> 8) & 0xFF);
        kbuf[2] = (uint8_t)((x >> 16) & 0xFF);
        kbuf[3] = (uint8_t)((x >> 24) & 0xFF);
        memset(kbuf + 4, (uint8_t)(x & 0xFF), sizeof(kbuf) - 4);
        int rc = db->put(db, &k, &d, 0);
        TEST_ASSERT_EQUAL_INT(RET_SUCCESS, rc);
        if (mpool_peak_pinned(mp) >= 3)
            break;
    }

    const uint32_t peak = mpool_peak_pinned(mp);
    TEST_ASSERT_TRUE_MESSAGE(peak <= 3, "expected top-down split peak <= 3 pinned pages");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3u, peak, "expected to observe peak pinned pages == 3 in top-down split");

    (void)db->close(db);
    tdb_kv_unbind();
}

void tdb_register_tests_btree_pincount(void) {
    RUN_TEST(test_btree_peak_pinned_pages_current);
}

