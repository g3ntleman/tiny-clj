// test_btree_split_child.c - Unit tests for __bt_split_child().

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

static int tree_height(BTREE* t) {
    int h = 0;
    pgno_t pg = P_ROOT;
    while (1) {
        PAGE* p = mpool_get(t->bt_mp, pg, 0);
        if (!p)
            return -1;
        h++;
        if (p->flags & P_BLEAF) {
            (void)mpool_put(t->bt_mp, p, 0);
            return h;
        }
        pg = GETBINTERNAL(p, 0)->pgno;
        (void)mpool_put(t->bt_mp, p, 0);
    }
}

static void fill_until_height(DB* db, BTREE* t, int want_height) {
    uint32_t x = 0xC0FFEEu;
    uint8_t kbuf[32];
    uint8_t vbuf[32];
    memset(vbuf, 0x5A, sizeof(vbuf));
    DBT k = {.data = kbuf, .size = sizeof(kbuf)};
    DBT d = {.data = vbuf, .size = sizeof(vbuf)};
    for (int i = 0; i < 10000; i++) {
        if (tree_height(t) >= want_height)
            return;
        x = x * 1664525u + 1013904223u;
        kbuf[0] = (uint8_t)(x & 0xFF);
        kbuf[1] = (uint8_t)((x >> 8) & 0xFF);
        kbuf[2] = (uint8_t)((x >> 16) & 0xFF);
        kbuf[3] = (uint8_t)((x >> 24) & 0xFF);
        memset(kbuf + 4, (uint8_t)(x & 0xFF), sizeof(kbuf) - 4);
        TEST_ASSERT_EQUAL_INT(RET_SUCCESS, db->put(db, &k, &d, 0));
    }
    TEST_FAIL_MESSAGE("failed to reach requested tree height");
}

static int try_find_split_pair(BTREE* t, int want_leaf_child, PAGE** out_parent, indx_t* out_parent_index,
                               PAGE** out_child) {
    if (out_parent)
        *out_parent = NULL;
    if (out_parent_index)
        *out_parent_index = 0;
    if (out_child)
        *out_child = NULL;
    if (!t || !out_parent || !out_parent_index || !out_child)
        return 0;

    const size_t need_parent_bytes = NBINTERNAL(32);

    pgno_t pg = P_ROOT;
    for (int depth = 0; depth < 16; depth++) {
        PAGE* parent = mpool_get(t->bt_mp, pg, 0);
        if (!parent)
            return 0;
        if (parent->flags & P_BLEAF) {
            (void)mpool_put(t->bt_mp, parent, 0);
            return 0;
        }
        if ((parent->flags & P_TYPE) != P_BINTERNAL) {
            (void)mpool_put(t->bt_mp, parent, 0);
            return 0;
        }

        /* Only split if parent has room (top-down precondition). */
        if (__bt_would_split(parent, need_parent_bytes)) {
            pg = GETBINTERNAL(parent, 0)->pgno;
            (void)mpool_put(t->bt_mp, parent, 0);
            continue;
        }

        const indx_t n = NEXTINDEX(parent);
        if (n == 0) {
            (void)mpool_put(t->bt_mp, parent, 0);
            return 0;
        }
        const indx_t parent_index = want_leaf_child ? (n - 1) : 0;

        pgno_t child_pgno = GETBINTERNAL(parent, parent_index)->pgno;
        PAGE* child = mpool_get(t->bt_mp, child_pgno, 0);
        if (!child) {
            (void)mpool_put(t->bt_mp, parent, 0);
            return 0;
        }

        const int child_is_leaf = (child->flags & P_BLEAF) != 0;
        const int child_ok = want_leaf_child ? child_is_leaf : ((child->flags & P_BINTERNAL) != 0);
        const int rightmost_leaf_ok = want_leaf_child ? (child->nextpg == P_INVALID) : 1;
        if (child_ok && rightmost_leaf_ok && NEXTINDEX(child) >= 4) {
            *out_parent = parent;
            *out_child = child;
            *out_parent_index = parent_index;
            return 1;
        }

        (void)mpool_put(t->bt_mp, child, 0);
        pg = child_pgno;
        (void)mpool_put(t->bt_mp, parent, 0);
    }
    return 0;
}

