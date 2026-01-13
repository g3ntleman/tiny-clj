// test_host_smoke.c - Minimal host smoke tests.

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
    memcpy(r->buf + addr, data, len);
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

static void test_db_init_deinit_smoke(void) {
    uint8_t storage[4096];
    ramdev_t rd = { .buf = storage, .len = sizeof(storage) };

    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage), .read_granularity = 1, .prog_granularity = 1, .erase_granularity = 16},
    };
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blockdev_validate(&bdev));

    ft_db_t* db = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_db_init(&db, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(db);
    ft_db_deinit(db);
}

void ft_register_tests_host_smoke(void) {
    RUN_TEST(test_db_init_deinit_smoke);
}

