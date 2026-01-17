#ifndef TINY_CLJ_FS_LAYER_H
#define TINY_CLJ_FS_LAYER_H

#include "object.h"
#include "namespace.h" /* EvalState */

#include "tiny_db.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal KV-backed "filesystem-like" layer (Phase 2.2).
 *
 * This is intentionally small and deterministic. The backing store is a KV
 * database (tiny-db). For host unit tests we use a small RAM-backed blockdev.
 */

typedef struct FsKvStore FsKvStore;

FsKvStore *fs_kv_store_new(void);
void fs_kv_store_free(FsKvStore *st);

/* Process-global store used by native bindings. */
FsKvStore *fs_global_store(void);
void fs_global_store_reset(void);

/* Low-level KV ops (bytes) used by fs-layer tests. */
bool fs_kv_put(FsKvStore *st, const char *key, const uint8_t *data, size_t len);
size_t fs_kv_get(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out);
bool fs_kv_del(FsKvStore *st, const char *key);

/* Status-returning variants (for native bindings: nil only on NOT_FOUND). */
tdb_status_t fs_kv_put_status(FsKvStore *st, const char *key, const uint8_t *data, size_t len);
tdb_status_t fs_kv_get_status(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out);
tdb_status_t fs_kv_del_status(FsKvStore *st, const char *key);

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

/* File streaming (path is still string key) */
tdb_status_t fs_file_stream_read(FsKvStore* st,
                                const char* path,
                                size_t max_chunk,
                                fs_stream_sink_cb cb, void* arg);

/* Optional: start streaming from an offset (seek-by-chunk arithmetic). */
tdb_status_t fs_file_stream_read_from(FsKvStore* st,
                                     const char* path,
                                     size_t offset,
                                     size_t max_chunk,
                                     fs_stream_sink_cb cb, void* arg);

tdb_status_t fs_file_stream_write(FsKvStore* st, EvalState* eval,
                                 const char* path,
                                 fs_stream_source_cb next, void* arg,
                                 size_t* out_total_len);

/* -------------------------------------------------------------------------- */
/* FS layer (paths -> meta + versioned chunks)                                */
/* -------------------------------------------------------------------------- */

typedef enum {
    FS_NO_ERR = 0,
    FS_ERR_INVALID_PATH,
    FS_ERR_NOT_FOUND,
    FS_ERR_TYPE,
    FS_ERR_OOM,
    FS_ERR_IO,
} fs_err_t;

/* Create/update a directory meta entry (path must end with '/'). */
fs_err_t fs_mkdir(FsKvStore *st, const char *dir_path, ID ctime_inst, ID mtime_inst);

/* Write a file as versioned chunk keys and commit meta last. */
fs_err_t fs_write_bytes(FsKvStore *st, EvalState *eval, const char *path, const uint8_t *data, size_t len);

/* Read full file bytes into a new byte-array. */
ID fs_read_bytes(FsKvStore *st, EvalState *eval, const char *path);

/* Return a File-Map (Clojure map) or nil. */
ID fs_stat(FsKvStore *st, EvalState *eval, const char *path);

/* Delete meta key only (chunks are left for GC). Returns true if deleted. */
bool fs_delete(FsKvStore *st, const char *path);

/* List direct children (files/dirs) under a directory. Returns a vector. */
ID fs_list_dir(FsKvStore *st, EvalState *eval, const char *dir_path);

#endif /* TINY_CLJ_FS_LAYER_H */

