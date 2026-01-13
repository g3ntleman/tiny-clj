#include "flash_tree.h"

#include "ft_btree.h"
#include "ft_blockdev.h"

#include "flash-tree.h"

#include <stdlib.h>
#include <string.h>

// Single-threaded interleaving model:
// Cursor steps and write operations can interleave, but there is no concurrency.
// We keep the code structured so a future SWMR implementation can add real
// atomics/locks if desired.
typedef struct ft_spinlock {
    int v;
} ft_spinlock_t;

static void ft_spin_lock(ft_spinlock_t* l) {
    (void)l;
}

static void ft_spin_unlock(ft_spinlock_t* l) {
    (void)l;
}

typedef struct ft_index_entry {
    uint32_t refcnt;
    uint8_t* key;
    size_t key_len;
    uint8_t* val;
    size_t val_len;
} ft_index_entry_t;

typedef struct ft_index_root {
    uint32_t refcnt;
    ft_index_entry_t** e; // sorted by key (lex order)
    size_t n;
} ft_index_root_t;

static uint32_t ft_ref_inc_u32(uint32_t* p) {
    return ++(*p);
}

static uint32_t ft_ref_dec_u32(uint32_t* p) {
    return --(*p);
}

static ft_index_entry_t* ft_index_entry_new_copy(const void* key, size_t key_len,
                                                 const void* val, size_t val_len) {
    if ((!key && key_len != 0) || (!val && val_len != 0)) return NULL;
    ft_index_entry_t* e = (ft_index_entry_t*)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->refcnt = 1;
    e->key_len = key_len;
    e->val_len = val_len;
    if (key_len) {
        e->key = (uint8_t*)malloc(key_len);
        if (!e->key) { free(e); return NULL; }
        memcpy(e->key, key, key_len);
    }
    if (val_len) {
        e->val = (uint8_t*)malloc(val_len);
        if (!e->val) { free(e->key); free(e); return NULL; }
        memcpy(e->val, val, val_len);
    }
    return e;
}

static void ft_index_entry_retain(ft_index_entry_t* e) {
    if (!e) return;
    (void)ft_ref_inc_u32(&e->refcnt);
}

static void ft_index_entry_release(ft_index_entry_t* e) {
    if (!e) return;
    if (ft_ref_dec_u32(&e->refcnt) != 0) return;
    free(e->key);
    free(e->val);
    free(e);
}

static ft_index_root_t* ft_index_root_new(size_t n) {
    ft_index_root_t* r = (ft_index_root_t*)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->refcnt = 1;
    r->n = n;
    if (n) {
        r->e = (ft_index_entry_t**)calloc(n, sizeof(r->e[0]));
        if (!r->e) { free(r); return NULL; }
    }
    return r;
}

static void ft_index_root_retain(ft_index_root_t* r) {
    if (!r) return;
    (void)ft_ref_inc_u32(&r->refcnt);
}

static void ft_index_root_release(ft_index_root_t* r) {
    if (!r) return;
    if (ft_ref_dec_u32(&r->refcnt) != 0) return;
    for (size_t i = 0; i < r->n; i++) {
        ft_index_entry_release(r->e[i]);
    }
    free(r->e);
    free(r);
}

static int ft_key_has_prefix(const ft_index_entry_t* e, const void* prefix, size_t prefix_len) {
    if (prefix_len == 0) return 1;
    if (!e || e->key_len < prefix_len) return 0;
    return memcmp(e->key, prefix, prefix_len) == 0;
}

static size_t ft_index_lower_bound(const ft_index_root_t* r, const void* key, size_t key_len, int* out_equal) {
    if (out_equal) *out_equal = 0;
    if (!r || r->n == 0) return 0;
    size_t lo = 0;
    size_t hi = r->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        ft_index_entry_t* e = r->e[mid];
        int cmp = ft_lex_bytes_cmp(e->key, e->key_len, key, key_len);
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    if (out_equal && lo < r->n) {
        ft_index_entry_t* e = r->e[lo];
        *out_equal = (ft_lex_bytes_cmp(e->key, e->key_len, key, key_len) == 0);
    }
    return lo;
}

