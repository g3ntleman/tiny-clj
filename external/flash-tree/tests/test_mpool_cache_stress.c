// test_mpool_cache_stress.c - Stress basic ops with varying cache sizes.

#include "unity.h"

#include "flash_tree.h"
#include "ft_blockdev.h"
#include "ft_kv_bind.h"

#define __DBINTERFACE_PRIVATE
#include "ft_bsd_db.h"
#include "ft_bsd_btree.h"
#include "ft_bsd_mpool.h"

#include <fcntl.h>
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
    bdev->geom.erase_granularity = 4096;
}

static void run_put_get_stress(uint32_t cache_pagecount) {
    const uint32_t old = ft_mpool_get_cache_pagecount();
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_enable_psram_autosize(0));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_set_cache_pagecount(cache_pagecount));

    static uint8_t storage[16 * 1024 * 1024];
    ramdev_t rd = {0};
    ft_blockdev_t bdev = {0};
    make_bdev(&bdev, &rd, storage, sizeof(storage));

    ft_kv_bind(&bdev, 0);

    BTREEINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.psize = 512;
    bi.cachesize = 2u * bi.psize;
    bi.minkeypage = 2;

    DB* db = __bt_open("mpool_cache_stress", O_RDWR | O_CREAT, 0600, &bi, 0);
    TEST_ASSERT_NOT_NULL(db);
    BTREE* t = (BTREE*)db->internal;
    TEST_ASSERT_NOT_NULL(t);

    uint32_t x = 0x2468ACE0u;
    uint8_t kbuf[16];
    uint8_t vbuf[16];
    memset(vbuf, 0xCC, sizeof(vbuf));
    DBT k = {.data = kbuf, .size = sizeof(kbuf)};
    DBT d = {.data = vbuf, .size = sizeof(vbuf)};

    for (int i = 0; i < 3000; i++) {
        x = x * 1664525u + 1013904223u;
        kbuf[0] = (uint8_t)(x & 0xFF);
        kbuf[1] = (uint8_t)((x >> 8) & 0xFF);
        kbuf[2] = (uint8_t)((x >> 16) & 0xFF);
        kbuf[3] = (uint8_t)((x >> 24) & 0xFF);
        memset(kbuf + 4, (uint8_t)(x & 0xFF), sizeof(kbuf) - 4);
        TEST_ASSERT_EQUAL_INT(RET_SUCCESS, db->put(db, &k, &d, 0));
    }

    /* Basic sanity: top-down insertion peak should stay low regardless of cache size. */
    TEST_ASSERT_TRUE(mpool_peak_pinned(t->bt_mp) <= 3);

    (void)db->close(db);
    ft_kv_unbind();

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_set_cache_pagecount(old));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_mpool_enable_psram_autosize(1));
}

static void test_cache_pagecount_3_stress(void) {
    run_put_get_stress(3);
}

static void test_cache_pagecount_16_stress(void) {
    run_put_get_stress(16);
}

void ft_register_tests_mpool_cache_stress(void) {
    RUN_TEST(test_cache_pagecount_3_stress);
    RUN_TEST(test_cache_pagecount_16_stress);
}

