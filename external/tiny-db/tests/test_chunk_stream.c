// test_chunk_stream.c - Contract test for chunked value streaming via prefix cursor.
//
// This is a low-level test for:
// - deterministic chunk-key encoding (big-endian ordering)
// - prefix cursor iteration order
// - streaming integrity via CRC32
//
// Note: To keep the codebase on track for removing BSD btree overflow pages,
// this test intentionally chooses a chunk size that stays below the btree
// overflow threshold for the configured page size.

#include "unity.h"

#include "tiny_db.h"
#include "tdb_blockdev.h"
#include "tdb_crc32.h"
#include "tdb_page_policy.h"

#define __DBINTERFACE_PRIVATE
#include "tdb_bsd_db.h"
#include "tdb_bsd_btree.h" // for BTDATAOFF/DEFMINKEYPAGE/NBLEAFDBT/indx_t
#include "tdb_bsd_mpool.h" // for tdb_page_hdr_t size

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds_monotonic(void) {
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

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
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]); // 1->0 only
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

static void u32_be_write(uint8_t out[4], uint32_t v) {
    out[0] = (uint8_t)((v >> 24) & 0xFF);
    out[1] = (uint8_t)((v >> 16) & 0xFF);
    out[2] = (uint8_t)((v >> 8) & 0xFF);
    out[3] = (uint8_t)(v & 0xFF);
}

static uint32_t u32_be_read(const uint8_t in[4]) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) |
           ((uint32_t)in[3]);
}

typedef struct __attribute__((packed)) chunk_meta {
    uint32_t gen_be;
    uint32_t chunk_size_be;
    uint32_t total_len_be;
    uint32_t crc32_be;
} chunk_meta_t;

static uint8_t payload_byte(uint32_t idx, uint32_t off) {
    // Deterministic, order-independent byte generator (fast hash mix).
    uint32_t x = idx * 0x9E3779B1u ^ off * 0x85EBCA6Bu ^ 0xC001D00Du;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (uint8_t)(x >> 24);
}

static void fill_payload(uint8_t* dst, uint32_t idx, size_t n) {
    for (size_t k = 0; k < n; k++) {
        dst[k] = payload_byte(idx, (uint32_t)k);
    }
}

static uint32_t btree_ovfl_cutoff_bytes(uint32_t psize) {
    // Mirrors the calculation in tdb_bt_open.c for t->bt_ovflsize.
    indx_t ov = (indx_t)((psize - BTDATAOFF) / DEFMINKEYPAGE - (sizeof(indx_t) + NBLEAFDBT(0, 0)));
    if (ov < (indx_t)(NBLEAFDBT(NOVFLSIZE, NOVFLSIZE) + sizeof(indx_t))) {
        ov = (indx_t)(NBLEAFDBT(NOVFLSIZE, NOVFLSIZE) + sizeof(indx_t));
    }
    return (uint32_t)ov;
}

