#include "fs_layer.h"

#include "byte_array.h"
#include "instant.h"
#include "map.h"
#include "memory.h"
#include "ast_canon.h"
#include "parser.h"
#include "strings.h"
#include "to_string.h"
#include "value.h"
#include "vector.h"

#include "flash_tree.h"
#include "ft_blockdev.h"

#include <string.h>
#include <sys/time.h>

#define FS_CHUNK_SIZE 256u
#define FS_KEY_MAX 64u

typedef struct {
    uint8_t* buf;
    size_t len;
} FsRamBdev;

static ft_status_t fs_ram_read(void* ctx, uint32_t addr, void* out, size_t len) {
    FsRamBdev* r = (FsRamBdev*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memcpy(out, r->buf + addr, len);
    return FT_OK;
}

static ft_status_t fs_ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    FsRamBdev* r = (FsRamBdev*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    }
    return FT_OK;
}

static ft_status_t fs_ram_erase(void* ctx, uint32_t addr, size_t len) {
    FsRamBdev* r = (FsRamBdev*)ctx;
    if ((size_t)addr + len > r->len) return FT_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return FT_OK;
}

typedef struct {
    FsKvStore* st;
    EvalState* eval;
    const char* dir_path;
    size_t prefix_len;
    ID vec;
} FsListDirCtx;

static ft_status_t fs_list_dir_cb(const void* key, size_t key_len,
                                 const void* val, size_t val_len,
                                 void* arg) {
    (void)val;
    (void)val_len;
    FsListDirCtx* c = (FsListDirCtx*)arg;
    if (!c || !c->st || !c->eval || !c->dir_path) return FT_ERR_INVALID_ARG;
    if (key_len == c->prefix_len) return FT_OK; // skip dir itself
    if (key_len <= c->prefix_len) return FT_OK;

    // Keys are path strings; make a temporary NUL-terminated view for existing helpers.
    if (key_len >= FS_KEY_MAX) return FT_OK;
    char kstr[FS_KEY_MAX];
    memcpy(kstr, key, key_len);
    kstr[key_len] = '\0';

    const char* rest = kstr + c->prefix_len;
    if (!rest || rest[0] == '\0') return FT_OK;

    // Skip chunk keys (versioned "@v#NNNN").
    const char* at = strchr(rest, '@');
    if (at && strchr(at, '#')) return FT_OK;

    // Only direct children: allow at most one '/' at end.
    const char* slash = strchr(rest, '/');
    if (slash && slash[1] != '\0') return FT_OK;

    ID entry = fs_stat(c->st, c->eval, kstr);
    if (entry) {
        c->vec = (ID)vector_conj((CljVector*)c->vec, entry);
    }
    return FT_OK;
}

struct FsKvStore {
    ft_db_t* db;
    ft_blockdev_t bdev;
    FsRamBdev ram;
};

static FsKvStore *g_fs_global_store = NULL;

FsKvStore *fs_global_store(void)
{
    if (!g_fs_global_store) {
        g_fs_global_store = fs_kv_store_new();
    }
    return g_fs_global_store;
}

void fs_global_store_reset(void)
{
    if (g_fs_global_store) {
        fs_kv_store_free(g_fs_global_store);
        g_fs_global_store = NULL;
    }
}

/* -------------------------------------------------------------------------- */
/* KV store helpers                                                           */
/* -------------------------------------------------------------------------- */

static bool fs_kv_exists(FsKvStore *st, const char *key)
{
    if (!st || !st->db || !key) return false;
    ft_blob_t blob = {0};
    return ft_get(st->db, key, strlen(key), &blob) == FT_OK;
}

FsKvStore *fs_kv_store_new(void)
{
    FsKvStore *st = (FsKvStore *)malloc(sizeof(FsKvStore));
    if (!st) {
        throw_oom();
        return NULL;
    }
    memset(st, 0, sizeof(*st));

    // Host default: RAM-backed block device for flash-tree.
    // Note: flash-tree KV is currently in-memory; the bdev is an integration anchor.
    const size_t ram_bytes = 128 * 1024;
    st->ram.buf = (uint8_t*)malloc(ram_bytes);
    if (!st->ram.buf) {
        free(st);
        throw_oom();
        return NULL;
    }
    st->ram.len = ram_bytes;
    memset(st->ram.buf, 0xFF, ram_bytes);

    st->bdev.ctx = &st->ram;
    st->bdev.ops.read = fs_ram_read;
    st->bdev.ops.prog = fs_ram_prog;
    st->bdev.ops.erase = fs_ram_erase;
    st->bdev.geom.total_size_bytes = (uint32_t)ram_bytes;
    st->bdev.geom.read_granularity = 1;
    st->bdev.geom.prog_granularity = 1;
    st->bdev.geom.erase_granularity = 16;

    ft_status_t fst = ft_db_init(&st->db, &st->bdev, NULL);
    if (fst != FT_OK) {
        free(st->ram.buf);
        free(st);
        throw_exception(EXCEPTION_RUNTIME,
                        "fs_kv_store_new: flash-tree init failed",
                        __FILE__, __LINE__, 0);
        return NULL;
    }
    return st;
}