static void test_bt_split_child_leaf(void) {
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
    bi.prefix = NULL; /* simplify: avoid prefix compression in this test */

    DB* db = __bt_open("split_child_leaf", O_RDWR | O_CREAT, 0600, &bi, 0);
    TEST_ASSERT_NOT_NULL(db);

    BTREE* t = (BTREE*)db->internal;
    TEST_ASSERT_NOT_NULL(t);

    fill_until_height(db, t, 2);

    PAGE* parent = NULL;
    PAGE* child = NULL;
    indx_t parent_index = 0;
    uint32_t x = 0xBADC0DEu;
    uint8_t kbuf[32];
    uint8_t vbuf[32];
    memset(vbuf, 0x5A, sizeof(vbuf));
    DBT k = {.data = kbuf, .size = sizeof(kbuf)};
    DBT d = {.data = vbuf, .size = sizeof(vbuf)};
    for (int i = 0; i < 8000; i++) {
        if (try_find_split_pair(t, 1, &parent, &parent_index, &child))
            break;
        x = x * 1664525u + 1013904223u;
        kbuf[0] = (uint8_t)(x & 0xFF);
        kbuf[1] = (uint8_t)((x >> 8) & 0xFF);
        kbuf[2] = (uint8_t)((x >> 16) & 0xFF);
        kbuf[3] = (uint8_t)((x >> 24) & 0xFF);
        memset(kbuf + 4, (uint8_t)(x & 0xFF), sizeof(kbuf) - 4);
        TEST_ASSERT_EQUAL_INT(RET_SUCCESS, db->put(db, &k, &d, 0));
    }
    TEST_ASSERT_NOT_NULL(parent);
    TEST_ASSERT_NOT_NULL(child);

    const indx_t before = NEXTINDEX(parent);
    pgno_t right_pgno = PGNO_INVALID;
    TEST_ASSERT_EQUAL_INT(RET_SUCCESS, __bt_split_child(t, parent, parent_index, child, &right_pgno));
    TEST_ASSERT_NOT_EQUAL_UINT32(PGNO_INVALID, (uint32_t)right_pgno);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(before + 1), (uint32_t)NEXTINDEX(parent));

    /* Mark modified pages dirty and release pins. */
    TEST_ASSERT_EQUAL_INT(0, mpool_put(t->bt_mp, child, MPOOL_DIRTY));
    TEST_ASSERT_EQUAL_INT(0, mpool_put(t->bt_mp, parent, MPOOL_DIRTY));

    (void)db->close(db);
    tdb_kv_unbind();
}

static void test_bt_split_child_internal(void) {
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
    bi.prefix = NULL;

    DB* db = __bt_open("split_child_internal", O_RDWR | O_CREAT, 0600, &bi, 0);
    TEST_ASSERT_NOT_NULL(db);

    BTREE* t = (BTREE*)db->internal;
    TEST_ASSERT_NOT_NULL(t);

    fill_until_height(db, t, 3);

    PAGE* parent = NULL;
    PAGE* child = NULL;
    indx_t parent_index = 0;
    uint32_t x = 0xFEEDFACEu;
    uint8_t kbuf[32];
    uint8_t vbuf[32];
    memset(vbuf, 0x5A, sizeof(vbuf));
    DBT k = {.data = kbuf, .size = sizeof(kbuf)};
    DBT d = {.data = vbuf, .size = sizeof(vbuf)};
    for (int i = 0; i < 40000; i++) {
        if (try_find_split_pair(t, 0, &parent, &parent_index, &child))
            break;
        x = x * 1664525u + 1013904223u;
        kbuf[0] = (uint8_t)(x & 0xFF);
        kbuf[1] = (uint8_t)((x >> 8) & 0xFF);
        kbuf[2] = (uint8_t)((x >> 16) & 0xFF);
        kbuf[3] = (uint8_t)((x >> 24) & 0xFF);
        memset(kbuf + 4, (uint8_t)(x & 0xFF), sizeof(kbuf) - 4);
        TEST_ASSERT_EQUAL_INT(RET_SUCCESS, db->put(db, &k, &d, 0));
    }
    TEST_ASSERT_NOT_NULL(parent);
    TEST_ASSERT_NOT_NULL(child);

    const indx_t before = NEXTINDEX(parent);
    pgno_t right_pgno = PGNO_INVALID;
    TEST_ASSERT_EQUAL_INT(RET_SUCCESS, __bt_split_child(t, parent, parent_index, child, &right_pgno));
    TEST_ASSERT_NOT_EQUAL_UINT32(PGNO_INVALID, (uint32_t)right_pgno);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(before + 1), (uint32_t)NEXTINDEX(parent));

    TEST_ASSERT_EQUAL_INT(0, mpool_put(t->bt_mp, child, MPOOL_DIRTY));
    TEST_ASSERT_EQUAL_INT(0, mpool_put(t->bt_mp, parent, MPOOL_DIRTY));

    (void)db->close(db);
    tdb_kv_unbind();
}

void tdb_register_tests_btree_split_child(void) {
    RUN_TEST(test_bt_split_child_leaf);
    RUN_TEST(test_bt_split_child_internal);
}

