// test_blob.c - Tests for blob values (Meta-Key + Index-Keys + Data-Pages).

#include "unity.h"

#include "flash_tree.h"
#include "ft_blockdev.h"
#include "ft_crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Internal format helpers live in src/. */
#include "ft_blob.h"

typedef struct {
    uint8_t* buf;
    size_t len;
    uint64_t read_calls;
    uint64_t prog_calls;
    uint64_t erase_calls;
    uint64_t read_bytes;
    uint64_t prog_bytes;
    uint64_t erase_bytes;
} ramdev_t;

static ft_status_t ram_read(void* ctx, uint32_t addr, void* out, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    r->read_calls++;
    r->read_bytes += len;
    memcpy(out, r->buf + addr, len);
    return FT_OK;
}

static ft_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    r->prog_calls++;
    r->prog_bytes += len;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]); /* 1->0 only */
    }
    return FT_OK;
}

static ft_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return FT_ERR_IO;
    r->erase_calls++;
    r->erase_bytes += len;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

static ft_blockdev_t make_ram_bdev(ramdev_t* ctx, uint8_t* storage, size_t storage_len,
                                   uint32_t read_g, uint32_t prog_g, uint32_t erase_g) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->buf = storage;
    ctx->len = storage_len;
    ft_blockdev_t bdev = {
        .ctx = ctx,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)storage_len,
                 .read_granularity = read_g,
                 .prog_granularity = prog_g,
                 .erase_granularity = erase_g},
    };
    return bdev;
}

static void test_blobdesc_roundtrip(void) {
    uint8_t buf[128] = {0};
    uint32_t inline_pgnos[2] = {123u, 456u};

    ft_blob_desc_t d = {
        .version = (uint8_t)FT_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = 4080,
        .logical_size = 123456,
        .generation = 7,
        .index_block_count = 3,
        .inline_pgno_count = 2,
    };

    size_t enc_len = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK,
                          ft_blob_desc_encode(buf, sizeof(buf), &d, inline_pgnos, 2, &enc_len));
    TEST_ASSERT_EQUAL_UINT(FT_BLOB_DESC_HDR_SIZE + 8u, (unsigned)enc_len);

    ft_blob_desc_t out = {0};
    uint32_t out_pgnos[2] = {0};
    uint16_t cap = 2;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_desc_decode(buf, enc_len, &out, out_pgnos, &cap));
    TEST_ASSERT_EQUAL_UINT8(FT_BLOB_DESC_VERSION, out.version);
    TEST_ASSERT_EQUAL_UINT16(4080, out.chunk_size);
    TEST_ASSERT_EQUAL_UINT32(123456u, out.logical_size);
    TEST_ASSERT_EQUAL_UINT32(7u, out.generation);
    TEST_ASSERT_EQUAL_UINT16(3u, out.index_block_count);
    TEST_ASSERT_EQUAL_UINT16(2u, out.inline_pgno_count);
    TEST_ASSERT_EQUAL_UINT16(2u, cap);
    TEST_ASSERT_EQUAL_UINT32(123u, out_pgnos[0]);
    TEST_ASSERT_EQUAL_UINT32(456u, out_pgnos[1]);
}

static void test_blobdesc_decode_bounds_reports_needed(void) {
    uint8_t buf[128] = {0};
    uint32_t inline_pgnos[3] = {1u, 2u, 3u};

    ft_blob_desc_t d = {
        .version = (uint8_t)FT_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = 4080,
        .logical_size = 1,
        .generation = 1,
        .index_block_count = 0,
        .inline_pgno_count = 3,
    };
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK,
                          ft_blob_desc_encode(buf, sizeof(buf), &d, inline_pgnos, 3, &enc_len));

    ft_blob_desc_t out = {0};
    uint32_t out_pgnos[2] = {0};
    uint16_t cap = 2;
    TEST_ASSERT_EQUAL_INT(FT_ERR_INVALID_ARG,
                          ft_blob_desc_decode(buf, enc_len, &out, out_pgnos, &cap));
    TEST_ASSERT_EQUAL_UINT16(3u, cap); /* required */
}

