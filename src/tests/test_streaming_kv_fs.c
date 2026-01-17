#include "tests_common.h"

#include "../fs_layer.h"

typedef struct {
    uint8_t* out;
    size_t out_cap;
    size_t out_len;
    size_t max_seen;
} SinkCtx;

static tdb_status_t sink_collect_cb(const uint8_t* data, size_t len, void* arg)
{
    SinkCtx* c = (SinkCtx*)arg;
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(len <= 4096); // contract: app never sees > 4KB
    if (len > c->max_seen) c->max_seen = len;
    if (c->out_len + len > c->out_cap) return TDB_ERR_NO_MEMORY;
    if (len) memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
    return TDB_OK;
}

TEST(test_kv_stream_read_enforces_4kb_and_preserves_bytes)
{
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    // 32KB test payload
    enum { N = 32 * 1024 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(i & 0xFF);

    const uint8_t key[] = {'k'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_key_bytes_status(st, key, sizeof(key), data, sizeof(data)));

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));

    static uint8_t out[N];
    SinkCtx ctx = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_stream_read_key_bytes(st, key, sizeof(key), 4096, sink_collect_cb, &ctx));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)ctx.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
    TEST_ASSERT_TRUE(ctx.max_seen <= 4096);

    FsStreamStats stats = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)stats.blocks_read); // 32KB / 4KB

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stats.blocks_read);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)stats.blocks_written);

    fs_kv_store_free(st);
}

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
    size_t call_idx;
} SrcCtx;

static tdb_status_t src_var_chunks(uint8_t* out, size_t out_cap, size_t* out_len, void* arg)
{
    if (out_len) *out_len = 0;
    SrcCtx* c = (SrcCtx*)arg;
    if (!c || (!out && out_cap != 0) || !out_len) return TDB_ERR_INVALID_ARG;
    if (c->pos >= c->len) return TDB_OK; // EOF -> out_len stays 0

    // Intentionally irregular sizes (includes > 4096).
    static const size_t pattern[] = {1, 7, 9000, 13, 4096, 3, 2000, 8191};
    const size_t want0 = pattern[c->call_idx % (sizeof(pattern) / sizeof(pattern[0]))];
    c->call_idx++;

    size_t want = want0;
    size_t remaining = c->len - c->pos;
    if (want > remaining) want = remaining;
    if (want > out_cap) want = out_cap;

    memcpy(out, c->data + c->pos, want);
    c->pos += want;
    *out_len = want;
    return TDB_OK;
}

TEST(test_kv_stream_write_accepts_variable_input_sizes_and_roundtrips)
{
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 32 * 1024 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(0xA5u ^ (uint8_t)(i & 0xFF));

    const uint8_t key[] = {'w'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));

    SrcCtx src = {.data = data, .len = sizeof(data), .pos = 0, .call_idx = 0};
    size_t total_written = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_stream_write_key_bytes(st, key, sizeof(key), src_var_chunks, &src, &total_written));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)total_written);

    static uint8_t out[N];
    size_t saved = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_get_key_bytes_status(st, key, sizeof(key), out, sizeof(out), &saved));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)saved);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));

    FsStreamStats stats = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)stats.blocks_written);

    fs_kv_store_free(st);
}

TEST(test_fs_file_stream_write_read_roundtrip_and_chunk_cap)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 9000 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(i * 3u);

    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_reset(st));

    SrcCtx src = {.data = data, .len = sizeof(data), .pos = 0, .call_idx = 0};
    size_t total_written = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_write(st, "/data/s.bin", src_var_chunks, &src, &total_written));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)total_written);

    static uint8_t out[N];
    SinkCtx sink = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};
    // Use a small max_chunk to ensure slicing works and stays <= max_chunk.
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_read(st, "/data/s.bin", 1024, sink_collect_cb, &sink));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)sink.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
    TEST_ASSERT_TRUE(sink.max_seen <= 1024);

    FsStreamStats stats = {0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_stream_stats_get(st, &stats));
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)stats.blocks_written); // 9000 -> 3 chunks of 4096
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)stats.blocks_read);

    fs_kv_store_free(st);
}

TEST(test_kv_stream_read_from_offset_seeks_by_chunk_arithmetic)
{
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 32 * 1024 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(i & 0xFF);

    const uint8_t key[] = {'s'};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_put_key_bytes_status(st, key, sizeof(key), data, sizeof(data)));

    const size_t off = 12345;
    static uint8_t out[N];
    SinkCtx sink = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_kv_stream_read_key_bytes_from(st, key, sizeof(key), off, 4096, sink_collect_cb, &sink));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(N - off), (uint32_t)sink.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data + off, out, N - off);
    fs_kv_store_free(st);
}

TEST(test_fs_file_stream_read_from_offset_seeks_by_chunk_arithmetic)
{
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    FsKvStore* st = fs_kv_store_new();
    TEST_ASSERT_NOT_NULL(st);

    enum { N = 9000 };
    static uint8_t data[N];
    for (size_t i = 0; i < (size_t)N; i++) data[i] = (uint8_t)(0x5Au + (uint8_t)i);

    SrcCtx src = {.data = data, .len = sizeof(data), .pos = 0, .call_idx = 0};
    size_t total_written = 0;
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_write(st, "/data/seek.bin", src_var_chunks, &src, &total_written));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(data), (uint32_t)total_written);

    const size_t off = 5000;
    static uint8_t out[N];
    SinkCtx sink = {.out = out, .out_cap = sizeof(out), .out_len = 0, .max_seen = 0};
    TEST_ASSERT_EQUAL_INT(TDB_OK, fs_file_stream_read_from(st, "/data/seek.bin", off, 1024, sink_collect_cb, &sink));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(N - off), (uint32_t)sink.out_len);
    TEST_ASSERT_EQUAL_MEMORY(data + off, out, N - off);

    fs_kv_store_free(st);
}

