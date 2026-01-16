// test_blob.c - Tests for blob values (Meta-Key + Index-Keys + Data-Pages).

#include "unity.h"

#include "tiny_db.h"
#include "tdb_blockdev.h"
#include "tdb_crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Internal format helpers live in src/. */
#include "tdb_blob.h"

/* For tdb_page_hdr_t sizing (erase-block payload is 4096 - sizeof(header)). */
#define __DBINTERFACE_PRIVATE
#include "tdb_bsd_db.h"
#include "tdb_bsd_mpool.h"

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

static tdb_status_t ram_read(void* ctx, uint32_t addr, void* out, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    r->read_calls++;
    r->read_bytes += len;
    memcpy(out, r->buf + addr, len);
    return TDB_OK;
}

static tdb_status_t ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    r->prog_calls++;
    r->prog_bytes += len;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]); /* 1->0 only */
    }
    return TDB_OK;
}

static tdb_status_t ram_erase(void* ctx, uint32_t addr, size_t len) {
    ramdev_t* r = (ramdev_t*)ctx;
    if ((size_t)addr + len > r->len)
        return TDB_ERR_IO;
    r->erase_calls++;
    r->erase_bytes += len;
    memset(r->buf + addr, 0xFF, len);
    return TDB_OK;
}

static tdb_blockdev_t make_ram_bdev(ramdev_t* ctx, uint8_t* storage, size_t storage_len,
                                   uint32_t read_g, uint32_t prog_g, uint32_t erase_g) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->buf = storage;
    ctx->len = storage_len;
    tdb_blockdev_t bdev = {
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

    tdb_blob_desc_t d = {
        .version = (uint8_t)TDB_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = 4080,
        .logical_size = 123456,
        .generation = 7,
        .index_block_count = 3,
        .inline_pgno_count = 2,
    };

    size_t enc_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          tdb_blob_desc_encode(buf, sizeof(buf), &d, inline_pgnos, 2, &enc_len));
    TEST_ASSERT_EQUAL_UINT(TDB_BLOB_DESC_HDR_SIZE + 8u, (unsigned)enc_len);

    tdb_blob_desc_t out = {0};
    uint32_t out_pgnos[2] = {0};
    uint16_t cap = 2;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_desc_decode(buf, enc_len, &out, out_pgnos, &cap));
    TEST_ASSERT_EQUAL_UINT8(TDB_BLOB_DESC_VERSION, out.version);
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

    tdb_blob_desc_t d = {
        .version = (uint8_t)TDB_BLOB_DESC_VERSION,
        .reserved = 0,
        .chunk_size = 4080,
        .logical_size = 1,
        .generation = 1,
        .index_block_count = 0,
        .inline_pgno_count = 3,
    };
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          tdb_blob_desc_encode(buf, sizeof(buf), &d, inline_pgnos, 3, &enc_len));

    tdb_blob_desc_t out = {0};
    uint32_t out_pgnos[2] = {0};
    uint16_t cap = 2;
    TEST_ASSERT_EQUAL_INT(TDB_ERR_INVALID_ARG,
                          tdb_blob_desc_decode(buf, enc_len, &out, out_pgnos, &cap));
    TEST_ASSERT_EQUAL_UINT16(3u, cap); /* required */
}

static void test_indexblock_roundtrip(void) {
    uint8_t buf[128] = {0};
    uint32_t pgnos[3] = {11u, 22u, 33u};
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_index_block_encode(buf, sizeof(buf), pgnos, 3, &enc_len));
    TEST_ASSERT_EQUAL_UINT(TDB_INDEX_BLOCK_HDR_SIZE + 12u, (unsigned)enc_len);

    uint32_t out_pgnos[3] = {0};
    uint16_t cap = 3;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_index_block_decode(buf, enc_len, out_pgnos, &cap));
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

static double now_seconds_monotonic(void) {
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void test_blob_put_and_readback_64k(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:test1";
    const size_t total = 64u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    uint8_t* dst = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    TEST_ASSERT_NOT_NULL(dst);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i * 131u + 7u);

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    size_t got_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_get_len(kv, user_key, sizeof(user_key) - 1u, &got_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)got_len);

    size_t saved_len = 0;
    memset(dst, 0, total);
    TEST_ASSERT_EQUAL_INT(
        TDB_OK, tdb_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, total);

    free(dst);
    free(src);
    tdb_kv_close(kv);
}