static void run_chunk_stream(size_t total_len, const char* label) {
    // Keep this test fast: large payloads cause lots of btree splits.
    const uint32_t erase_g = 4096;

    // Allocate enough "flash" to hold lots of btree churn (splits, rewrites).
    // This is a host contract test; we size generously to avoid "log full".
    // For stress runs we size the backing store larger to avoid GC interaction.
    const size_t flash_bytes = (total_len <= (256u << 10)) ? (16u << 20) : (128u << 20);
    uint8_t* storage = (uint8_t*)malloc(flash_bytes);
    TEST_ASSERT_NOT_NULL(storage);
    memset(storage, 0xFF, flash_bytes);

    ramdev_t rd = {.buf = storage, .len = flash_bytes};
    tdb_blockdev_t bdev = {
        .ctx = &rd,
        .ops = {.read = ram_read, .prog = ram_prog, .erase = ram_erase},
        .geom = {.total_size_bytes = (uint32_t)rd.len,
                 .read_granularity = 1,
                 .prog_granularity = 4,
                 .erase_granularity = erase_g},
    };
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_blockdev_validate(&bdev));

    // Derive btree page size (Variant B) and a safe chunk size (no overflow pages).
    tdb_page_policy_t pol = {0};
    TEST_ASSERT_EQUAL_INT(
        TDB_OK, tdb_page_policy_compute_variant_b(&bdev.geom, sizeof(tdb_page_hdr_t), &pol));

    const uint32_t ov_cutoff = btree_ovfl_cutoff_bytes(pol.page_size);

    // Keys in this test are small; reserve a conservative key budget.
    const uint32_t key_budget = 64;
    TEST_ASSERT_TRUE(ov_cutoff > key_budget + 16);
    // Be conservative: keep values comfortably below the cutoff so we don't
    // accidentally trigger overflow behavior during this contract test.
    const uint32_t chunk_size_max =
        (ov_cutoff > (key_budget + 256)) ? (ov_cutoff - key_budget - 256) : 256;
    uint32_t chunk_size = chunk_size_max;

    /* Optional tuning knob for quick experiments. */
    const char* cs_env = getenv("TINY_DB_CHUNK_SIZE");
    if (cs_env && cs_env[0] != '\0') {
        char* endp = NULL;
        unsigned long req = strtoul(cs_env, &endp, 10);
        if (endp != cs_env && req > 0) {
            uint32_t r = (uint32_t)req;
            if (r > chunk_size_max)
                r = chunk_size_max;
            /* Keep alignment nice for typical flash prog granularity. */
            r &= ~3u;
            if (r >= 256)
                chunk_size = r;
        }
    }
    const uint32_t chunk_count =
        (uint32_t)((total_len + (size_t)chunk_size - 1) / (size_t)chunk_size);

    const uint32_t gen = 1;
    const uint8_t user_key[] = "chunk-stream";

    // Open KV
    tdb_kv_t* kv = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_open(&kv, &bdev, NULL));
    TEST_ASSERT_NOT_NULL(kv);

    // Write meta.
    chunk_meta_t meta = {0};
    u32_be_write((uint8_t*)&meta.gen_be, gen);
    u32_be_write((uint8_t*)&meta.chunk_size_be, chunk_size);
    u32_be_write((uint8_t*)&meta.total_len_be, (uint32_t)total_len);

    uint8_t* chunk_buf = (uint8_t*)malloc(chunk_size);
    TEST_ASSERT_NOT_NULL(chunk_buf);

    uint32_t expected_crc = 0;
    for (uint32_t idx = 0; idx < chunk_count; idx++) {
        const size_t base = (size_t)idx * (size_t)chunk_size;
        const size_t remain = (base < total_len) ? (total_len - base) : 0;
        const size_t n = (remain < chunk_size) ? remain : chunk_size;
        fill_payload(chunk_buf, idx, n);
        expected_crc = tdb_crc32_ieee(chunk_buf, n, expected_crc);
    }

    // Keys:
    //   meta:  <user_key> | 0x00 | 'M'
    //   chunk: <user_key> | 0x00 | 'C' | gen_be(u32) | i_be(u32)
    uint8_t meta_key[sizeof(user_key) + 2];
    size_t meta_key_len = 0;
    memcpy(meta_key, user_key, sizeof(user_key) - 1);
    meta_key_len += sizeof(user_key) - 1;
    meta_key[meta_key_len++] = 0x00;
    meta_key[meta_key_len++] = (uint8_t)'M';

    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, meta_key, meta_key_len, &meta, sizeof(meta)));

    const double t_start = now_seconds_monotonic();
    double t_last = t_start;
    size_t bytes_last = 0;
    uint64_t prog_bytes_last = 0;
    uint64_t read_bytes_last = 0;
    uint64_t prog_calls_last = 0;
    uint64_t read_calls_last = 0;

    uint32_t i = 0;
    size_t produced = 0;
    for (uint32_t step = 0; step < chunk_count; step++) {
        const uint32_t idx = step;
        const size_t base = (size_t)idx * (size_t)chunk_size;
        const size_t remain = (base < total_len) ? (total_len - base) : 0;
        const size_t n = (remain < chunk_size) ? remain : chunk_size;

        fill_payload(chunk_buf, idx, n);

        uint8_t chunk_key[sizeof(user_key) + 1 + 1 + 4 + 4];
        size_t chunk_key_len = 0;
        memcpy(chunk_key, user_key, sizeof(user_key) - 1);
        chunk_key_len += sizeof(user_key) - 1;
        chunk_key[chunk_key_len++] = 0x00;
        chunk_key[chunk_key_len++] = (uint8_t)'C';
        u32_be_write(&chunk_key[chunk_key_len], gen);
        chunk_key_len += 4;
        u32_be_write(&chunk_key[chunk_key_len], idx);
        chunk_key_len += 4;

        tdb_status_t put_st = tdb_kv_put(kv, chunk_key, chunk_key_len, chunk_buf, n);
        if (put_st != TDB_OK) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "tdb_kv_put failed: st=%d errno=%d idx=%u key_len=%zu val_len=%zu", (int)put_st,
                     errno, (unsigned)idx, chunk_key_len, n);
            TEST_FAIL_MESSAGE(msg);
        }

        produced += n;
        i++;
        if ((i % 128) == 0 || step + 1 == chunk_count) {
            const double t_now = now_seconds_monotonic();
            const double dt = (t_now - t_last);
            const size_t db = produced - bytes_last;
            const uint64_t d_prog_b = rd.prog_bytes - prog_bytes_last;
            const uint64_t d_read_b = rd.read_bytes - read_bytes_last;
            const uint64_t d_prog_c = rd.prog_calls - prog_calls_last;
            const uint64_t d_read_c = rd.read_calls - read_calls_last;
            const double mib_payload_s = (dt > 0.0) ? ((double)db / (1024.0 * 1024.0)) / dt : 0.0;
            const double mib_prog_s =
                (dt > 0.0) ? ((double)d_prog_b / (1024.0 * 1024.0)) / dt : 0.0;
            const double wa = (db > 0) ? ((double)d_prog_b / (double)db) : 0.0;

            printf("%s: wrote %u chunks (%zu bytes) payload=%.2f MiB/s prog=%.2f MiB/s WA=%.2fx "
                   "prog_calls=%llu read_calls=%llu\n",
                   label, (unsigned)i, produced, mib_payload_s, mib_prog_s, wa,
                   (unsigned long long)d_prog_c, (unsigned long long)d_read_c);
            fflush(stdout);
            t_last = t_now;
            bytes_last = produced;
            prog_bytes_last = rd.prog_bytes;
            read_bytes_last = rd.read_bytes;
            prog_calls_last = rd.prog_calls;
            read_calls_last = rd.read_calls;
        }
    }
    u32_be_write((uint8_t*)&meta.crc32_be, expected_crc);

    // Overwrite meta with final CRC.
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_put(kv, meta_key, meta_key_len, &meta, sizeof(meta)));

    // Cursor prefix: <user_key> | 0x00 | 'C' | gen_be
    uint8_t prefix[sizeof(user_key) + 1 + 1 + 4];
    size_t prefix_len = 0;
    memcpy(prefix, user_key, sizeof(user_key) - 1);
    prefix_len += sizeof(user_key) - 1;
    prefix[prefix_len++] = 0x00;
    prefix[prefix_len++] = (uint8_t)'C';
    u32_be_write(&prefix[prefix_len], gen);
    prefix_len += 4;

    tdb_kv_cursor_t* cur = NULL;
    TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_open_prefix(kv, prefix, prefix_len, &cur));
    TEST_ASSERT_NOT_NULL(cur);

    uint32_t stream_crc = 0;
    uint32_t seen = 0;
    int has = 0;
    while (1) {
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_next(cur, &has));
        if (!has)
            break;

        tdb_blob_t k = {0}, v = {0};
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_key(cur, &k));
        TEST_ASSERT_EQUAL_INT(TDB_OK, tdb_kv_cursor_val(cur, &v));

        TEST_ASSERT_TRUE(k.len >= prefix_len + 4);
        const uint8_t* kb = (const uint8_t*)k.data;
        const uint32_t idx = u32_be_read(&kb[k.len - 4]);
        TEST_ASSERT_EQUAL_UINT32(seen, idx);

        stream_crc = tdb_crc32_ieee(v.data, v.len, stream_crc);
        seen++;
    }

    tdb_kv_cursor_close(cur);

    TEST_ASSERT_EQUAL_UINT32(chunk_count, seen);
    TEST_ASSERT_EQUAL_UINT32(expected_crc, stream_crc);

    const double t_end = now_seconds_monotonic();
    const double total_s = (t_end - t_start);
    const double avg_mib_s =
        (total_s > 0.0) ? ((double)total_len / (1024.0 * 1024.0)) / total_s : 0.0;
    const double avg_prog_mib_s =
        (total_s > 0.0) ? ((double)rd.prog_bytes / (1024.0 * 1024.0)) / total_s : 0.0;
    const double total_wa = (total_len > 0) ? ((double)rd.prog_bytes / (double)total_len) : 0.0;
    printf(
        "%s: total %zu bytes in %.3fs (payload avg %.2f MiB/s) (prog avg %.2f MiB/s) (WA %.2fx)\n",
        label, total_len, total_s, avg_mib_s, avg_prog_mib_s, total_wa);
    fflush(stdout);

    free(chunk_buf);
    tdb_kv_close(kv);
    free(storage);
}

static void test_chunk_stream_256k_crc_and_order(void) {
    run_chunk_stream(256u << 10, "chunk_stream_256k");
}

static void test_chunk_stream_1m_crc_and_order_stress(void) {
    const char* env = getenv("TINY_DB_STRESS");
    if (!env || env[0] == '\0' || (env[0] == '0' && env[1] == '\0')) {
        TEST_IGNORE_MESSAGE("set TINY_DB_STRESS=1 to enable");
    }
    run_chunk_stream(1u << 20, "chunk_stream_1m");
}

void tdb_register_tests_chunk_stream(void) {
    RUN_TEST(test_chunk_stream_256k_crc_and_order);
    RUN_TEST(test_chunk_stream_1m_crc_and_order_stress);
}