void fs_kv_store_free(FsKvStore *st)
{
    if (!st) return;
    if (st->db) {
        ft_db_deinit(st->db);
        st->db = NULL;
    }
    free(st->ram.buf);
    st->ram.buf = NULL;
    st->ram.len = 0;
    free(st);
}

bool fs_kv_put(FsKvStore *st, const char *key, const uint8_t *data, size_t len)
{
    return fs_kv_put_status(st, key, data, len) == FT_OK;
}

size_t fs_kv_get(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out)
{
    ft_status_t stc = fs_kv_get_status(st, key, out, out_len, saved_len_out);
    if (stc != FT_OK) return 0;
    if (!out || out_len == 0 || !saved_len_out) return 0;
    return (*saved_len_out < out_len) ? *saved_len_out : out_len;
}

bool fs_kv_del(FsKvStore *st, const char *key)
{
    if (!st || !key) return false;
    (void)fs_kv_del_status(st, key);
    return true;
}

ft_status_t fs_kv_put_status(FsKvStore *st, const char *key, const uint8_t *data, size_t len)
{
    if (!st || !st->db || !key) return FT_ERR_INVALID_ARG;
    if (len > (size_t)INT32_MAX) return FT_ERR_INVALID_ARG;
    return ft_put(st->db, key, strlen(key), data, len);
}

ft_status_t fs_kv_get_status(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out)
{
    if (saved_len_out) *saved_len_out = 0;
    if (!st || !st->db || !key) return FT_ERR_INVALID_ARG;
    size_t saved = 0;
    ft_status_t stc = ft_get_into(st->db, key, strlen(key), out, out_len, &saved);
    if (saved_len_out) *saved_len_out = saved;
    return stc;
}

ft_status_t fs_kv_del_status(FsKvStore *st, const char *key)
{
    if (!st || !st->db || !key) return FT_ERR_INVALID_ARG;
    return ft_del(st->db, key, strlen(key));
}

/* -------------------------------------------------------------------------- */
/* FS layer                                                                   */
/* -------------------------------------------------------------------------- */

static bool fs_is_valid_path(const char *path)
{
    return path && path[0] == '/';
}

static bool fs_is_dir_path(const char *path)
{
    if (!path) return false;
    size_t n = strlen(path);
    return n > 0 && path[n - 1] == '/';
}

static ID fs_now_instant(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    int32_t days = (int32_t)(tv.tv_sec / 86400);
    int32_t sec_in_day = (int32_t)(tv.tv_sec % 86400);
    int32_t millis = sec_in_day * 1000 + (int32_t)(tv.tv_usec / 1000);
    return AUTORELEASE(make_instant(days, (uint32_t)millis));
}

static bool fs_make_name(const char *path, char out[FS_KEY_MAX])
{
    if (!path || !out) return false;

    size_t n = strlen(path);
    if (n == 0) {
        out[0] = '\0';
        return true;
    }

    size_t end = n;
    if (end > 1 && path[end - 1] == '/') {
        end--;
    }

    size_t start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }

    size_t len = end - start;
    if (len >= FS_KEY_MAX) {
        return false;
    }
    memcpy(out, path + start, len);
    out[len] = '\0';
    return true;
}

static CljSymbol *kw(const char *name)
{
    return intern_symbol_global(name);
}

static ID fs_meta_get_map(FsKvStore *st, EvalState *eval, const char *path)
{
    if (!st || !path) return NULL;

    size_t saved = 0;
    size_t n = fs_kv_get(st, path, NULL, 0, &saved);
    (void)n;
    if (saved == 0) return NULL;

    uint8_t *buf = (uint8_t *)malloc(saved + 1);
    if (!buf) {
        throw_oom();
        return NULL;
    }
    size_t got = fs_kv_get(st, path, buf, saved, &saved);
    buf[got] = 0;

    ID parsed = parse((const char *)buf, eval);
    if (parsed) {
        parsed = canonicalize_ast(parsed, eval);
    }
    free(buf);
    return parsed;
}