static void test_blob_truncate_smaller(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:test2";
    const size_t total = 64u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i ^ 0xA5u);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    const size_t new_size = 20000u;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_truncate(kv, user_key, sizeof(user_key) - 1u, new_size));

    size_t got_len = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_get_len(kv, user_key, sizeof(user_key) - 1u, &got_len));
    TEST_ASSERT_EQUAL_UINT(new_size, (unsigned)got_len);

    uint8_t* dst = (uint8_t*)malloc(new_size);
    TEST_ASSERT_NOT_NULL(dst);
    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        TDB_OK, tdb_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, new_size, &saved_len));
    TEST_ASSERT_EQUAL_UINT(new_size, (unsigned)saved_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, new_size);

    free(dst);
    free(src);
    tdb_kv_close(kv);
}

static void test_blob_range_write(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:test3";
    const size_t total = 32u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    uint8_t* dst = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    TEST_ASSERT_NOT_NULL(dst);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i * 3u + 1u);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    const size_t off = 1000u;
    const size_t n = 2000u;
    uint8_t* patch = (uint8_t*)malloc(n);
    TEST_ASSERT_NOT_NULL(patch);
    fill_payload(patch, 42u, n);
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          tdb_blob_write_range(kv, user_key, sizeof(user_key) - 1u, off, patch, n));

    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        TDB_OK, tdb_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);

    /* Verify patch applied. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, off);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(patch, dst + off, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src + off + n, dst + off + n, total - off - n);

    free(patch);
    free(dst);
    free(src);
    tdb_kv_close(kv);
}

static void test_blob_writer_multi_step_and_abort(void) {
    static uint8_t storage[4096 * 256];
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char key_ok[] = "blob:writer:ok";
    const char key_abort[] = "blob:writer:abort";

    /* Multi-step writer should equal tdb_blob_put. */
    {
        const size_t total = 8192;
        uint8_t src[8192];
        for (size_t i = 0; i < total; i++)
            src[i] = (uint8_t)(i * 17u + 3u);

        tdb_blob_writer_t* w = NULL;
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_writer_init(kv, key_ok, sizeof(key_ok) - 1u, &w));
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_write(w, src, 1000));
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_write(w, src + 1000, 5000));
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_write(w, src + 6000, total - 6000));
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_finish(w));

        uint8_t dst[8192];
        size_t saved_len = 0;
        TEST_ASSERT_EQUAL_INT(
            TDB_OK, tdb_blob_get_into(kv, key_ok, sizeof(key_ok) - 1u, dst, sizeof(dst), &saved_len));
        TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, total);
    }

    /* Abort must not create a committed blob (meta key never written). */
    {
        tdb_blob_writer_t* w = NULL;
        TEST_ASSERT_EQUAL_INT(TDB_OK,
                              tdb_blob_writer_init(kv, key_abort, sizeof(key_abort) - 1u, &w));
        uint8_t tmp[123];
        memset(tmp, 0xAB, sizeof(tmp));
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_write(w, tmp, sizeof(tmp)));
        tdb_blob_abort(w);

        size_t got_len = 0;
        TEST_ASSERT_EQUAL_INT(TDB_ERR_NOT_FOUND,
                              tdb_blob_get_len(kv, key_abort, sizeof(key_abort) - 1u, &got_len));
    }

    tdb_kv_close(kv);
}

