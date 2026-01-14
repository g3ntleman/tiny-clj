// test_multi_kv.c - Two KV DBs at different start pages.

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

static void test_two_kv_dbs_on_different_pages_are_independent(void) {
    static uint8_t storage[256 * 1024];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};
    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage), .read_granularity = 1, .prog_granularity = 1, .erase_granularity = 4096},
    };
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blockdev_validate(&bdev));

    ft_kv_t* kv0 = NULL;
    ft_kv_t* kv1 = NULL;

    ft_kv_cfg_t cfg0 = {.start_page = FT_KV_ROOT_PAGE};
    ft_kv_cfg_t cfg1 = {.start_page = 16};  // 16 * 4096 = 65536

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv0, &bdev, &cfg0));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv1, &bdev, &cfg1));

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv0, "k", 1, "v0", 2));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_put(kv1, "k", 1, "v1", 2));

    ft_blob_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv0, "k", 1, &out));
    TEST_ASSERT_EQUAL_MEMORY("v0", out.data, 2);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv1, "k", 1, &out));
    TEST_ASSERT_EQUAL_MEMORY("v1", out.data, 2);

    ft_kv_close(kv0);
    ft_kv_close(kv1);

    // Reopen to verify persistence within each region.
    kv0 = NULL;
    kv1 = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv0, &bdev, &cfg0));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv1, &bdev, &cfg1));

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv0, "k", 1, &out));
    TEST_ASSERT_EQUAL_MEMORY("v0", out.data, 2);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_get(kv1, "k", 1, &out));
    TEST_ASSERT_EQUAL_MEMORY("v1", out.data, 2);

    ft_kv_close(kv0);
    ft_kv_close(kv1);
}

void ft_register_tests_multi_kv(void) {
    RUN_TEST(test_two_kv_dbs_on_different_pages_are_independent);
}

