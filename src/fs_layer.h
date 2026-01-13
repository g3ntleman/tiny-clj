#ifndef TINY_CLJ_FS_LAYER_H
#define TINY_CLJ_FS_LAYER_H

#include "object.h"
#include "namespace.h" /* EvalState */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal KV-backed "filesystem-like" layer (Phase 2.2).
 *
 * This is intentionally small and deterministic. The backing store is a KV
 * database (later: flashdb-reloaded). For now we provide an in-memory KV store
 * for host unit tests.
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