static int ft_index_entry_ptr_cmp_qsort(const void* a, const void* b) {
    const ft_index_entry_t* ea = *(const ft_index_entry_t* const*)a;
    const ft_index_entry_t* eb = *(const ft_index_entry_t* const*)b;
    return ft_lex_bytes_cmp(ea->key, ea->key_len, eb->key, eb->key_len);
}

typedef struct ft_entry_ptr_vec {
    ft_index_entry_t** v;
    size_t n;
    size_t cap;
} ft_entry_ptr_vec_t;

static ft_status_t ft_entry_ptr_vec_ensure(ft_entry_ptr_vec_t* vec, size_t need) {
    if (need <= vec->cap) return FT_OK;
    size_t new_cap = (vec->cap == 0) ? 8 : (vec->cap * 2);
    while (new_cap < need) new_cap *= 2;
    ft_index_entry_t** p = (ft_index_entry_t**)realloc(vec->v, new_cap * sizeof(vec->v[0]));
    if (!p) return FT_ERR_NO_MEMORY;
    vec->v = p;
    vec->cap = new_cap;
    return FT_OK;
}

static void ft_entry_ptr_vec_free(ft_entry_ptr_vec_t* vec) {
    if (!vec) return;
    for (size_t i = 0; i < vec->n; i++) ft_index_entry_release(vec->v[i]);
    free(vec->v);
    vec->v = NULL;
    vec->n = 0;
    vec->cap = 0;
}

static ft_index_root_t* ft_index_root_build_from_kvdb(ft_db_t* db, ft_status_t* out_st);

static ft_index_root_t* ft_index_root_put(const ft_index_root_t* old,
                                          const void* key, size_t key_len,
                                          const void* val, size_t val_len,
                                          ft_status_t* out_st) {
    if (out_st) *out_st = FT_OK;
    ft_index_entry_t* new_entry = ft_index_entry_new_copy(key, key_len, val, val_len);
    if (!new_entry) { if (out_st) *out_st = FT_ERR_NO_MEMORY; return NULL; }

    int equal = 0;
    size_t idx = ft_index_lower_bound(old, key, key_len, &equal);
    size_t old_n = old ? old->n : 0;
    size_t new_n = equal ? old_n : (old_n + 1);

    ft_index_root_t* r = ft_index_root_new(new_n);
    if (!r) { ft_index_entry_release(new_entry); if (out_st) *out_st = FT_ERR_NO_MEMORY; return NULL; }

    // Copy pointers with retain (persistent snapshot).
    if (old && old_n) {
        if (equal) {
            // [0..idx-1] + new + [idx+1..old_n-1]
            for (size_t i = 0; i < idx; i++) {
                ft_index_entry_t* src = old->e[i];
                ft_index_entry_retain(src);
                r->e[i] = src;
            }
            r->e[idx] = new_entry;
            for (size_t i = idx + 1; i < old_n; i++) {
                ft_index_entry_t* src = old->e[i];
                ft_index_entry_retain(src);
                r->e[i] = src;
            }
        } else {
            // [0..idx-1] + new + [idx..old_n-1] shifted right
            for (size_t i = 0; i < idx; i++) {
                ft_index_entry_t* src = old->e[i];
                ft_index_entry_retain(src);
                r->e[i] = src;
            }
            r->e[idx] = new_entry;
            for (size_t i = idx; i < old_n; i++) {
                ft_index_entry_t* src = old->e[i];
                ft_index_entry_retain(src);
                r->e[i + 1] = src;
            }
        }
    } else {
        // Empty old root.
        r->e[0] = new_entry;
    }
    return r;
}

