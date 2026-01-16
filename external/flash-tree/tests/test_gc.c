// test_gc.c - GC step guardrails (no compaction yet).

#include "unity.h"

#include "flash_tree.h"
#include "ft_blockdev.h"
#include "ft_kv_internal.h"

#define __DBINTERFACE_PRIVATE
#include "ft_bsd_mpool.h"

#include <stdlib.h>
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

static void test_gc_state_defaults_on_new_db(void) {
    static uint8_t storage[16384];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(kv);

    /* Defaults: cursor starts at 0, free list empty (PGNO_INVALID). */
    TEST_ASSERT_EQUAL_UINT32(0u, kv->gc_cursor);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PGNO_INVALID, kv->free_head);

    /* alloc_next is implementation-defined; it must be non-zero after btree init. */
    TEST_ASSERT_TRUE(kv->alloc_next >= 1u);

    ft_kv_close(kv);
}

static void test_gc_state_survives_close_open(void) {
    static uint8_t storage[16384];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(kv);

    kv->gc_cursor = 42;
    kv->free_head = 1234;
    kv->alloc_next = 5678;
    kv->gc_dirty = 1;

    ft_kv_close(kv);

    kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(kv);

    TEST_ASSERT_EQUAL_UINT32(42u, kv->gc_cursor);
    TEST_ASSERT_EQUAL_UINT32(1234u, kv->free_head);
    TEST_ASSERT_EQUAL_UINT32(5678u, kv->alloc_next);

    ft_kv_close(kv);
}

static void test_gc_allows_open_cursor_and_preserves_snapshot(void) {
    static uint8_t storage[16384];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "a", 1, "1", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "aa", 2, "2", 1));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "ab", 2, "3", 1));

    ft_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_open_prefix(kv, "a", 1, &cur));
    TEST_ASSERT_NOT_NULL(cur);

    /* GC must not disturb the active cursor. */
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_gc_step(kv, 0));

    int has = 0;
    ft_blob_t k = {0};

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("a", k.data, 1);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("aa", k.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_TRUE(has);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_key(cur, &k));
    TEST_ASSERT_EQUAL_MEMORY("ab", k.data, 2);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_cursor_next(cur, &has));
    TEST_ASSERT_FALSE(has);

    ft_kv_cursor_close(cur);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_gc_step(kv, 0));

    ft_kv_close(kv);
}

static void test_recovery_persists_across_reinit(void) {
    static uint8_t storage[16384];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "k", 1, "v", 1));
    ft_kv_close(kv);

    /* Re-open on the same backing store, without erasing. */
    kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv, "k", 1, &out));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)out.len);
    TEST_ASSERT_EQUAL_MEMORY("v", out.data, 1);
    ft_kv_close(kv);
}

static void test_gc_preserves_latest_value_after_many_updates(void) {
    static uint8_t storage[65536];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    /* Create garbage by rewriting the same key many times. */
    for (int i = 0; i < 200; i++) {
        char v = (char)('A' + (i % 26));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv, "k", 1, &v, 1));
    }

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_gc_step(kv, 0));

    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv, "k", 1, &out));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)out.len);
    /* Last written value (i=199). */
    char expect = (char)('A' + (199 % 26));
    TEST_ASSERT_EQUAL_MEMORY(&expect, out.data, 1);

    ft_kv_close(kv);
}

static uint32_t le_u32_read(const uint8_t* p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_gc_persists_periodically_during_gc_steps(void) {
    /* Must keep writes within the active half to satisfy mpool's GC invariants. */
    static uint8_t storage[1048576];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(kv);

    /* Force many distinct pages by writing a large blob (data pages use mpool_new). */
    /*
     * Create enough distinct data pages so incremental GC takes > 100 steps
     * with a tiny budget, ensuring periodic persistence triggers.
     */
    const size_t blob_len = 110u * 4080u;
    uint8_t* blob = (uint8_t*)malloc(blob_len);
    TEST_ASSERT_NOT_NULL(blob);
    for (size_t i = 0; i < blob_len; i++)
        blob[i] = (uint8_t)(i * 131u + 7u);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_put(kv, "B", 1, blob, blob_len));
    free(blob);

    /* Drive incremental GC so it takes many calls. */
    int saw_more_work = 0;
    for (int i = 0; i < 150; i++) {
        int rc = ft_kv_gc_step_more(kv, 1);
        TEST_ASSERT_TRUE(rc == 0 || rc == 1);
        if (rc == 1)
            saw_more_work = 1;
    }
    TEST_ASSERT_TRUE(saw_more_work);

    /* System-key must exist after periodic persist. */
    const uint8_t sys_gc_cursor[] = {0x00, 'g', 'c', '_', 'c', 'u', 'r', 's', 'o', 'r'};
    ft_blob_t out = {0};
    ft_status_t st = ft_kv_get(kv, sys_gc_cursor, sizeof(sys_gc_cursor), &out);
    TEST_ASSERT_EQUAL_INT(FT_OK, st);
    TEST_ASSERT_EQUAL_UINT32(4u, (uint32_t)out.len);
    const uint32_t persisted = le_u32_read((const uint8_t*)out.data);
    TEST_ASSERT_TRUE(persisted > 0u);

    ft_kv_close(kv);
}

static void test_free_list_reuses_tombstoned_pages_and_persists(void) {
#if FT_MPOOL_O1_RAM
    TEST_IGNORE_MESSAGE("FT_MPOOL_O1_RAM: simple mode does not reuse freed pgno numbers");
#endif
    static uint8_t storage[262144];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    MPOOL* mp = ft_kv_get_mpool(kv);
    TEST_ASSERT_NOT_NULL(mp);

    pgno_t p1 = PGNO_INVALID;
    void* page = mpool_new(mp, &p1);
    TEST_ASSERT_NOT_NULL(page);
    TEST_ASSERT_NOT_EQUAL(PGNO_INVALID, p1);
    memset(page, 0xCC, mp->pagesize);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, page, MPOOL_DIRTY));

    /* Free the pgno and expect next allocation to reuse it (LIFO). */
    TEST_ASSERT_EQUAL_INT(0, mpool_free_pgno(mp, p1));

    pgno_t p2 = PGNO_INVALID;
    void* page2 = mpool_new(mp, &p2);
    TEST_ASSERT_NOT_NULL(page2);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)p1, (uint32_t)p2);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, page2, MPOOL_DIRTY));

    /* Free again and ensure it survives close/open (persisted head). */
    TEST_ASSERT_EQUAL_INT(0, mpool_free_pgno(mp, p2));
    ft_kv_close(kv);

    kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    mp = ft_kv_get_mpool(kv);
    TEST_ASSERT_NOT_NULL(mp);

    pgno_t p3 = PGNO_INVALID;
    void* page3 = mpool_new(mp, &p3);
    TEST_ASSERT_NOT_NULL(page3);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)p1, (uint32_t)p3);
    TEST_ASSERT_EQUAL_INT(0, mpool_put(mp, page3, MPOOL_DIRTY));

    ft_kv_close(kv);
}

void ft_register_tests_gc(void) {
    RUN_TEST(test_gc_state_defaults_on_new_db);
    RUN_TEST(test_gc_state_survives_close_open);
    RUN_TEST(test_gc_allows_open_cursor_and_preserves_snapshot);
    RUN_TEST(test_recovery_persists_across_reinit);
    RUN_TEST(test_gc_preserves_latest_value_after_many_updates);
    RUN_TEST(test_gc_persists_periodically_during_gc_steps);
    RUN_TEST(test_free_list_reuses_tombstoned_pages_and_persists);
}
