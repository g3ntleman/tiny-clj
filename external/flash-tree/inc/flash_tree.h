// flash_tree.h - Public API (embedded-friendly, no heap allocation)
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

typedef uint64_t ft_time_t;

typedef enum ft_status {
    FT_OK = 0,
    FT_ERR_INVALID_ARG = -1,
    FT_ERR_IO = -2,
    FT_ERR_CORRUPT = -3,
    FT_ERR_NOT_FOUND = -4,
    FT_ERR_NO_MEMORY = -5,
    FT_ERR_UNSUPPORTED = -6,
} ft_status_t;

typedef struct ft_blob {
    const void* data;
    size_t len;
} ft_blob_t;

/* ============== KV (Key/Value) ============== */

/* Standard start page for the root/catalog KV DB. */
#define FT_KV_ROOT_PAGE 0u

typedef struct ft_kv_cfg {
    /* Page number where this KV DB starts (internally: start_page * erase_granularity). */
    uint32_t start_page;
} ft_kv_cfg_t;

typedef struct ft_blockdev ft_blockdev_t;

typedef ft_status_t (*ft_key_cb)(const void* key, size_t key_len, const void* val, size_t val_len,
                                 void* arg);

/* ============== Block device geometry contract (portability) ============== */

/*
 * flash-tree is storage-backend agnostic. Targets provide a block device with
 * explicit granularities so flash-tree can validate alignment and bounds.
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

typedef struct ft_kv ft_kv_t;
typedef struct ft_kv_cursor ft_kv_cursor_t;
typedef struct ft_blob_writer ft_blob_writer_t;

/*
 * Initialize a KV database.
 * Uses B-Tree with Copy-on-Write log storage.
 */
ft_status_t ft_kv_open(ft_kv_t** out_kv, ft_blockdev_t* bdev, const ft_kv_cfg_t* cfg);
void ft_kv_close(ft_kv_t* kv);

/* KV operations */
ft_status_t ft_kv_put(ft_kv_t* kv, const void* key, size_t key_len, const void* val,
                      size_t val_len);
ft_status_t ft_kv_get(ft_kv_t* kv, const void* key, size_t key_len, ft_blob_t* out);
ft_status_t ft_kv_get_len(ft_kv_t* kv, const void* key, size_t key_len, size_t* out_len);
ft_status_t ft_kv_get_into(ft_kv_t* kv, const void* key, size_t key_len, void* out, size_t out_len,
                           size_t* saved_len_out);
ft_status_t ft_kv_del(ft_kv_t* kv, const void* key, size_t key_len);

/*
 * Query the maximum allowed value length for a given key length.
 *
 * Notes:
 * - The limit depends on the key length (the btree stores key+value inline).
 * - If key_len is too large, *out_max_val_len will be set to 0.
 */
ft_status_t ft_kv_max_val_len(const ft_kv_t* kv, size_t key_len, size_t* out_max_val_len);

/* Prefix iteration (callback-based) */
ft_status_t ft_kv_iter_prefix(ft_kv_t* kv, const void* prefix, size_t prefix_len, ft_key_cb cb,
                              void* arg);

/* ============== Cursor API ============== */

ft_status_t ft_kv_cursor_open_prefix(ft_kv_t* kv, const void* prefix, size_t prefix_len,
                                     ft_kv_cursor_t** out_cur);
ft_status_t ft_kv_cursor_next(ft_kv_cursor_t* cur, int* out_has_item);
ft_status_t ft_kv_cursor_key(const ft_kv_cursor_t* cur, ft_blob_t* out_key);
ft_status_t ft_kv_cursor_val(const ft_kv_cursor_t* cur, ft_blob_t* out_val);
void ft_kv_cursor_close(ft_kv_cursor_t* cur);

/* ============== GC / Compaction ============== */

ft_status_t ft_kv_gc_step(ft_kv_t* kv, size_t budget_bytes);

/*
 * Incremental GC/compaction step with a compact return value:
 * - < 0: ft_status_t error code (FT_ERR_*)
 * - 0: success, no more work remains
 * - 1: success, more work remains
 */
int ft_kv_gc_step_more(ft_kv_t* kv, size_t budget_bytes);

/* ============== Large values (blob over Meta/Index keys) ============== */

/*
 * Encoding:
 * - Meta key:  <user_key> | 0x00 | 'M'
 * - Index key: <user_key> | 0x00 | 'C' | gen_be(u32) | block_i_be(u32)
 *
 * Meta value contains blob descriptor + an inline list of data page numbers (pgno).
 * Index values contain additional pgno arrays when the inline list is insufficient.
 */

typedef ft_status_t (*ft_blob_stream_cb)(const void* data, size_t len, void* arg);

/*
 * Query the blob chunk size (payload bytes per data page).
 *
 * This is typically (erase_granularity - sizeof(ft_page_hdr_t)) for the KV policy,
 * e.g. 4096 - 16 = 4080 on ESP32-like setups.
 */