static void test_blob_gc_and_recovery_after_updates(void) {
    static uint8_t storage[4096 * 1024]; /* 4 MiB */
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:gc:recover";
    const size_t total = 128u * 1024u;
    uint8_t* expected = (uint8_t*)malloc(total);
    uint8_t* tmp = (uint8_t*)malloc(2048);
    TEST_ASSERT_NOT_NULL(expected);
    TEST_ASSERT_NOT_NULL(tmp);

    for (size_t i = 0; i < total; i++)
        expected[i] = (uint8_t)(i * 5u + 11u);
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_put(kv, user_key, sizeof(user_key) - 1u, expected, total));

    /* Apply a handful of updates and mirror them in expected[]. */
    for (uint32_t i = 0; i < 20; i++) {
        const size_t off = (size_t)(i * 997u) % (total - 2048u);
        fill_payload(tmp, i + 1u, 2048);
        memcpy(expected + off, tmp, 2048);
        TEST_ASSERT_EQUAL_INT(
            TDB_OK, tdb_blob_write_range(kv, user_key, sizeof(user_key) - 1u, off, tmp, 2048));
    }
    free(tmp);

    /* Run incremental GC until complete. */
    int more = 1;
    int iters = 0;
    while (more == 1) {
        more = tdb_kv_gc_step_more(kv, 4096 * 8);
        TEST_ASSERT_TRUE_MESSAGE(more == 0 || more == 1, "gc_step_more returned error");
        iters++;
        TEST_ASSERT_LESS_THAN_INT_MESSAGE(20000, iters, "gc loop too long");
    }

    tdb_kv_close(kv);

    /* Reopen and verify recovery + blob integrity. */
    kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));
    uint8_t* got = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(got);
    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        TDB_OK, tdb_blob_get_into(kv, user_key, sizeof(user_key) - 1u, got, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, got, total);
    free(got);
    free(expected);

    tdb_kv_close(kv);
}

static void test_blob_stream_1mib_gated(void) {
    const char* stress = getenv("TINY_DB_STRESS");
    if (!stress || stress[0] == '\0') {
        TEST_IGNORE();
        return;
    }

    static uint8_t storage[4096 * 1024]; /* 4 MiB */
    memset(storage, 0xFF, sizeof(storage));

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, sizeof(storage), 1, 1, 4096);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char user_key[] = "blob:stress1m";
    const size_t total = 1024u * 1024u;
    uint8_t* src = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(src);
    for (uint32_t i = 0; i < (uint32_t)total; i++)
        src[i] = payload_byte(i / 4096u, i % 4096u);

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_put(kv, user_key, sizeof(user_key) - 1u, src, total));

    uint32_t expected_crc = tdb_crc32_ieee(src, total, 0);
    free(src);

    /* For now, use get_into for CRC path (still exercises streaming internally). */
    uint8_t* dst = (uint8_t*)malloc(total);
    TEST_ASSERT_NOT_NULL(dst);
    size_t saved_len = 0;
    TEST_ASSERT_EQUAL_INT(
        TDB_OK, tdb_blob_get_into(kv, user_key, sizeof(user_key) - 1u, dst, total, &saved_len));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)saved_len);
    uint32_t got_crc = tdb_crc32_ieee(dst, total, 0);
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

    tdb_kv_close(kv);
}

typedef struct {
    tdb_blob_writer_t* w;
    uint32_t crc;
    size_t bytes;
} stream_copy_ctx_t;

static tdb_status_t stream_copy_cb(const void* data, size_t len, void* arg) {
    stream_copy_ctx_t* c = (stream_copy_ctx_t*)arg;
    if (!c || !c->w)
        return TDB_ERR_INVALID_ARG;
    tdb_status_t st = tdb_blob_write(c->w, data, len);
    if (st != TDB_OK)
        return st;
    c->crc = tdb_crc32_ieee((const uint8_t*)data, len, c->crc);
    c->bytes += len;
    return TDB_OK;
}

typedef struct {
    uint32_t crc;
    size_t bytes;
} stream_crc_ctx_t;

static tdb_status_t stream_crc_cb(const void* data, size_t len, void* arg) {
    stream_crc_ctx_t* c = (stream_crc_ctx_t*)arg;
    if (!c)
        return TDB_ERR_INVALID_ARG;
    c->crc = tdb_crc32_ieee((const uint8_t*)data, len, c->crc);
    c->bytes += len;
    return TDB_OK;
}