static ft_index_root_t* ft_index_root_del(const ft_index_root_t* old,
                                          const void* key, size_t key_len,
                                          ft_status_t* out_st) {
    if (out_st) *out_st = FT_OK;
    if (!old || old->n == 0) { if (out_st) *out_st = FT_ERR_NOT_FOUND; return NULL; }
    int equal = 0;
    size_t idx = ft_index_lower_bound(old, key, key_len, &equal);
    if (!equal) { if (out_st) *out_st = FT_ERR_NOT_FOUND; return NULL; }

    ft_index_root_t* r = ft_index_root_new(old->n - 1);
    if (!r) { if (out_st) *out_st = FT_ERR_NO_MEMORY; return NULL; }

    for (size_t i = 0, j = 0; i < old->n; i++) {
        if (i == idx) continue;
        ft_index_entry_t* src = old->e[i];
        ft_index_entry_retain(src);
        r->e[j++] = src;
    }
    return r;
}

struct ft_db {
    ft_blockdev_t* bdev;
    struct fdb_kvdb kvdb;
    size_t open_cursors;
    ft_spinlock_t root_lock;
    ft_index_root_t* root;

    /* Scratch backing for ft_get (stable until next ft_get/ft_db_deinit). */
    uint8_t* get_buf;
    size_t get_cap;
};

struct ft_cursor {
    ft_db_t* db;
    size_t next_index;
    size_t current_index;
    int has_current;
    ft_index_root_t* root; // pinned snapshot root
    uint8_t* prefix;       // owned copy (or NULL if prefix_len==0)
    size_t prefix_len;
};

void ft_cursor_close(ft_cursor_t* cur);

static ft_status_t ft_from_fdb_err(fdb_err_t e) {
    switch (e) {
        case FDB_NO_ERR: return FT_OK;
        /* FlashDB uses FDB_KV_NAME_ERR both for invalid names and "not found". We
         * pre-validate arguments in Flash-Tree, so treat it as NOT_FOUND here. */
        case FDB_KV_NAME_ERR: return FT_ERR_NOT_FOUND;
        case FDB_INIT_FAILED: return FT_ERR_INVALID_ARG;
        case FDB_READ_ERR:
        case FDB_WRITE_ERR:
        case FDB_ERASE_ERR: return FT_ERR_IO;
        case FDB_SAVED_FULL: return FT_ERR_UNSUPPORTED;
        default: return FT_ERR_IO;
    }
}

static ft_index_root_t* ft_index_root_build_from_kvdb(ft_db_t* db, ft_status_t* out_st) {
    if (out_st) *out_st = FT_OK;
    if (!db) { if (out_st) *out_st = FT_ERR_INVALID_ARG; return NULL; }

    ft_entry_ptr_vec_t vec = {0};
    struct fdb_kv_iterator it;
    fdb_kv_iterator_init(&db->kvdb, &it);

    while (fdb_kv_iterate(&db->kvdb, &it)) {
        struct fdb_kv* kv = &it.curr_kv;
        if (!kv->crc_is_ok || kv->status != FDB_KV_WRITE) continue;

        // Read value bytes so cursor snapshots are stable (independent of FlashDB GC).
        uint8_t* val_copy = NULL;
        if (kv->value_len) {
            val_copy = (uint8_t*)malloc(kv->value_len);
            if (!val_copy) { if (out_st) *out_st = FT_ERR_NO_MEMORY; ft_entry_ptr_vec_free(&vec); return NULL; }
            struct fdb_blob blob;
            fdb_blob_make(&blob, val_copy, kv->value_len);
            size_t nread = fdb_blob_read((fdb_db_t)&db->kvdb, fdb_kv_to_blob(kv, &blob));
            if (nread != kv->value_len) {
                free(val_copy);
                if (out_st) *out_st = FT_ERR_IO;
                ft_entry_ptr_vec_free(&vec);
                return NULL;
            }
        }

        ft_index_entry_t* e = ft_index_entry_new_copy(kv->name, kv->name_len, val_copy, kv->value_len);
        free(val_copy);
        if (!e) { if (out_st) *out_st = FT_ERR_NO_MEMORY; ft_entry_ptr_vec_free(&vec); return NULL; }

        ft_status_t st = ft_entry_ptr_vec_ensure(&vec, vec.n + 1);
        if (st != FT_OK) { if (out_st) *out_st = st; ft_index_entry_release(e); ft_entry_ptr_vec_free(&vec); return NULL; }
        vec.v[vec.n++] = e;
    }

    if (vec.n) qsort(vec.v, vec.n, sizeof(vec.v[0]), ft_index_entry_ptr_cmp_qsort);

    ft_index_root_t* r = ft_index_root_new(vec.n);
    if (!r) { if (out_st) *out_st = FT_ERR_NO_MEMORY; ft_entry_ptr_vec_free(&vec); return NULL; }

    // Transfer ownership of entries to root (root holds initial refs).
    for (size_t i = 0; i < vec.n; i++) {
        r->e[i] = vec.v[i];
        vec.v[i] = NULL;
    }
    free(vec.v);
    vec.v = NULL;
    vec.n = 0;
    vec.cap = 0;

    return r;
}

