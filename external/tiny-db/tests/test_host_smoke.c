// test_host_smoke.c - Minimal host smoke tests.

#include "unity.h"

#include "tiny_db.h"
#include "tdb_blockdev.h"

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
    /* Flash semantics: can only clear bits (1→0), not set them */
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

static void test_db_init_deinit_smoke(void) {
    static uint8_t storage[16384];          /* Larger for GC ping-pong */
    memset(storage, 0xFF, sizeof(storage)); /* Fresh flash starts erased */
    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};

    tdb_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage),
                 .read_granularity = 1,
                 .prog_granularity = 1,
                 .erase_granularity = 4096},
    };
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_validate(&bdev));

    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(kv);
    tdb_kv_close(kv);
}

void tdb_register_tests_host_smoke(void) {
    RUN_TEST(test_db_init_deinit_smoke);
}
