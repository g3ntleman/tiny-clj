// tiny_db.h - Public API (embedded-friendly, no heap allocation)
//
// KV-DB using B-Tree with Copy-on-Write Log-based storage.
// All writes are append-only (flash-friendly, no RMW).
//
// All comments and docs in English (workspace rule).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Types ============== */

typedef uint64_t tdb_time_t;

typedef enum tdb_status {
    TDB_OK = 0,
    TDB_ERR_INVALID_ARG = -1,
    TDB_ERR_IO = -2,
    TDB_ERR_CORRUPT = -3,
    TDB_ERR_NOT_FOUND = -4,
    TDB_ERR_NO_MEMORY = -5,
    TDB_ERR_UNSUPPORTED = -6,
} tdb_status_t;

typedef struct tdb_blob {
    const void* data;
    size_t len;
} tdb_blob_t;

/* ============== KV (Key/Value) ============== */

/* Standard start page for the root/catalog KV DB. */
#define TDB_KV_ROOT_PAGE 0u

typedef struct tdb_kv_cfg {
    /* Page number where this KV DB starts (internally: start_page * erase_granularity). */
    uint32_t start_page;
} tdb_kv_cfg_t;

typedef struct tdb_blockdev tdb_blockdev_t;

typedef tdb_status_t (*tdb_key_cb)(const void* key, size_t key_len, const void* val, size_t val_len,
                                 void* arg);

/* ============== Block device geometry contract (portability) ============== */

/*
 * tiny-db is storage-backend agnostic. Targets provide a block device with
 * explicit granularities so tiny-db can validate alignment and bounds.
 *
 * Terminology:
 * - read_granularity: smallest unit accepted by read()
 * - prog_granularity: smallest unit accepted by prog() (program/write)
 * - erase_granularity: smallest unit accepted by erase() (typically a flash sector)
 *
 * Requirements:
 * - All granularities are power-of-2.
 * - addr/len passed to each op must be aligned to its respective granularity.
 * - prog() must follow NOR-flash semantics (bits may only transition 1 -> 0).
 *   Any 0 -> 1 transition requires erase() first.
 *
 * Sizing policy (Variant B, used by KV):
 * - record_size == erase_granularity
 * - on-flash record = header + page_payload
 * - page_payload == erase_granularity - sizeof(header)
 *
 * This policy keeps the log record aligned to erase blocks and is the basis
 * for removing BSD btree overflow pages in higher layers (chunking instead).
 */

/* ============== KV Database (opaque, heap-allocated internally) ============== */

typedef struct tdb_kv tdb_kv_t;
typedef struct tdb_kv_cursor tdb_kv_cursor_t;
typedef struct tdb_blob_writer tdb_blob_writer_t;

/*
 * Initialize a KV database.
 * Uses B-Tree with Copy-on-Write log storage.
 */
tdb_status_t tdb_kv_open(tdb_kv_t** out_kv, tdb_blockdev_t* bdev, const tdb_kv_cfg_t* cfg);
void tdb_kv_close(tdb_kv_t* kv);

/* KV operations */
tdb_status_t tdb_kv_put(tdb_kv_t* kv, const void* key, size_t key_len, const void* val,
                      size_t val_len);
tdb_status_t tdb_kv_get(tdb_kv_t* kv, const void* key, size_t key_len, tdb_blob_t* out);
tdb_status_t tdb_kv_get_len(tdb_kv_t* kv, const void* key, size_t key_len, size_t* out_len);
tdb_status_t tdb_kv_get_into(tdb_kv_t* kv, const void* key, size_t key_len, void* out, size_t out_len,
                           size_t* saved_len_out);
tdb_status_t tdb_kv_del(tdb_kv_t* kv, const void* key, size_t key_len);

/*
 * Query the maximum allowed value length for a given key length.
 *
 * Notes:
 * - The limit depends on the key length (the btree stores key+value inline).
 * - If key_len is too large, *out_max_val_len will be set to 0.
 */
