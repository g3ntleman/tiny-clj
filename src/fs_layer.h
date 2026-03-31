#ifndef TINY_CLJ_FS_LAYER_H
#define TINY_CLJ_FS_LAYER_H

#include "object.h"

#include "tiny_db.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Max key length for path-based FS keys (including terminator when used as C-string). */
#define FS_KEY_MAX 64u

/* Reserved internal suffix byte for metadata sidecars stored adjacent to file keys. */
#define FS_META_SIDECAR_MARKER 0x01u

/* Minimal KV-backed "filesystem-like" layer (Phase 2.2).
 *
 * This is intentionally small and deterministic. The backing store is a KV
 * database (tiny-db). For host unit tests we use a small RAM-backed blockdev.
 * On ESP32, tiny-db is wired to a dedicated flash partition.
 */

typedef struct FsKvStore FsKvStore;

FsKvStore *fs_kv_store_new(void);
void fs_kv_store_free(FsKvStore *st);

/* Process-global store used by native bindings. */
FsKvStore *fs_global_store(void);
// Non-allocating accessor. Returns NULL if the global store has not been created yet.
FsKvStore *fs_global_store_if_initialized(void);
void fs_global_store_reset(void);

/* Low-level KV ops (bytes) used by fs-layer tests. */
bool fs_kv_put(FsKvStore *st, const char *key, const uint8_t *data, size_t len);
size_t fs_kv_get(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out);
bool fs_kv_del(FsKvStore *st, const char *key);

/* Status-returning variants (for native bindings: nil only on NOT_FOUND). */
tdb_status_t fs_kv_put_status(FsKvStore *st, const char *key, const uint8_t *data, size_t len);
tdb_status_t fs_kv_get_status(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out);
tdb_status_t fs_kv_del_status(FsKvStore *st, const char *key);
tdb_status_t fs_kv_max_val_len_status(FsKvStore *st, const char *key, size_t *out_max_val_len);
tdb_status_t fs_kv_sync_status(FsKvStore *st);

/* Blob-key variants used by tiny-db.kv (avoid C-string/strlen/snprintf). */
tdb_status_t fs_kv_put_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len);
tdb_status_t fs_kv_get_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len, uint8_t *out, size_t out_len, size_t *saved_len_out);
tdb_status_t fs_kv_del_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len);

/* -------------------------------------------------------------------------- */
/* Streaming APIs (C-only)                                                    */
/* -------------------------------------------------------------------------- */

typedef tdb_status_t (*fs_stream_sink_cb)(const uint8_t* data, size_t len, void* arg);
typedef tdb_status_t (*fs_stream_source_cb)(uint8_t* out, size_t out_cap, size_t* out_len, void* arg);

typedef struct FsStreamStats {
    uint64_t blocks_read;    /* number of stored chunk-keys read (not app slices, not meta) */
    uint64_t blocks_written; /* number of stored chunk-keys written (not meta) */
} FsStreamStats;

tdb_status_t fs_stream_stats_reset(FsKvStore* st);
tdb_status_t fs_stream_stats_get(const FsKvStore* st, FsStreamStats* out);

/* KV blob-key streaming */
tdb_status_t fs_kv_stream_read_key_bytes(FsKvStore* st,
                                        const uint8_t* key, size_t key_len,
                                        size_t max_chunk,
                                        fs_stream_sink_cb cb, void* arg);

/* Optional: start streaming from an offset (seek-by-chunk arithmetic). */
tdb_status_t fs_kv_stream_read_key_bytes_from(FsKvStore* st,
                                             const uint8_t* key, size_t key_len,
                                             size_t offset,
                                             size_t max_chunk,
                                             fs_stream_sink_cb cb, void* arg);

tdb_status_t fs_kv_stream_write_key_bytes(FsKvStore* st,
                                         const uint8_t* key, size_t key_len,
                                         fs_stream_source_cb next, void* arg,
                                         size_t* out_total_len);

/* File streaming (path-based, uses chunked storage) */
tdb_status_t fs_file_stream_read(FsKvStore* st,
                                const char* path,
                                size_t max_chunk,
                                fs_stream_sink_cb cb, void* arg);

tdb_status_t fs_file_stream_read_from(FsKvStore* st,
                                     const char* path,
                                     size_t offset,
                                     size_t max_chunk,
                                     fs_stream_sink_cb cb, void* arg);

tdb_status_t fs_file_stream_write(FsKvStore* st,
                                 const char* path,
                                 fs_stream_source_cb next, void* arg,
                                 size_t* out_total_len);

/* -------------------------------------------------------------------------- */
/* FS layer (simple path -> bytes, binary metadata)                           */
/* -------------------------------------------------------------------------- */

typedef enum {
    FS_NO_ERR = 0,
    FS_ERR_INVALID_PATH,
    FS_ERR_NOT_FOUND,
    FS_ERR_TYPE,
    FS_ERR_OOM,
    FS_ERR_IO,
} fs_err_t;

/* Write bytes directly to KV store at path. */
fs_err_t fs_write_bytes(FsKvStore *st, const char *path, const uint8_t *data, size_t len);

/* Set file size directly. Creates zero-filled chunks if extending the file,
 * or truncates/free trailing chunks if shrinking. Commits metadata atomically.
 */
fs_err_t fs_set_size(FsKvStore *st, const char *path, uint32_t size);

/* Read full file bytes into a new byte-array. Returns NULL if not found. */
ID fs_read_bytes(FsKvStore *st, const char *path);

/* Get file size. Returns -1 if not found. */
int64_t fs_stat_size(FsKvStore *st, const char *path);

/* Check if path exists. */
bool fs_exists(FsKvStore *st, const char *path);

/* Delete key. Returns true if deleted. */
bool fs_delete(FsKvStore *st, const char *path);

/* Build the internal metadata sidecar key for a public path.
 * Public paths reserve control bytes; the sidecar key appends FS_META_SIDECAR_MARKER.
 */
tdb_status_t fs_make_meta_sidecar_key(const char *path,
                                      uint8_t *out,
                                      size_t out_cap,
                                      size_t *out_len);

/*
 * List directory entries in batches.
 *
 * Reads up to batch_size entries under dir_path, starting after after_key
 * (exclusive). Writes a continuation key into out_last_key:
 * - out_last_key[0] == '\0' means no more data.
 * - otherwise, call again with after_key = out_last_key.
 *
 * Returns a vector of entry maps:
 *   {:path <string> :meta <map>}
 * where :meta includes at least formal fields like :size and :chunks when available.
 * Returns NULL on error.
 */
ID fs_list_dir_batch(FsKvStore *st,
                     const char *dir_path,
                     const char *after_key,
                     size_t batch_size,
                     char *out_last_key,
                     size_t out_last_key_cap);

/* List direct children paths under a directory (batched). */
#endif /* TINY_CLJ_FS_LAYER_H */
