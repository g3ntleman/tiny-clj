// test_log_checkpoint.c - Append-only log + checkpoint recovery tests.

#include "unity.h"

#include "ft_log.h"

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
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    }
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

static void test_recover_last_checkpoint_happy_path(void) {
    uint8_t storage[2048];
    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};

    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage), .read_granularity = 1, .prog_granularity = 1, .erase_granularity = 16},
    };

    ft_log_t log;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_init(&log, &bdev));

    ft_log_checkpoint_t cp1 = {.seqno = 111, .root_off = 123};
    ft_log_checkpoint_t cp2 = {.seqno = 222, .root_off = 456};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_checkpoint(&log, cp1, NULL));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_checkpoint(&log, cp2, NULL));

    ft_log_checkpoint_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_recover_last_checkpoint(&bdev, &out));
    TEST_ASSERT_EQUAL_UINT64(cp2.seqno, out.seqno);
    TEST_ASSERT_EQUAL_UINT32(cp2.root_off, out.root_off);
}

static void test_recover_ignores_torn_last_record(void) {
    uint8_t storage[2048];
    ramdev_t rd = {.buf = storage, .len = sizeof(storage)};

    ft_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)sizeof(storage), .read_granularity = 1, .prog_granularity = 1, .erase_granularity = 16},
    };

    ft_log_t log;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_init(&log, &bdev));

    ft_log_checkpoint_t cp1 = {.seqno = 1, .root_off = 100};
    ft_log_checkpoint_t cp2 = {.seqno = 2, .root_off = 200};

    uint32_t cp1_off = 0;
    uint32_t cp2_off = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_checkpoint(&log, cp1, &cp1_off));
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_checkpoint(&log, cp2, &cp2_off));

    // Corrupt one byte inside cp2 payload to simulate a torn write / power loss.
    // We flip bits 1->0 (NOR semantics) which will invalidate CRC.
    const uint8_t zero = 0x00;
    // Corrupt the first payload byte (LSB of cp2.seqno), which is non-zero (2).
    const uint32_t hdr_size = (uint32_t)sizeof(uint32_t) /*magic*/
                            + (uint32_t)sizeof(uint16_t) /*version*/
                            + (uint32_t)sizeof(uint16_t) /*type*/
                            + (uint32_t)sizeof(uint32_t) /*payload_len*/
                            + (uint32_t)sizeof(uint64_t) /*seqno*/
                            + (uint32_t)sizeof(uint32_t) /*crc32*/;
    uint32_t payload_byte0 = cp2_off + hdr_size;
    TEST_ASSERT_EQUAL_INT(FT_OK, bdev.ops.prog(bdev.ctx, payload_byte0, &zero, 1));

    ft_log_checkpoint_t out = {0};
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_log_recover_last_checkpoint(&bdev, &out));
    TEST_ASSERT_EQUAL_UINT64(cp1.seqno, out.seqno);
    TEST_ASSERT_EQUAL_UINT32(cp1.root_off, out.root_off);
}

void ft_register_tests_log_checkpoint(void) {
    RUN_TEST(test_recover_last_checkpoint_happy_path);
    RUN_TEST(test_recover_ignores_torn_last_record);
}