tdb_status_t tdb_kv_max_val_len(const tdb_kv_t* kv, size_t key_len, size_t* out_max_val_len);

/* Prefix iteration (callback-based) */
tdb_status_t tdb_kv_iter_prefix(tdb_kv_t* kv, const void* prefix, size_t prefix_len, tdb_key_cb cb,
                              void* arg);

/* ============== Cursor API ============== */

tdb_status_t tdb_kv_cursor_open_prefix(tdb_kv_t* kv, const void* prefix, size_t prefix_len,
                                     tdb_kv_cursor_t** out_cur);
tdb_status_t tdb_kv_cursor_next(tdb_kv_cursor_t* cur, int* out_has_item);
tdb_status_t tdb_kv_cursor_key(const tdb_kv_cursor_t* cur, tdb_blob_t* out_key);
tdb_status_t tdb_kv_cursor_val(const tdb_kv_cursor_t* cur, tdb_blob_t* out_val);
void tdb_kv_cursor_close(tdb_kv_cursor_t* cur);

/* ============== GC / Compaction ============== */

tdb_status_t tdb_kv_gc_step(tdb_kv_t* kv, size_t budget_bytes);

/*
 * Incremental GC/compaction step with a compact return value:
 * - < 0: tdb_status_t error code (TDB_ERR_*)
 * - 0: success, no more work remains
 * - 1: success, more work remains
 */
int tdb_kv_gc_step_more(tdb_kv_t* kv, size_t budget_bytes);

/* ============== Large values (blob over Meta/Index keys) ============== */

/*
 * Encoding:
 * - Meta key:  <user_key> | 0x00 | 'M'
 * - Index key: <user_key> | 0x00 | 'C' | gen_be(u32) | block_i_be(u32)
 *
 * Meta value contains blob descriptor + an inline list of data page numbers (pgno).
 * Index values contain additional pgno arrays when the inline list is insufficient.
 */

typedef tdb_status_t (*tdb_blob_stream_cb)(const void* data, size_t len, void* arg);

/*
 * Query the blob chunk size (payload bytes per data page).
 *
 * This is typically (erase_granularity - sizeof(tdb_page_hdr_t)) for the KV policy,
 * e.g. 4096 - 16 = 4080 on ESP32-like setups.
 */
tdb_status_t tdb_blob_chunk_size(tdb_kv_t* kv, size_t* out_chunk_size);

/* Convenience: replace whole blob (writes + commits). */
tdb_status_t tdb_blob_put(tdb_kv_t* kv, const void* key, size_t key_len, const void* data, size_t len);

/* Read APIs */
tdb_status_t tdb_blob_get_len(tdb_kv_t* kv, const void* key, size_t key_len, size_t* out_len);
tdb_status_t tdb_blob_get_into(tdb_kv_t* kv, const void* key, size_t key_len, void* out,
                             size_t out_len, size_t* saved_len_out);
tdb_status_t tdb_blob_stream(tdb_kv_t* kv, const void* key, size_t key_len, tdb_blob_stream_cb cb,
                           void* arg);

/* Writer API (append-style). finish/abort free the writer. */
tdb_status_t tdb_blob_writer_init(tdb_kv_t* kv, const void* key, size_t key_len,
                                tdb_blob_writer_t** out_writer);
tdb_status_t tdb_blob_write(tdb_blob_writer_t* writer, const void* data, size_t len);
tdb_status_t tdb_blob_finish(tdb_blob_writer_t* writer);
void tdb_blob_abort(tdb_blob_writer_t* writer);

/* Truncate an existing blob to new_size bytes. */
tdb_status_t tdb_blob_truncate(tdb_kv_t* kv, const void* key, size_t key_len, size_t new_size);

/* Follow-up: range write (random update). */
tdb_status_t tdb_blob_write_range(tdb_kv_t* kv, const void* key, size_t key_len, size_t offset,
                                const void* data, size_t len);

/* ============== Mpool tuning (optional) ============== */

/*
 * Configure the mpool page cache size used by the embedded B-Tree.
 *
 * Notes:
 * - Applies to subsequent opens (mpool_open), not already-open instances.
 * - Minimum is 3 pages for top-down split.
 */
