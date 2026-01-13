// flash_tree.c - Orchestration layer (initial skeleton).

#include "flash_tree.h"

#include "ft_btree.h"

#include <stdlib.h>
#include <string.h>

typedef struct kv_entry {
    uint8_t* key;
    size_t key_len;
    uint8_t* val;
    size_t val_len;
} kv_entry_t;

typedef struct ts_entry {
    ft_time_t t;
    ft_tsl_status_t status;
    uint8_t* data;
    size_t len;
} ts_entry_t;

struct ft_db {
    ft_blockdev_t* bdev;
    kv_entry_t* entries;
    size_t n;
    size_t cap;
    size_t open_cursors;
};

struct ft_tsdb {
    ft_blockdev_t* bdev;
    ts_entry_t* entries;
    size_t n;
    size_t cap;
};

struct ft_cursor {
    ft_db_t* db;
    kv_entry_t* items; // deep-copied snapshot entries
    size_t n;
    size_t next_index;
    size_t current_index;
    int has_current;
};

// Forward decl (used for cleanup on allocation failure).
void ft_cursor_close(ft_cursor_t* cur);

static void kv_entry_free(kv_entry_t* e) {
    if (!e) return;
    free(e->key);
    free(e->val);
    e->key = NULL;
    e->val = NULL;
    e->key_len = 0;
    e->val_len = 0;
}

static size_t kv_lower_bound(const kv_entry_t* entries, size_t n, const void* key, size_t key_len) {
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = ft_lex_bytes_cmp(entries[mid].key, entries[mid].key_len, key, key_len);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int kv_key_equal(const kv_entry_t* e, const void* key, size_t key_len) {
    return e && e->key_len == key_len && memcmp(e->key, key, key_len) == 0;
}

static ft_status_t kv_ensure_cap(ft_db_t* db, size_t need) {
    if (need <= db->cap) return FT_OK;
    size_t new_cap = (db->cap == 0) ? 8 : (db->cap * 2);
    while (new_cap < need) new_cap *= 2;
    kv_entry_t* p = (kv_entry_t*)realloc(db->entries, new_cap * sizeof(kv_entry_t));
    if (!p) return FT_ERR_NO_MEMORY;
    // zero-init the tail for safe frees
    memset(p + db->cap, 0, (new_cap - db->cap) * sizeof(kv_entry_t));
    db->entries = p;
    db->cap = new_cap;
    return FT_OK;
}

ft_status_t ft_db_init(ft_db_t** out_db, ft_blockdev_t* bdev, const ft_cfg_t* cfg) {
    (void)cfg;
    if (!out_db || !bdev) return FT_ERR_INVALID_ARG;
    ft_db_t* db = (ft_db_t*)calloc(1, sizeof(ft_db_t));
    if (!db) return FT_ERR_NO_MEMORY;
    db->bdev = bdev;
    *out_db = db;
    return FT_OK;
}

void ft_db_deinit(ft_db_t* db) {
    if (!db) return;
    for (size_t i = 0; i < db->n; i++) kv_entry_free(&db->entries[i]);
    free(db->entries);
    free(db);
}

ft_status_t ft_put(ft_db_t* db, const void* key, size_t key_len, const void* val, size_t val_len) {
    if (!db || (!key && key_len != 0) || (!val && val_len != 0)) return FT_ERR_INVALID_ARG;
    size_t idx = kv_lower_bound(db->entries, db->n, key, key_len);
    if (idx < db->n && kv_key_equal(&db->entries[idx], key, key_len)) {
        // overwrite value
        uint8_t* new_val = NULL;
        if (val_len) {
            new_val = (uint8_t*)malloc(val_len);
            if (!new_val) return FT_ERR_NO_MEMORY;
            memcpy(new_val, val, val_len);
        }
        free(db->entries[idx].val);
        db->entries[idx].val = new_val;
        db->entries[idx].val_len = val_len;
        return FT_OK;
    }

    ft_status_t st = kv_ensure_cap(db, db->n + 1);
    if (st != FT_OK) return st;

    // shift to make room
    for (size_t i = db->n; i > idx; i--) {
        db->entries[i] = db->entries[i - 1];
    }
    memset(&db->entries[idx], 0, sizeof(db->entries[idx]));

    if (key_len) {
        db->entries[idx].key = (uint8_t*)malloc(key_len);
        if (!db->entries[idx].key) return FT_ERR_NO_MEMORY;
        memcpy(db->entries[idx].key, key, key_len);
    }
    if (val_len) {
        db->entries[idx].val = (uint8_t*)malloc(val_len);
        if (!db->entries[idx].val) {
            free(db->entries[idx].key);
            db->entries[idx].key = NULL;
            return FT_ERR_NO_MEMORY;
        }
        memcpy(db->entries[idx].val, val, val_len);
    }
    db->entries[idx].key_len = key_len;
    db->entries[idx].val_len = val_len;
    db->n++;
    return FT_OK;
}

ft_status_t ft_get(ft_db_t* db, const void* key, size_t key_len, ft_blob_t* out) {
    if (!db || (!key && key_len != 0) || !out) return FT_ERR_INVALID_ARG;
    size_t idx = kv_lower_bound(db->entries, db->n, key, key_len);
    if (idx >= db->n || !kv_key_equal(&db->entries[idx], key, key_len)) return FT_ERR_NOT_FOUND;
    out->data = db->entries[idx].val;
    out->len = db->entries[idx].val_len;
    return FT_OK;
}

ft_status_t ft_get_len(ft_db_t* db, const void* key, size_t key_len, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!db || (!key && key_len != 0) || !out_len) return FT_ERR_INVALID_ARG;
    size_t idx = kv_lower_bound(db->entries, db->n, key, key_len);
    if (idx >= db->n || !kv_key_equal(&db->entries[idx], key, key_len)) return FT_ERR_NOT_FOUND;
    *out_len = db->entries[idx].val_len;
    return FT_OK;
}