ft_status_t ft_db_init(ft_db_t** out_db, ft_blockdev_t* bdev, const ft_cfg_t* cfg) {
    (void)cfg;
    if (!out_db || !bdev) return FT_ERR_INVALID_ARG;
    ft_status_t st = ft_blockdev_validate(bdev);
    if (st != FT_OK) return st;

    ft_db_t* db = (ft_db_t*)calloc(1, sizeof(ft_db_t));
    if (!db) return FT_ERR_NO_MEMORY;
    db->bdev = bdev;

    fdb_err_t fe = fdb_kvdb_init(&db->kvdb, "ft_kv", "ft_blockdev", NULL, bdev);
    if (fe != FDB_NO_ERR) { free(db); return ft_from_fdb_err(fe); }

    // Build in-memory immutable index root from persisted FlashDB contents.
    ft_status_t bst = FT_OK;
    db->root = ft_index_root_build_from_kvdb(db, &bst);
    if (bst != FT_OK) {
        (void)fdb_kvdb_deinit(&db->kvdb);
        free(db);
        return bst;
    }

    *out_db = db;
    return FT_OK;
}

void ft_db_deinit(ft_db_t* db) {
    if (!db) return;
    (void)fdb_kvdb_deinit(&db->kvdb);
    ft_index_root_release(db->root);
    free(db->get_buf);
    free(db);
}

ft_status_t ft_put(ft_db_t* db, const void* key, size_t key_len, const void* val, size_t val_len) {
    if (!db || (!key && key_len != 0) || (!val && val_len != 0)) return FT_ERR_INVALID_ARG;
    ft_spin_lock(&db->root_lock);
    struct fdb_blob blob;
    fdb_blob_make(&blob, val, val_len);
    ft_status_t st = ft_from_fdb_err(fdb_kv_set_blob_ex(&db->kvdb, key, key_len, &blob));
    if (st == FT_OK) {
        ft_status_t ist = FT_OK;
        ft_index_root_t* new_root = ft_index_root_put(db->root, key, key_len, val, val_len, &ist);
        if (!new_root) {
            ft_spin_unlock(&db->root_lock);
            return ist;
        }
        ft_index_root_t* old = db->root;
        db->root = new_root;
        ft_spin_unlock(&db->root_lock);
        ft_index_root_release(old);
        return FT_OK;
    }
    ft_spin_unlock(&db->root_lock);
    return st;
}

ft_status_t ft_get(ft_db_t* db, const void* key, size_t key_len, ft_blob_t* out) {
    if (!db || (!key && key_len != 0) || !out) return FT_ERR_INVALID_ARG;

    ft_spin_lock(&db->root_lock);
    int equal = 0;
    size_t idx = ft_index_lower_bound(db->root, key, key_len, &equal);
    if (!equal) { ft_spin_unlock(&db->root_lock); return FT_ERR_NOT_FOUND; }
    ft_index_entry_t* e = db->root->e[idx];
    if (e->val_len > db->get_cap) {
        size_t new_cap = (db->get_cap == 0) ? 64 : db->get_cap;
        while (new_cap < e->val_len) new_cap *= 2;
        uint8_t* p = (uint8_t*)realloc(db->get_buf, new_cap);
        if (!p) { ft_spin_unlock(&db->root_lock); return FT_ERR_NO_MEMORY; }
        db->get_buf = p;
        db->get_cap = new_cap;
    }
    if (e->val_len) memcpy(db->get_buf, e->val, e->val_len);
    out->data = db->get_buf;
    out->len = e->val_len;
    ft_spin_unlock(&db->root_lock);
    return FT_OK;
}