tdb_status_t tdb_mpool_set_cache_pagecount(uint32_t count);
uint32_t tdb_mpool_get_cache_pagecount(void);

/*
 * Enable/disable automatic cache sizing when PSRAM is available (ESP32).
 * When enabled and PSRAM is present, mpool_open will raise the cache size
 * to a higher default if the current configured value is smaller.
 */
tdb_status_t tdb_mpool_enable_psram_autosize(int enable);

/* ============== Backwards-compatible aliases (temporary) ============== */

typedef tdb_kv_t tdb_db_t;
typedef tdb_kv_cursor_t tdb_cursor_t;
typedef tdb_kv_cfg_t tdb_cfg_t;

static inline tdb_status_t tdb_db_init(tdb_db_t** out_db, tdb_blockdev_t* bdev, const tdb_cfg_t* cfg) {
    return tdb_kv_open((tdb_kv_t**)out_db, bdev, (const tdb_kv_cfg_t*)cfg);
}
static inline void tdb_db_deinit(tdb_db_t* db) {
    tdb_kv_close((tdb_kv_t*)db);
}
static inline tdb_status_t tdb_put(tdb_db_t* db, const void* k, size_t kl, const void* v, size_t vl) {
    return tdb_kv_put((tdb_kv_t*)db, k, kl, v, vl);
}
static inline tdb_status_t tdb_get(tdb_db_t* db, const void* k, size_t kl, tdb_blob_t* out) {
    return tdb_kv_get((tdb_kv_t*)db, k, kl, out);
}
static inline tdb_status_t tdb_get_len(tdb_db_t* db, const void* k, size_t kl, size_t* out_len) {
    return tdb_kv_get_len((tdb_kv_t*)db, k, kl, out_len);
}
static inline tdb_status_t tdb_get_into(tdb_db_t* db, const void* k, size_t kl, void* out,
                                      size_t out_len, size_t* saved_len_out) {
    return tdb_kv_get_into((tdb_kv_t*)db, k, kl, out, out_len, saved_len_out);
}
static inline tdb_status_t tdb_del(tdb_db_t* db, const void* k, size_t kl) {
    return tdb_kv_del((tdb_kv_t*)db, k, kl);
}
static inline tdb_status_t tdb_iter_prefix(tdb_db_t* db, const void* pfx, size_t pfx_len, tdb_key_cb cb,
                                         void* arg) {
    return tdb_kv_iter_prefix((tdb_kv_t*)db, pfx, pfx_len, cb, arg);
}
static inline tdb_status_t tdb_cursor_open_prefix(tdb_db_t* db, const void* pfx, size_t pfx_len,
                                                tdb_cursor_t** out_cur) {
    return tdb_kv_cursor_open_prefix((tdb_kv_t*)db, pfx, pfx_len, (tdb_kv_cursor_t**)out_cur);
}
static inline tdb_status_t tdb_cursor_next(tdb_cursor_t* cur, int* out_has_item) {
    return tdb_kv_cursor_next((tdb_kv_cursor_t*)cur, out_has_item);
}
static inline tdb_status_t tdb_cursor_key(const tdb_cursor_t* cur, tdb_blob_t* out_key) {
    return tdb_kv_cursor_key((const tdb_kv_cursor_t*)cur, out_key);
}
static inline tdb_status_t tdb_cursor_val(const tdb_cursor_t* cur, tdb_blob_t* out_val) {
    return tdb_kv_cursor_val((const tdb_kv_cursor_t*)cur, out_val);
}
static inline void tdb_cursor_close(tdb_cursor_t* cur) {
    tdb_kv_cursor_close((tdb_kv_cursor_t*)cur);
}
static inline tdb_status_t tdb_gc_step(tdb_db_t* db, size_t budget_bytes) {
    return tdb_kv_gc_step((tdb_kv_t*)db, budget_bytes);
}

#ifdef __cplusplus
} // extern "C"
#endif