static void test_blob_stream_copy_1mib(void) {
    const size_t total = 1024u * 1024u;
    const uint32_t erase_g = 4096;

    // Size generously: two 1MiB blobs + btree churn + GC metadata.
    const size_t flash_bytes = 32u << 20; // 32 MiB
    uint8_t* storage = (uint8_t*)malloc(flash_bytes);
    TEST_ASSERT_NOT_NULL(storage);
    memset(storage, 0xFF, flash_bytes);

    ramdev_t ctx;
    tdb_blockdev_t bdev = make_ram_bdev(&ctx, storage, flash_bytes, 1, 4, erase_g);
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));

    const char key_src[] = "file:1m:src";
    const char key_dst[] = "file:1m:dst";

    // Create source blob without holding 1MiB in RAM.
    size_t chunk_size = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_chunk_size(kv, &chunk_size));
    TEST_ASSERT_TRUE(chunk_size > 0);
    TEST_ASSERT_TRUE(chunk_size <= 4096u);
    uint8_t* buf = (uint8_t*)malloc(chunk_size);
    TEST_ASSERT_NOT_NULL(buf);
    uint32_t expected_crc = 0;
    {
        tdb_blob_writer_t* w = NULL;
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_writer_init(kv, key_src, sizeof(key_src) - 1u, &w));
        size_t off = 0;
        while (off < total) {
            const size_t n = ((total - off) < chunk_size) ? (total - off) : chunk_size;
            // Deterministic pattern; do NOT assume 4096-byte chunks (blob chunk size is mp->pagesize, e.g. 4080).
            for (size_t i = 0; i < n; i++) {
                const uint32_t abs = (uint32_t)(off + i);
                buf[i] = payload_byte(abs, (uint32_t)i);
            }
            expected_crc = tdb_crc32_ieee(buf, n, expected_crc);
            TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_write(w, buf, n));
            off += n;
        }
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_finish(w));
    }
    free(buf);
    buf = NULL;

    // Streaming copy: tdb_blob_stream(src) -> tdb_blob_write(dst writer).
    const double t0 = now_seconds_monotonic();
    stream_copy_ctx_t copy = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_writer_init(kv, key_dst, sizeof(key_dst) - 1u, &copy.w));
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          tdb_blob_stream(kv, key_src, sizeof(key_src) - 1u, stream_copy_cb, &copy));
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blob_finish(copy.w));
    copy.w = NULL;
    const double t1 = now_seconds_monotonic();

    TEST_ASSERT_EQUAL_UINT(total, (unsigned)copy.bytes);
    TEST_ASSERT_EQUAL_UINT32(expected_crc, copy.crc);

    // Verify destination by streaming readback CRC (still O(1) RAM).
    stream_crc_ctx_t rd = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK,
                          tdb_blob_stream(kv, key_dst, sizeof(key_dst) - 1u, stream_crc_cb, &rd));
    TEST_ASSERT_EQUAL_UINT(total, (unsigned)rd.bytes);
    TEST_ASSERT_EQUAL_UINT32(expected_crc, rd.crc);

    // Basic metric print for manual inspection.
    const double dt = (t1 > t0) ? (t1 - t0) : 0.0;
    const double payload_mib = (double)total / (1024.0 * 1024.0);
    const double mib_s = (dt > 0.0) ? (payload_mib / dt) : 0.0;
    const double prog_mib = (double)ctx.prog_bytes / (1024.0 * 1024.0);
    const double wa = (payload_mib > 0.0) ? (prog_mib / payload_mib) : 0.0;
    printf("[file-copy] 1MiB copy: %.2f MiB/s (WA %.2fx) prog_calls=%llu read_calls=%llu\n", mib_s,
           wa, (unsigned long long)ctx.prog_calls, (unsigned long long)ctx.read_calls);

    tdb_kv_close(kv);
    free(storage);
}

void tdb_register_tests_blob(void) {
    RUN_TEST(test_blobdesc_roundtrip);
    RUN_TEST(test_blobdesc_decode_bounds_reports_needed);
    RUN_TEST(test_indexblock_roundtrip);
    RUN_TEST(test_blob_put_and_readback_64k);
    RUN_TEST(test_blob_truncate_smaller);
    RUN_TEST(test_blob_range_write);
    RUN_TEST(test_blob_writer_multi_step_and_abort);
    RUN_TEST(test_blob_gc_and_recovery_after_updates);
    RUN_TEST(test_blob_stream_1mib_gated);
    RUN_TEST(test_blob_stream_copy_1mib);
}