ft_status_t ft_get_into(ft_db_t* db, const void* key, size_t key_len, void* out, size_t out_len, size_t* saved_len_out) {
    if (saved_len_out) *saved_len_out = 0;
    if (!db || (!key && key_len != 0)) return FT_ERR_INVALID_ARG;

    size_t idx = kv_lower_bound(db->entries, db->n, key, key_len);
    if (idx >= db->n || !kv_key_equal(&db->entries[idx], key, key_len)) return FT_ERR_NOT_FOUND;

    const kv_entry_t* e = &db->entries[idx];
    if (saved_len_out) *saved_len_out = e->val_len;

    if (!out || out_len == 0) return FT_OK;
    size_t n = (e->val_len < out_len) ? e->val_len : out_len;
    if (n) memcpy(out, e->val, n);
    return FT_OK;
}

ft_status_t ft_del(ft_db_t* db, const void* key, size_t key_len) {
    if (!db || (!key && key_len != 0)) return FT_ERR_INVALID_ARG;
    size_t idx = kv_lower_bound(db->entries, db->n, key, key_len);
    if (idx >= db->n || !kv_key_equal(&db->entries[idx], key, key_len)) return FT_ERR_NOT_FOUND;
    kv_entry_free(&db->entries[idx]);
    for (size_t i = idx; i + 1 < db->n; i++) {
        db->entries[i] = db->entries[i + 1];
    }
    // clear last slot
    memset(&db->entries[db->n - 1], 0, sizeof(db->entries[db->n - 1]));
    db->n--;
    return FT_OK;
}

ft_status_t ft_iter_prefix(ft_db_t* db, const void* prefix, size_t prefix_len, ft_key_cb cb, void* arg) {
    if (!db) return FT_ERR_INVALID_ARG;
    if ((!prefix && prefix_len != 0) || !cb) return FT_ERR_INVALID_ARG;
    // Create a lightweight view array for the helper.
    ft_kv_ref_t tmp[128];
    if (db->n > 128) return FT_ERR_UNSUPPORTED;
    for (size_t i = 0; i < db->n; i++) {
        tmp[i].key = db->entries[i].key;
        tmp[i].key_len = db->entries[i].key_len;
        tmp[i].val = db->entries[i].val;
        tmp[i].val_len = db->entries[i].val_len;
    }
    return ft_iter_prefix_kv(tmp, db->n, prefix, prefix_len, cb, arg);
}

