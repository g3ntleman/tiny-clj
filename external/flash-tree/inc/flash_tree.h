// flash_tree.h - Public API (minimal, test-first)
//
// NOTE: This is an early, host-test-oriented API. The on-flash layout and
// performance constraints will be iterated as the implementation matures.
//
// All comments and docs in English (workspace rule).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ft_time_t;

typedef struct ft_db ft_db_t;
typedef struct ft_tsdb ft_tsdb_t;

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

typedef struct ft_cfg {
    uint32_t reserved;
} ft_cfg_t;

typedef struct ft_blockdev ft_blockdev_t;

typedef ft_status_t (*ft_key_cb)(const void* key, size_t key_len,
                                const void* val, size_t val_len,
                                void* arg);

// DB init/deinit.
ft_status_t ft_db_init(ft_db_t** out_db, ft_blockdev_t* bdev, const ft_cfg_t* cfg);
void ft_db_deinit(ft_db_t* db);

// KV operations.
ft_status_t ft_put(ft_db_t* db, const void* key, size_t key_len, const void* val, size_t val_len);
ft_status_t ft_get(ft_db_t* db, const void* key, size_t key_len, ft_blob_t* out);
// "Caller-provided buffer" API (avoids intermediate copies in bindings).
ft_status_t ft_get_len(ft_db_t* db, const void* key, size_t key_len, size_t* out_len);
ft_status_t ft_get_into(ft_db_t* db, const void* key, size_t key_len, void* out, size_t out_len, size_t* saved_len_out);
ft_status_t ft_del(ft_db_t* db, const void* key, size_t key_len);

// Prefix iteration (keys in lexicographic byte-wise order).
ft_status_t ft_iter_prefix(ft_db_t* db, const void* prefix, size_t prefix_len, ft_key_cb cb, void* arg);

// Snapshot cursor (prefix-scoped).
typedef struct ft_cursor ft_cursor_t;

ft_status_t ft_cursor_open_prefix(ft_db_t* db, const void* prefix, size_t prefix_len, ft_cursor_t** out_cur);
ft_status_t ft_cursor_next(ft_cursor_t* cur, int* out_has_item);
ft_status_t ft_cursor_key(const ft_cursor_t* cur, ft_blob_t* out_key);
ft_status_t ft_cursor_val(const ft_cursor_t* cur, ft_blob_t* out_val);
void ft_cursor_close(ft_cursor_t* cur);

// GC/compaction step (optional, no-op for now). Must not run with open cursors.
ft_status_t ft_gc_step(ft_db_t* db, size_t budget_bytes);

// TSDB (v1 - minimal).
typedef enum ft_tsl_status {
    FT_TSL_STATUS_OK = 0,
    FT_TSL_STATUS_DROPPED = 1,
} ft_tsl_status_t;

typedef struct ft_tsdb_cfg {
    uint32_t reserved;
} ft_tsdb_cfg_t;

typedef ft_status_t (*ft_tsl_cb)(ft_time_t t, const void* data, size_t len, ft_tsl_status_t status, void* arg);

typedef struct ft_agg_f32 {
    uint64_t count;
    float sum;
    float min;
    float max;
    float avg;
} ft_agg_f32_t;

ft_status_t ft_tsdb_init(ft_tsdb_t** out_tsdb, ft_blockdev_t* bdev, const ft_tsdb_cfg_t* cfg);
void ft_tsdb_deinit(ft_tsdb_t* tsdb);

ft_status_t ft_tsl_append(ft_tsdb_t* tsdb, const void* data, size_t len, ft_time_t t);
ft_status_t ft_tsl_iter_by_time(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_cb cb, void* arg);
ft_status_t ft_tsl_query_count(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_status_t status, uint64_t* out_count);
ft_status_t ft_tsl_aggregate_f32(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_status_t status, ft_agg_f32_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

