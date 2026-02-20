// test_btree_topdown_insert.c - Top-down insertion tests (search+split only).

#include "unity.h"

#include "tdb_blockdev.h"
#include "tdb_kv_bind.h"

#define __DBINTERFACE_PRIVATE
#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h"
#include "tdb_bsd_mpool.h"

#include <fcntl.h>
#include <errno.h>
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
 * @brief topdown_put.
 * @param db B-Tree database handle.
 * @param key Key bytes.
 * @param data Value bytes.
 * @return RET_SUCCESS on success, RET_ERROR on failure.
 */
static int topdown_put(DB* db, DBT* key, DBT* data) {
    BTREE* t = (BTREE*)db->internal;
    const size_t nbytes = NBLEAFDBT(key->size, data->size);

    int exact = 0;
    EPG* e = __bt_search_insert(t, key, nbytes, &exact);
    if (!e)
        return RET_ERROR;
    PAGE* h = e->page;
    indx_t index = e->index;

    /* This helper is only used for no-dup tests. */
    if (exact && ISSET(t, B_NODUPS)) {
        mpool_put(t->bt_mp, h, 0);
        errno = EEXIST;
        return RET_SPECIAL;
    }

    if (__bt_would_split(h, nbytes)) {
        mpool_put(t->bt_mp, h, 0);
        errno = ENOMEM;
        return RET_ERROR;
    }

    indx_t nxtindex;
    if (index < (nxtindex = NEXTINDEX(h)))
        memmove(h->linp + index + 1, h->linp + index, (nxtindex - index) * sizeof(indx_t));
    h->lower += sizeof(indx_t);

    h->linp[index] = h->upper -= (indx_t)nbytes;
    char* dest = (char*)h + h->upper;
    WR_BLEAF(dest, key, data, 0);

    mpool_put(t->bt_mp, h, MPOOL_DIRTY);
    return RET_SUCCESS;
}

/**
 * @brief test_topdown_peak_pinned_is_3.
 */
static void test_topdown_peak_pinned_is_3(void) {
    static uint8_t storage[16 * 1024 * 1024];
    ramdev_t rd = {0};
    tdb_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    tdb_kv_bind(&bdev, 0);

    BTREEINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.psize = 512;
    bi.cachesize = 2u * bi.psize;
    bi.minkeypage = 2;
    bi.compare = __bt_defcmp;
    bi.prefix = NULL; /* simpler: no prefix compression in this test */

    DB* db = __bt_open("topdown_pins", O_RDWR | O_CREAT, 0600, &bi, 0);
    TEST_ASSERT_NOT_NULL(db);
    BTREE* t = (BTREE*)db->internal;
    TEST_ASSERT_NOT_NULL(t);

    uint32_t x = 0x13579BDFu;
    uint8_t kbuf[32];
    uint8_t vbuf[32];
    memset(vbuf, 0xA5, sizeof(vbuf));
    DBT k = {.data = kbuf, .size = sizeof(kbuf)};
    DBT d = {.data = vbuf, .size = sizeof(vbuf)};

    for (int i = 0; i < 4000; i++) {
        x = x * 1664525u + 1013904223u;
        kbuf[0] = (uint8_t)(x & 0xFF);
        kbuf[1] = (uint8_t)((x >> 8) & 0xFF);
        kbuf[2] = (uint8_t)((x >> 16) & 0xFF);
        kbuf[3] = (uint8_t)((x >> 24) & 0xFF);
        memset(kbuf + 4, (uint8_t)(x & 0xFF), sizeof(kbuf) - 4);
        TEST_ASSERT_EQUAL_INT(RET_SUCCESS, topdown_put(db, &k, &d));
    }

    MPOOL* mp = t->bt_mp;
    TEST_ASSERT_NOT_NULL(mp);
    TEST_ASSERT_TRUE_MESSAGE(mpool_peak_pinned(mp) <= 3, "expected top-down insert peak pinned pages <= 3");

    (void)db->close(db);
    tdb_kv_unbind();
}

/**
 * @brief tdb_register_tests_btree_topdown_insert.
 */
void tdb_register_tests_btree_topdown_insert(void) {
    RUN_TEST(test_topdown_peak_pinned_is_3);
}