ft_status_t ft_cursor_open_prefix(ft_db_t* db, const void* prefix, size_t prefix_len, ft_cursor_t** out_cur) {
    if (!db || (!prefix && prefix_len != 0) || !out_cur) return FT_ERR_INVALID_ARG;
    ft_cursor_t* cur = (ft_cursor_t*)calloc(1, sizeof(ft_cursor_t));
    if (!cur) return FT_ERR_NO_MEMORY;

    // Count matches first.
    size_t start = kv_lower_bound(db->entries, db->n, prefix, prefix_len);
    size_t count = 0;
    for (size_t i = start; i < db->n; i++) {
        if (prefix_len && (db->entries[i].key_len < prefix_len || memcmp(db->entries[i].key, prefix, prefix_len) != 0)) break;
        count++;
    }

    if (count) {
        cur->items = (kv_entry_t*)calloc(count, sizeof(kv_entry_t));
        if (!cur->items) { free(cur); return FT_ERR_NO_MEMORY; }
        cur->n = count;
        for (size_t j = 0; j < count; j++) {
            const kv_entry_t* src = &db->entries[start + j];
            kv_entry_t* dst = &cur->items[j];
            if (src->key_len) {
                dst->key = (uint8_t*)malloc(src->key_len);
                if (!dst->key) { ft_cursor_close(cur); return FT_ERR_NO_MEMORY; }
                memcpy(dst->key, src->key, src->key_len);
                dst->key_len = src->key_len;
            }
            if (src->val_len) {
                dst->val = (uint8_t*)malloc(src->val_len);
                if (!dst->val) { ft_cursor_close(cur); return FT_ERR_NO_MEMORY; }
                memcpy(dst->val, src->val, src->val_len);
                dst->val_len = src->val_len;
            }
        }
    }

    cur->next_index = 0;
    cur->current_index = 0;
    cur->has_current = 0;
    cur->db = db;
    db->open_cursors++;
    *out_cur = cur;
    return FT_OK;
}

ft_status_t ft_cursor_next(ft_cursor_t* cur, int* out_has_item) {
    if (!cur || !out_has_item) return FT_ERR_INVALID_ARG;
    if (cur->next_index >= cur->n) {
        cur->has_current = 0;
        *out_has_item = 0;
        return FT_OK;
    }
    cur->current_index = cur->next_index;
    cur->next_index++;
    cur->has_current = 1;
    *out_has_item = 1;
    return FT_OK;
}

ft_status_t ft_cursor_key(const ft_cursor_t* cur, ft_blob_t* out_key) {
    if (!cur || !out_key) return FT_ERR_INVALID_ARG;
    if (!cur->has_current) return FT_ERR_NOT_FOUND;
    out_key->data = cur->items[cur->current_index].key;
    out_key->len = cur->items[cur->current_index].key_len;
    return FT_OK;
}

ft_status_t ft_cursor_val(const ft_cursor_t* cur, ft_blob_t* out_val) {
    if (!cur || !out_val) return FT_ERR_INVALID_ARG;
    if (!cur->has_current) return FT_ERR_NOT_FOUND;
    out_val->data = cur->items[cur->current_index].val;
    out_val->len = cur->items[cur->current_index].val_len;
    return FT_OK;
}

void ft_cursor_close(ft_cursor_t* cur) {
    if (!cur) return;
    for (size_t i = 0; i < cur->n; i++) kv_entry_free(&cur->items[i]);
    free(cur->items);
    if (cur->db && cur->db->open_cursors) {
        cur->db->open_cursors--;
    }
    free(cur);
}

ft_status_t ft_gc_step(ft_db_t* db, size_t budget_bytes) {
    (void)budget_bytes;
    if (!db) return FT_ERR_INVALID_ARG;
    if (db->open_cursors != 0) return FT_ERR_UNSUPPORTED;
    // No-op placeholder: real implementation will compact persisted pages.
    return FT_OK;
}