static fs_err_t fs_meta_put_map(FsKvStore *st, const char *path, ID map_obj)
{
    if (!st || !path) return FS_ERR_IO;
    if (!map_obj || TAG(map_obj) != CLJ_MAP) return FS_ERR_TYPE;

    CljString *s = pr_str(map_obj);
    if (!s) return FS_ERR_OOM;

    const uint8_t *bytes = (const uint8_t *)string_data(s);
    size_t len = (size_t)string_length(s);
    bool ok = fs_kv_put(st, path, bytes, len);
    RELEASE(s);
    return ok ? FS_NO_ERR : FS_ERR_IO;
}

static uint32_t fs_meta_version(ID meta_map)
{
    if (!meta_map || TAG(meta_map) != CLJ_MAP) return 0;
    ID v = map_get(as_map(meta_map), (ID)kw(":version"));
    if (!v) return 0;
    if (is_fixnum((CljValue)v)) {
        int32_t vi = as_fixnum((CljValue)v);
        return vi > 0 ? (uint32_t)vi : 0;
    }
    return 0;
}

static size_t fs_meta_size(ID meta_map)
{
    if (!meta_map || TAG(meta_map) != CLJ_MAP) return 0;
    ID v = map_get(as_map(meta_map), (ID)kw(":size"));
    if (!v) return 0;
    if (is_fixnum((CljValue)v)) {
        int32_t vi = as_fixnum((CljValue)v);
        return vi >= 0 ? (size_t)vi : 0;
    }
    return 0;
}

static uint32_t fs_meta_chunks(ID meta_map)
{
    if (!meta_map || TAG(meta_map) != CLJ_MAP) return 0;
    ID v = map_get(as_map(meta_map), (ID)kw(":chunks"));
    if (!v) return 0;
    if (is_fixnum((CljValue)v)) {
        int32_t vi = as_fixnum((CljValue)v);
        return vi > 0 ? (uint32_t)vi : 0;
    }
    return 0;
}

static ID fs_meta_ctime(ID meta_map)
{
    if (!meta_map || TAG(meta_map) != CLJ_MAP) return NULL;
    ID v = map_get(as_map(meta_map), (ID)kw(":ctime"));
    return v;
}

static ID fs_meta_mtime(ID meta_map)
{
    if (!meta_map || TAG(meta_map) != CLJ_MAP) return NULL;
    ID v = map_get(as_map(meta_map), (ID)kw(":mtime"));
    return v ? v : fs_meta_ctime(meta_map);
}

static fs_err_t fs_make_chunk_key(char out[FS_KEY_MAX], const char *path, uint32_t version, uint32_t chunk_idx)
{
    int n = snprintf(out, FS_KEY_MAX, "%s@%u#%04u", path, version, chunk_idx);
    if (n <= 0 || (size_t)n >= FS_KEY_MAX) {
        return FS_ERR_INVALID_PATH;
    }
    return FS_NO_ERR;
}