ft_status_t ft_blob_chunk_size(ft_kv_t* kv, size_t* out_chunk_size);

/* Convenience: replace whole blob (writes + commits). */
ft_status_t ft_blob_put(ft_kv_t* kv, const void* key, size_t key_len, const void* data, size_t len);

/* Read APIs */
ft_status_t ft_blob_get_len(ft_kv_t* kv, const void* key, size_t key_len, size_t* out_len);
ft_status_t ft_blob_get_into(ft_kv_t* kv, const void* key, size_t key_len, void* out,
                             size_t out_len, size_t* saved_len_out);
ft_status_t ft_blob_stream(ft_kv_t* kv, const void* key, size_t key_len, ft_blob_stream_cb cb,
                           void* arg);

/* Writer API (append-style). finish/abort free the writer. */
ft_status_t ft_blob_writer_init(ft_kv_t* kv, const void* key, size_t key_len,
                                ft_blob_writer_t** out_writer);
ft_status_t ft_blob_write(ft_blob_writer_t* writer, const void* data, size_t len);
ft_status_t ft_blob_finish(ft_blob_writer_t* writer);
void ft_blob_abort(ft_blob_writer_t* writer);

/* Truncate an existing blob to new_size bytes. */
ft_status_t ft_blob_truncate(ft_kv_t* kv, const void* key, size_t key_len, size_t new_size);

/* Follow-up: range write (random update). */
ft_status_t ft_blob_write_range(ft_kv_t* kv, const void* key, size_t key_len, size_t offset,
                                const void* data, size_t len);

/* ============== Backwards-compatible aliases (temporary) ============== */

typedef ft_kv_t ft_db_t;
typedef ft_kv_cursor_t ft_cursor_t;
typedef ft_kv_cfg_t ft_cfg_t;

static inline ft_status_t ft_db_init(ft_db_t** out_db, ft_blockdev_t* bdev, const ft_cfg_t* cfg) {
    return ft_kv_open((ft_kv_t**)out_db, bdev, (const ft_kv_cfg_t*)cfg);
}
static inline void ft_db_deinit(ft_db_t* db) {
    ft_kv_close((ft_kv_t*)db);
}
static inline ft_status_t ft_put(ft_db_t* db, const void* k, size_t kl, const void* v, size_t vl) {
    return ft_kv_put((ft_kv_t*)db, k, kl, v, vl);
}
static inline ft_status_t ft_get(ft_db_t* db, const void* k, size_t kl, ft_blob_t* out) {
    return ft_kv_get((ft_kv_t*)db, k, kl, out);
}
static inline ft_status_t ft_get_len(ft_db_t* db, const void* k, size_t kl, size_t* out_len) {
    return ft_kv_get_len((ft_kv_t*)db, k, kl, out_len);
}
static inline ft_status_t ft_get_into(ft_db_t* db, const void* k, size_t kl, void* out,
                                      size_t out_len, size_t* saved_len_out) {
    return ft_kv_get_into((ft_kv_t*)db, k, kl, out, out_len, saved_len_out);
}
static inline ft_status_t ft_del(ft_db_t* db, const void* k, size_t kl) {
    return ft_kv_del((ft_kv_t*)db, k, kl);
}
static inline ft_status_t ft_iter_prefix(ft_db_t* db, const void* pfx, size_t pfx_len, ft_key_cb cb,
                                         void* arg) {
    return ft_kv_iter_prefix((ft_kv_t*)db, pfx, pfx_len, cb, arg);
}
static inline ft_status_t ft_cursor_open_prefix(ft_db_t* db, const void* pfx, size_t pfx_len,
                                                ft_cursor_t** out_cur) {
    return ft_kv_cursor_open_prefix((ft_kv_t*)db, pfx, pfx_len, (ft_kv_cursor_t**)out_cur);
}
static inline ft_status_t ft_cursor_next(ft_cursor_t* cur, int* out_has_item) {
    return ft_kv_cursor_next((ft_kv_cursor_t*)cur, out_has_item);
}
static inline ft_status_t ft_cursor_key(const ft_cursor_t* cur, ft_blob_t* out_key) {
    return ft_kv_cursor_key((const ft_kv_cursor_t*)cur, out_key);
}
static inline ft_status_t ft_cursor_val(const ft_cursor_t* cur, ft_blob_t* out_val) {
    return ft_kv_cursor_val((const ft_kv_cursor_t*)cur, out_val);
}
static inline void ft_cursor_close(ft_cursor_t* cur) {
    ft_kv_cursor_close((ft_kv_cursor_t*)cur);
}
static inline ft_status_t ft_gc_step(ft_db_t* db, size_t budget_bytes) {
    return ft_kv_gc_step((ft_kv_t*)db, budget_bytes);
}

#ifdef __cplusplus
} // extern "C"
#endif