ft_status_t ft_tsdb_init(ft_tsdb_t** out_tsdb, ft_blockdev_t* bdev, const ft_tsdb_cfg_t* cfg) {
    (void)cfg;
    if (!out_tsdb || !bdev) return FT_ERR_INVALID_ARG;
    ft_tsdb_t* tsdb = (ft_tsdb_t*)calloc(1, sizeof(ft_tsdb_t));
    if (!tsdb) return FT_ERR_NO_MEMORY;
    tsdb->bdev = bdev;
    *out_tsdb = tsdb;
    return FT_OK;
}

void ft_tsdb_deinit(ft_tsdb_t* tsdb) {
    if (!tsdb) return;
    for (size_t i = 0; i < tsdb->n; i++) {
        free(tsdb->entries[i].data);
        tsdb->entries[i].data = NULL;
    }
    free(tsdb->entries);
    free(tsdb);
}

ft_status_t ft_tsl_append(ft_tsdb_t* tsdb, const void* data, size_t len, ft_time_t t) {
    if (!tsdb || (!data && len != 0)) return FT_ERR_INVALID_ARG;
    if (tsdb->n == tsdb->cap) {
        size_t new_cap = (tsdb->cap == 0) ? 16 : (tsdb->cap * 2);
        struct ts_entry* p = (struct ts_entry*)realloc(tsdb->entries, new_cap * sizeof(*p));
        if (!p) return FT_ERR_NO_MEMORY;
        tsdb->entries = p;
        tsdb->cap = new_cap;
    }
    uint8_t* copy = NULL;
    if (len) {
        copy = (uint8_t*)malloc(len);
        if (!copy) return FT_ERR_NO_MEMORY;
        memcpy(copy, data, len);
    }
    tsdb->entries[tsdb->n].t = t;
    tsdb->entries[tsdb->n].status = FT_TSL_STATUS_OK;
    tsdb->entries[tsdb->n].data = copy;
    tsdb->entries[tsdb->n].len = len;
    tsdb->n++;
    return FT_OK;
}

ft_status_t ft_tsl_iter_by_time(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_cb cb, void* arg) {
    if (!tsdb || !cb) return FT_ERR_INVALID_ARG;
    for (size_t i = 0; i < tsdb->n; i++) {
        ft_time_t t = tsdb->entries[i].t;
        if (t < from || t > to) continue;
        ft_status_t st = cb(t, tsdb->entries[i].data, tsdb->entries[i].len, tsdb->entries[i].status, arg);
        if (st != FT_OK) return st;
    }
    return FT_OK;
}

ft_status_t ft_tsl_query_count(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_status_t status, uint64_t* out_count) {
    if (!tsdb || !out_count) return FT_ERR_INVALID_ARG;
    uint64_t c = 0;
    for (size_t i = 0; i < tsdb->n; i++) {
        ft_time_t t = tsdb->entries[i].t;
        if (t < from || t > to) continue;
        if (tsdb->entries[i].status != status) continue;
        c++;
    }
    *out_count = c;
    return FT_OK;
}

ft_status_t ft_tsl_aggregate_f32(ft_tsdb_t* tsdb, ft_time_t from, ft_time_t to, ft_tsl_status_t status, ft_agg_f32_t* out) {
    if (!tsdb || !out) return FT_ERR_INVALID_ARG;
    uint64_t count = 0;
    float sum = 0.0f;
    float minv = 0.0f;
    float maxv = 0.0f;

    for (size_t i = 0; i < tsdb->n; i++) {
        ft_time_t t = tsdb->entries[i].t;
        if (t < from || t > to) continue;
        if (tsdb->entries[i].status != status) continue;
        if (tsdb->entries[i].len != sizeof(float)) return FT_ERR_INVALID_ARG;
        float v = 0.0f;
        memcpy(&v, tsdb->entries[i].data, sizeof(float));
        if (count == 0) {
            minv = v;
            maxv = v;
        } else {
            if (v < minv) minv = v;
            if (v > maxv) maxv = v;
        }
        sum += v;
        count++;
    }

    out->count = count;
    out->sum = sum;
    out->min = (count ? minv : 0.0f);
    out->max = (count ? maxv : 0.0f);
    out->avg = (count ? (sum / (float)count) : 0.0f);
    return FT_OK;
}