fs_err_t fs_mkdir(FsKvStore *st, const char *dir_path, ID ctime_inst, ID mtime_inst)
{
    if (!fs_is_valid_path(dir_path) || !fs_is_dir_path(dir_path)) return FS_ERR_INVALID_PATH;
    if (strlen(dir_path) >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;
    if (!ctime_inst) ctime_inst = fs_now_instant();
    if (!mtime_inst) mtime_inst = ctime_inst;

    CljMap *m = make_map(4);
    m = map_assoc(m, (ID)kw(":type"), (ID)kw(":dir"));
    m = map_assoc(m, (ID)kw(":ctime"), ctime_inst);
    if (mtime_inst != ctime_inst) {
        m = map_assoc(m, (ID)kw(":mtime"), mtime_inst);
    }
    return fs_meta_put_map(st, dir_path, (ID)m);
}

fs_err_t fs_write_bytes(FsKvStore *st, EvalState *eval, const char *path, const uint8_t *data, size_t len)
{
    if (!fs_is_valid_path(path) || fs_is_dir_path(path)) return FS_ERR_INVALID_PATH;
    if (strlen(path) >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;
    if (!st || !eval) return FS_ERR_IO;

    ID old_meta = fs_meta_get_map(st, eval, path);
    uint32_t old_ver = fs_meta_version(old_meta);
    uint32_t new_ver = old_ver + 1;
    if (new_ver == 0) new_ver = 1;

    ID ctime = fs_meta_ctime(old_meta);
    if (!ctime) ctime = fs_now_instant();
    ID mtime = fs_now_instant();

    uint32_t chunks = (uint32_t)((len + FS_CHUNK_SIZE - 1) / FS_CHUNK_SIZE);
    if (chunks == 0) chunks = 1;

    /* Write chunk keys first (new version). */
    for (uint32_t i = 0; i < chunks; i++) {
        size_t off = (size_t)i * FS_CHUNK_SIZE;
        size_t remaining = len > off ? (len - off) : 0;
        size_t chunk_len = remaining > FS_CHUNK_SIZE ? FS_CHUNK_SIZE : remaining;

        char ckey[FS_KEY_MAX];
        fs_err_t e = fs_make_chunk_key(ckey, path, new_ver, i);
        if (e != FS_NO_ERR) return e;

        if (!fs_kv_put(st, ckey, data + off, chunk_len)) {
            return FS_ERR_IO;
        }
    }

    /* Commit meta last (makes the new version visible). */
    CljMap *m = make_map(8);
    m = map_assoc(m, (ID)kw(":type"), (ID)kw(":file"));
    m = map_assoc(m, (ID)kw(":version"), fixnum((int32_t)new_ver));
    m = map_assoc(m, (ID)kw(":size"), fixnum((int32_t)len));
    m = map_assoc(m, (ID)kw(":chunks"), fixnum((int32_t)chunks));
    m = map_assoc(m, (ID)kw(":ctime"), ctime);
    if (mtime != ctime) {
        m = map_assoc(m, (ID)kw(":mtime"), mtime);
    }
    return fs_meta_put_map(st, path, (ID)m);
}

ID fs_read_bytes(FsKvStore *st, EvalState *eval, const char *path)
{
    if (!fs_is_valid_path(path) || fs_is_dir_path(path)) return NULL;
    if (!st || !eval) return NULL;

    ID meta = fs_meta_get_map(st, eval, path);
    if (!meta || TAG(meta) != CLJ_MAP) return NULL;

    uint32_t ver = fs_meta_version(meta);
    uint32_t chunks = fs_meta_chunks(meta);
    size_t total = fs_meta_size(meta);

    ID arr = (ID)make_byte_array((int)total);
    if (!arr) return NULL;

    CljByteArray *ba = as_byte_array(arr);
    size_t copied = 0;
    for (uint32_t i = 0; i < chunks && copied < total; i++) {
        char ckey[FS_KEY_MAX];
        if (fs_make_chunk_key(ckey, path, ver, i) != FS_NO_ERR) break;

        size_t saved = 0;
        size_t want = total - copied;
        if (want > FS_CHUNK_SIZE) want = FS_CHUNK_SIZE;

        size_t got = fs_kv_get(st, ckey, ba->data + copied, want, &saved);
        copied += got;
    }

    return AUTORELEASE(arr);
}

ID fs_stat(FsKvStore *st, EvalState *eval, const char *path)
{
    if (!fs_is_valid_path(path)) return NULL;
    if (!st || !eval) return NULL;

    ID meta = fs_meta_get_map(st, eval, path);
    if (!meta || TAG(meta) != CLJ_MAP) return NULL;

    ID type = map_get(as_map(meta), (ID)kw(":type"));
    if (!type || TAG(type) != CLJ_SYMBOL) return NULL;

    char name_buf[FS_KEY_MAX];
    if (!fs_make_name(path, name_buf)) return NULL;

    CljMap *out = make_map(8);
    out = map_assoc(out, (ID)kw(":path"), (ID)make_string(path));
    out = map_assoc(out, (ID)kw(":name"), (ID)make_string(name_buf));
    out = map_assoc(out, (ID)kw(":type"), type);

    ID ctime = fs_meta_ctime(meta);
    ID mtime = fs_meta_mtime(meta);
    if (ctime) out = map_assoc(out, (ID)kw(":ctime"), ctime);
    if (mtime) out = map_assoc(out, (ID)kw(":mtime"), mtime);

    if (strcmp(as_symbol(type)->cname, ":file") == 0) {
        out = map_assoc(out, (ID)kw(":size"), fixnum((int32_t)fs_meta_size(meta)));
    }

    return AUTORELEASE(out);
}

bool fs_delete(FsKvStore *st, const char *path)
{
    if (!fs_is_valid_path(path)) return false;
    if (!st) return false;
    if (!fs_kv_exists(st, path)) return false;
    return fs_kv_del(st, path);
}

ID fs_list_dir(FsKvStore *st, EvalState *eval, const char *dir_path)
{
    if (!fs_is_valid_path(dir_path) || !fs_is_dir_path(dir_path)) return NULL;
    if (!st || !st->db || !eval) return NULL;

    size_t prefix_len = strlen(dir_path);
    ID vec = (ID)make_vector(8, CLJ_VECTOR);
    if (!vec) return NULL;

    FsListDirCtx ctx = {.st = st, .eval = eval, .dir_path = dir_path, .prefix_len = prefix_len, .vec = vec};
    (void)ft_iter_prefix(st->db, dir_path, prefix_len, fs_list_dir_cb, &ctx);
    return AUTORELEASE(ctx.vec);
}