static void test_indexblock_roundtrip(void) {
    uint8_t buf[128] = {0};
    uint32_t pgnos[3] = {11u, 22u, 33u};
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_index_block_encode(buf, sizeof(buf), pgnos, 3, &enc_len));
    TEST_ASSERT_EQUAL_UINT(FT_INDEX_BLOCK_HDR_SIZE + 12u, (unsigned)enc_len);

    uint32_t out_pgnos[3] = {0};
    uint16_t cap = 3;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_index_block_decode(buf, enc_len, out_pgnos, &cap));
    TEST_ASSERT_EQUAL_UINT16(3u, cap);
    TEST_ASSERT_EQUAL_UINT32(11u, out_pgnos[0]);
    TEST_ASSERT_EQUAL_UINT32(22u, out_pgnos[1]);
    TEST_ASSERT_EQUAL_UINT32(33u, out_pgnos[2]);
}

static uint8_t payload_byte(uint32_t idx, uint32_t off) {
    uint32_t x = idx * 0x9E3779B1u ^ off * 0x85EBCA6Bu ^ 0xC001D00Du;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (uint8_t)(x >> 24);
}

static void fill_payload(uint8_t* dst, uint32_t idx, size_t n) {
    for (size_t k = 0; k < n; k++)
        dst[k] = payload_byte(idx, (uint32_t)k);
}

static void test_blob_put_and_readback_64k(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    ft_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:test1";
    const size_t total = 64u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    uint8_t* dst = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    TEST_ASSERT_NOT_NULL(dst);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i * 131u + 7u);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    size_t got_len = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_get_len(kv, user_key, sizeof(user_key) - 1u, &got_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)got_len);

    size_t saved_len = 0;
    memset(dst, 0, total);
    TEST_ASSERT_EQUAL_INT(
        FT_OK, ft_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, total);

    free(dst);
    free(src);
    ft_kv_close(kv);
}

static void test_blob_truncate_smaller(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    ft_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:test2";
    const size_t total = 64u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i ^ 0xA5u);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    const size_t new_size = 20000u;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_truncate(kv, user_key, sizeof(user_key) - 1u, new_size));

    size_t got_len = 0;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_get_len(kv, user_key, sizeof(user_key) - 1u, &got_len));
    TEST_ASSERT_EQUAL_UINT(new_size, (unsigned)got_len);

    uint8_t* dst = (uint8_t*)malloc(new_size);
    TEST_ASSERT_NOT_NULL(dst);
    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        FT_OK, ft_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, new_size, &saved_len));
    TEST_ASSERT_EQUAL_UINT(new_size, (unsigned)saved_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, new_size);

    free(dst);
    free(src);
    ft_kv_close(kv);
}

static void test_blob_range_write(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    ft_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:test3";
    const size_t total = 32u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    uint8_t* dst = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    TEST_ASSERT_NOT_NULL(dst);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i * 3u + 1u);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    const size_t off = 1000u;
    const size_t n = 2000u;
    uint8_t* patch = (uint8_t*)malloc(n);
    TEST_ASSERT_NOT_NULL(patch);
    fill_payload(patch, 42u, n);
    TEST_ASSERT_EQUAL_INT(FT_OK,
                          ft_blob_write_range(kv, user_key, sizeof(user_key) - 1u, off, patch, n));

    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        FT_OK, ft_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);

    /* Verify patch applied. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, off);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(patch, dst + off, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src + off + n, dst + off + n, total - off - n);

    free(patch);
    free(dst);
    free(src);
    ft_kv_close(kv);
}

static void test_blob_writer_multi_step_and_abort(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    ft_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    const char key_ok[] = "blob:writer:ok";
    const char key_abort[] = "blob:writer:abort";

    /* Multi-step writer should equal ft_blob_put. */
    {
        const size_t total = 8192;
        uint8_t src[8192];
        for (size_t i = 0; i < total; i++)
            src[i] = (uint8_t)(i * 17u + 3u);

        ft_blob_writer_t* w = NULL;
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_writer_init(kv, key_ok, sizeof(key_ok) - 1u, &w));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_write(w, src, 1000));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_write(w, src + 1000, 5000));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_write(w, src + 6000, total - 6000));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_finish(w));

        uint8_t dst[8192];
        size_t saved_len = 0;
        TEST_ASSERT_EQUAL_INT(
            FT_OK, ft_blob_get_into(kv, key_ok, sizeof(key_ok) - 1u, dst, sizeof(dst), &saved_len));
        TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, total);
    }

    /* Abort must not create a committed blob (meta key never written). */
    {
        ft_blob_writer_t* w = NULL;
        TEST_ASSERT_EQUAL_INT(FT_OK,
                              ft_blob_writer_init(kv, key_abort, sizeof(key_abort) - 1u, &w));
        uint8_t tmp[123];
        memset(tmp, 0xAB, sizeof(tmp));
        TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_write(w, tmp, sizeof(tmp)));
        ft_blob_abort(w);

        size_t got_len = 0;
        TEST_ASSERT_EQUAL_INT(FT_ERR_NOT_FOUND,
                              ft_blob_get_len(kv, key_abort, sizeof(key_abort) - 1u, &got_len));
    }

    ft_kv_close(kv);
}