ft_status_t ft_get_len(ft_db_t* db, const void* key, size_t key_len, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!db || (!key && key_len != 0) || !out_len) return FT_ERR_INVALID_ARG;
    ft_spin_lock(&db->root_lock);
    int equal = 0;
    size_t idx = ft_index_lower_bound(db->root, key, key_len, &equal);
    if (!equal) { ft_spin_unlock(&db->root_lock); return FT_ERR_NOT_FOUND; }
    *out_len = db->root->e[idx]->val_len;
    ft_spin_unlock(&db->root_lock);
    return FT_OK;
}

ft_status_t ft_get_into(ft_db_t* db, const void* key, size_t key_len, void* out, size_t out_len, size_t* saved_len_out) {
    if (saved_len_out) *saved_len_out = 0;
    if (!db || (!key && key_len != 0)) return FT_ERR_INVALID_ARG;
    ft_spin_lock(&db->root_lock);
    int equal = 0;
    size_t idx = ft_index_lower_bound(db->root, key, key_len, &equal);
    if (!equal) { ft_spin_unlock(&db->root_lock); return FT_ERR_NOT_FOUND; }
    ft_index_entry_t* e = db->root->e[idx];
    if (saved_len_out) *saved_len_out = e->val_len;
    if (!out || out_len == 0) { ft_spin_unlock(&db->root_lock); return FT_OK; }
    size_t to_copy = (e->val_len < out_len) ? e->val_len : out_len;
    if (to_copy) memcpy(out, e->val, to_copy);
    ft_spin_unlock(&db->root_lock);
    return FT_OK;
}

ft_status_t ft_del(ft_db_t* db, const void* key, size_t key_len) {
    if (!db || (!key && key_len != 0)) return FT_ERR_INVALID_ARG;
    ft_spin_lock(&db->root_lock);
    ft_status_t st = ft_from_fdb_err(fdb_kv_del_ex(&db->kvdb, key, key_len));
    if (st == FT_OK) {
        ft_status_t ist = FT_OK;
        ft_index_root_t* new_root = ft_index_root_del(db->root, key, key_len, &ist);
        if (!new_root) { ft_spin_unlock(&db->root_lock); return ist; }
        ft_index_root_t* old = db->root;
        db->root = new_root;
        ft_spin_unlock(&db->root_lock);
        ft_index_root_release(old);
        return FT_OK;
    }
    ft_spin_unlock(&db->root_lock);
    return st;
}

ft_status_t ft_iter_prefix(ft_db_t* db, const void* prefix, size_t prefix_len, ft_key_cb cb, void* arg) {
    if (!db) return FT_ERR_INVALID_ARG;
    if ((!prefix && prefix_len != 0) || !cb) return FT_ERR_INVALID_ARG;
    ft_cursor_t* cur = NULL;
    ft_status_t st = ft_cursor_open_prefix(db, prefix, prefix_len, &cur);
    if (st != FT_OK) return st;
    int has = 0;
    ft_blob_t k = {0}, v = {0};
    while ((st = ft_cursor_next(cur, &has)) == FT_OK && has) {
        st = ft_cursor_key(cur, &k);
        if (st != FT_OK) break;
        st = ft_cursor_val(cur, &v);
        if (st != FT_OK) break;
        st = cb(k.data, k.len, v.data, v.len, arg);
        if (st != FT_OK) break;
    }
    ft_cursor_close(cur);
    return st;
}