static void test_blob_gc_and_recovery_after_updates(void) {
    static uint8_t storage[4096 * 1024]; /* 4 MiB */
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    ft_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:gc:recover";
    const size_t total = 128u * 1024u;
    uint8_t* expected = (uint8_t*)malloc(total);
    uint8_t* tmp = (uint8_t*)malloc(2048);
    TEST_ASSERT_NOT_NULL(expected);
    TEST_ASSERT_NOT_NULL(tmp);

    for (size_t i = 0; i < total; i++)
        expected[i] = (uint8_t)(i * 5u + 11u);
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_put(kv, user_key, sizeof(user_key) - 1u, expected, total));

    /* Apply a handful of updates and mirror them in expected[]. */
    for (uint32_t i = 0; i < 20; i++) {
        const size_t off = (size_t)(i * 997u) % (total - 2048u);
        fill_payload(tmp, i + 1u, 2048);
        memcpy(expected + off, tmp, 2048);
        TEST_ASSERT_EQUAL_INT(
            FT_OK, ft_blob_write_range(kv, user_key, sizeof(user_key) - 1u, off, tmp, 2048));
    }
    free(tmp);

    /* Run incremental GC until complete. */
    int more = 1;
    int iters = 0;
    while (more == 1) {
        more = ft_kv_gc_step_more(kv, 4096 * 8);
        TEST_ASSERT_TRUE_MESSAGE(more == 0 || more == 1, "gc_step_more returned error");
        iters++;
        TEST_ASSERT_LESS_THAN_INT_MESSAGE(20000, iters, "gc loop too long");
    }

    ft_kv_close(kv);

    /* Reopen and verify recovery + blob integrity. */
    kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));
    uint8_t* got = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(got);
    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        FT_OK, ft_blob_get_into(kv, user_key, sizeof(user_key) - 1u, got, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, got, total);
    free(got);
    free(expected);

    ft_kv_close(kv);
}

static void test_blob_stream_1mib_gated(void) {
    const char* stress = getenv("FLASH_TREE_STRESS");
    if (!stress || stress[0] == '\0') {
        TEST_IGNORE();
        return;
    }

    static uint8_t storage[4096 * 1024]; /* 4 MiB */
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    ft_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    ft_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(FT_OK, ft_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:stress1m";
    const size_t total = 1024u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    for (uint32_t i = 0; i < (uint32_t)total; i++)
        src[i] = payload_byte(i / 4096u, i % 4096u);

    TEST_ASSERT_EQUAL_INT(FT_OK, ft_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    uint32_t expected_crc = ft_crc32_ieee(src, total, 0);
    free(src);

    /* For now, use get_into for CRC path (still exercises streaming internally). */
    uint8_t* dst = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(dst);
    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        FT_OK, ft_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
    uint32_t got_crc = ft_crc32_ieee(dst, total, 0);
    free(dst);

    TEST_ASSERT_EQUAL_UINT32(expected_crc, got_crc);

    /* Basic IO metric print (for manual inspection on host). */
    double payload_mib = (double)total / (1024.0 * 1024.0);
    double prog_mib = (double)ctx.prog_bytes / (1024.0 * 1024.0);
    double wa = (payload_mib > 0.0) ? (prog_mib / payload_mib) : 0.0;
    printf("[blob-stress] payload=%.2fMiB prog=%.2fMiB WA=%.2f prog_calls=%llu read_calls=%llu "
           "erase_calls=%llu\n",
           payload_mib, prog_mib, wa, (unsigned long long)ctx.prog_calls,
           (unsigned long long)ctx.read_calls, (unsigned long long)ctx.erase_calls);

    ft_kv_close(kv);
}

void ft_register_tests_blob(void) {
    RUN_TEST(test_blobdesc_roundtrip);
    RUN_TEST(test_blobdesc_decode_bounds_reports_needed);
    RUN_TEST(test_indexblock_roundtrip);
    RUN_TEST(test_blob_put_and_readback_64k);
    RUN_TEST(test_blob_truncate_smaller);
    RUN_TEST(test_blob_range_write);
    RUN_TEST(test_blob_writer_multi_step_and_abort);
    RUN_TEST(test_blob_gc_and_recovery_after_updates);
    RUN_TEST(test_blob_stream_1mib_gated);
}