ft_status_t ft_cursor_open_prefix(ft_db_t* db, const void* prefix, size_t prefix_len, ft_cursor_t** out_cur) {
    if (!db || (!prefix && prefix_len != 0) || !out_cur) return FT_ERR_INVALID_ARG;
    ft_cursor_t* cur = (ft_cursor_t*)calloc(1, sizeof(*cur));
    if (!cur) return FT_ERR_NO_MEMORY;

    ft_spin_lock(&db->root_lock);
    ft_index_root_retain(db->root);
    db->open_cursors++;
    cur->root = db->root;
    ft_spin_unlock(&db->root_lock);

    cur->db = db;
    cur->next_index = 0;
    cur->current_index = 0;
    cur->has_current = 0;

    // Persist prefix bytes: the caller's pointer might not live beyond this call.
    if (prefix_len) {
        cur->prefix = (uint8_t*)malloc(prefix_len);
        if (!cur->prefix) {
            ft_spin_lock(&db->root_lock);
            db->open_cursors--;
            ft_spin_unlock(&db->root_lock);
            ft_index_root_release(cur->root);
            free(cur);
            return FT_ERR_NO_MEMORY;
        }
        memcpy(cur->prefix, prefix, prefix_len);
        cur->prefix_len = prefix_len;
    }

    // Find first candidate; stopping is lazy in ft_cursor_next.
    if (cur->prefix_len) {
        cur->next_index = ft_index_lower_bound(cur->root, cur->prefix, cur->prefix_len, NULL);
    } else {
        cur->next_index = 0;
    }

    *out_cur = cur;
    return FT_OK;
}

ft_status_t ft_cursor_next(ft_cursor_t* cur, int* out_has_item) {
    if (!cur || !out_has_item) return FT_ERR_INVALID_ARG;
    if (cur->next_index >= cur->root->n) {
        cur->has_current = 0;
        *out_has_item = 0;
        return FT_OK;
    }
    if (cur->prefix_len && !ft_key_has_prefix(cur->root->e[cur->next_index], cur->prefix, cur->prefix_len)) {
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
    out_key->data = cur->root->e[cur->current_index]->key;
    out_key->len = cur->root->e[cur->current_index]->key_len;
    return FT_OK;
}

ft_status_t ft_cursor_val(const ft_cursor_t* cur, ft_blob_t* out_val) {
    if (!cur || !out_val) return FT_ERR_INVALID_ARG;
    if (!cur->has_current) return FT_ERR_NOT_FOUND;
    out_val->data = cur->root->e[cur->current_index]->val;
    out_val->len = cur->root->e[cur->current_index]->val_len;
    return FT_OK;
}

void ft_cursor_close(ft_cursor_t* cur) {
    if (!cur) return;
    if (cur->db && cur->db->open_cursors) {
        ft_spin_lock(&cur->db->root_lock);
        cur->db->open_cursors--;
        ft_spin_unlock(&cur->db->root_lock);
    }
    ft_index_root_release(cur->root);
    free(cur->prefix);
    free(cur);
}

ft_status_t ft_gc_step(ft_db_t* db, size_t budget_bytes) {
    if (!db) return FT_ERR_INVALID_ARG;
    /* Generational model:
     * - Cursors pin an immutable in-memory root (refcounted).
     * - GC compacts FlashDB storage and then we rebuild a fresh root generation.
     * - Active cursors continue using their pinned generation. */
    ft_spin_lock(&db->root_lock);

    ft_status_t st = ft_from_fdb_err(fdb_kvdb_gc_ex(&db->kvdb, budget_bytes));
    if (st != FT_OK) { ft_spin_unlock(&db->root_lock); return st; }

    ft_status_t bst = FT_OK;
    ft_index_root_t* new_root = ft_index_root_build_from_kvdb(db, &bst);
    if (!new_root) { ft_spin_unlock(&db->root_lock); return bst; }

    ft_index_root_t* old = db->root;
    db->root = new_root;
    ft_spin_unlock(&db->root_lock);

    /* Old generation might still be pinned by cursors. */
    ft_index_root_release(old);
    return FT_OK;
}

// (TSDB functions are implemented in ft_tsdb_flashdb.c)

