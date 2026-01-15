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

#define FS_APP_MAX_CHUNK_SIZE 4096u
// Internal storage chunk size. This is NOT an app-facing limit.
// App-facing streaming APIs (future) must cap chunks to FS_APP_MAX_CHUNK_SIZE.
#define FS_STORE_CHUNK_SIZE 4096u
#define FS_KEY_MAX 64u

// Forward declarations for FS helpers used by streaming APIs.
static bool fs_is_valid_path(const char *path);
static bool fs_is_dir_path(const char *path);
static ID fs_now_instant(void);
static CljSymbol *kw(const char *name);
static ID fs_meta_get_map(FsKvStore *st, EvalState *eval, const char *path);
static uint32_t fs_meta_version(ID meta_map);
static size_t fs_meta_size(ID meta_map);
static uint32_t fs_meta_chunks(ID meta_map);
static ID fs_meta_ctime(ID meta_map);
static fs_err_t fs_meta_put_map(FsKvStore *st, const char *path, ID map_obj);
static fs_err_t fs_make_chunk_key(char out[FS_KEY_MAX], const char *path, uint32_t version, uint32_t chunk_idx);

// Lightweight integer formatting helpers (avoid snprintf/printf dependencies).
static size_t fs_u32_to_dec_rev(uint32_t v, uint8_t out_rev[10])
{
    size_t n = 0;
    do {
        out_rev[n++] = (uint8_t)('0' + (v % 10u));
        v /= 10u;
    } while (v && n < 10);
    return n;
}

static size_t fs_append_u32_dec(uint8_t* out, size_t pos, size_t cap, uint32_t v)
{
    uint8_t rev[10];
    size_t n = fs_u32_to_dec_rev(v, rev);
    if (pos + n >= cap) return cap + 1;
    for (size_t i = 0; i < n; i++) {
        out[pos + i] = rev[n - 1 - i];
    }
    return pos + n;
}

static size_t fs_append_u32_dec_zeropad(uint8_t* out, size_t pos, size_t cap, uint32_t v, size_t width)
{
    uint8_t rev[10];
    size_t n = fs_u32_to_dec_rev(v, rev);
    if (n < width) {
        size_t pad = width - n;
        if (pos + pad >= cap) return cap + 1;
        for (size_t i = 0; i < pad; i++) out[pos++] = '0';
    }
    if (pos + n >= cap) return cap + 1;
    for (size_t i = 0; i < n; i++) out[pos + i] = rev[n - 1 - i];
    return pos + n;
}

// -----------------------------------------------------------------------------
// tinyclj.kv long-value storage (chunked) using blob keys (no C-strings).
// -----------------------------------------------------------------------------
#define FS_KV_META_MAGIC 0x4D564B46u /* 'F''K''V''M' */
#define FS_KV_CHUNK_KEY_SUFFIX_MAX 9u
#define FS_KV_KEY_MAX 1024u
#define FS_KV_CHUNK_TAG 0x01u

static void fs_write_be32(uint8_t out[4], uint32_t v)
{
    out[0] = (uint8_t)((v >> 24) & 0xFFu);
    out[1] = (uint8_t)((v >> 16) & 0xFFu);
    out[2] = (uint8_t)((v >> 8) & 0xFFu);
    out[3] = (uint8_t)(v & 0xFFu);
}

typedef struct __attribute__((packed)) FsKvMeta {
    uint32_t magic;
    uint32_t version;
    uint32_t total_len;
    uint32_t chunks;
} FsKvMeta;

static ft_status_t fs_kv_make_chunk_key_bytes(const uint8_t* key, size_t key_len,
                                              uint32_t version, uint32_t chunk_idx,
                                              uint8_t* out, size_t out_cap, size_t* out_len)
{
    if (out_len) *out_len = 0;
    if (!key || key_len == 0 || !out || !out_len) return FT_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return FT_ERR_INVALID_ARG;

    // Chunk key: K || tag(1B) || ver_be32 || idx_be32
    const size_t need = key_len + 1 + 4 + 4;
    if (need > out_cap) return FT_ERR_INVALID_ARG;
    size_t pos = 0;
    memcpy(out + pos, key, key_len);
    pos += key_len;
    out[pos++] = (uint8_t)FS_KV_CHUNK_TAG;
    fs_write_be32(out + pos, version);
    pos += 4;
    fs_write_be32(out + pos, chunk_idx);
    pos += 4;
    *out_len = pos;
    return FT_OK;
}

static ft_status_t fs_kv_read_meta_bytes(ft_db_t* db, const uint8_t* key, size_t key_len,
                                        FsKvMeta* out_meta, int* out_has_meta)
{
    if (out_has_meta) *out_has_meta = 0;
    if (!db || !key || key_len == 0 || !out_meta || !out_has_meta) return FT_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    size_t saved = 0;
    ft_status_t stc = ft_get_into(db, key, key_len, &meta, sizeof(meta), &saved);
    if (stc != FT_OK) return stc;

    if (saved == sizeof(meta) && meta.magic == FS_KV_META_MAGIC) {
        *out_meta = meta;
        *out_has_meta = 1;
    }
    return FT_OK;
}

static ft_status_t fs_kv_put_chunked_bytes(ft_db_t* db, const uint8_t* key, size_t key_len,
                                          const uint8_t* data, size_t len)
{
    if (!db || !key || key_len == 0 || (!data && len != 0)) return FT_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return FT_ERR_INVALID_ARG;
    if (len > UINT32_MAX) return FT_ERR_INVALID_ARG;

    FsKvMeta old_meta = {0};
    int has_meta = 0;
    ft_status_t stc = fs_kv_read_meta_bytes(db, key, key_len, &old_meta, &has_meta);
    if (stc != FT_OK && stc != FT_ERR_NOT_FOUND) return stc;

    uint32_t new_ver = has_meta ? (old_meta.version + 1u) : 1u;
    if (new_ver == 0) new_ver = 1u;

    uint32_t chunks = (len == 0) ? 0u : (uint32_t)((len + FS_STORE_CHUNK_SIZE - 1) / FS_STORE_CHUNK_SIZE);
    uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
    size_t ckey_len = 0;

    for (uint32_t i = 0; i < chunks; i++) {
        size_t off = (size_t)i * FS_STORE_CHUNK_SIZE;
        size_t remaining = len - off;
        size_t chunk_len = remaining > FS_STORE_CHUNK_SIZE ? FS_STORE_CHUNK_SIZE : remaining;

        stc = fs_kv_make_chunk_key_bytes(key, key_len, new_ver, i, ckey, sizeof(ckey), &ckey_len);
        if (stc != FT_OK) return stc;

        stc = ft_put(db, ckey, ckey_len, data + off, chunk_len);
        if (stc != FT_OK) return stc;
    }

    FsKvMeta meta = {
        .magic = FS_KV_META_MAGIC,
        .version = new_ver,
        .total_len = (uint32_t)len,
        .chunks = chunks,
    };
    return ft_put(db, key, key_len, &meta, sizeof(meta));
}

static ft_status_t fs_kv_get_chunked_bytes(ft_db_t* db, const uint8_t* key, size_t key_len,
                                          uint8_t* out, size_t out_len, size_t* saved_len_out)
{
    if (saved_len_out) *saved_len_out = 0;
    if (!db || !key || key_len == 0) return FT_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return FT_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    ft_status_t stc = fs_kv_read_meta_bytes(db, key, key_len, &meta, &has_meta);
    if (stc != FT_OK) return stc;
    if (!has_meta) return FT_ERR_CORRUPT;

    if (saved_len_out) *saved_len_out = (size_t)meta.total_len;
    if (!out || out_len == 0 || meta.total_len == 0) return FT_OK;

    size_t want_total = (size_t)meta.total_len;
    size_t copied = 0;
    uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
    size_t ckey_len = 0;

    for (uint32_t i = 0; i < meta.chunks && copied < want_total; i++) {
        stc = fs_kv_make_chunk_key_bytes(key, key_len, meta.version, i, ckey, sizeof(ckey), &ckey_len);
        if (stc != FT_OK) return stc;

        size_t remaining = want_total - copied;
        size_t want = remaining > FS_STORE_CHUNK_SIZE ? FS_STORE_CHUNK_SIZE : remaining;
        if (want > out_len - copied) want = out_len - copied;

        size_t saved_chunk = 0;
        stc = ft_get_into(db, ckey, ckey_len, out + copied, want, &saved_chunk);
        if (stc != FT_OK) return stc;
        if (saved_chunk < want && copied + saved_chunk < want_total) return FT_ERR_CORRUPT;
        copied += (saved_chunk < want ? saved_chunk : want);
    }

    return FT_OK;
}

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
    FsStreamStats stats;
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
    size_t saved = 0;
    return fs_kv_get_status(st, key, NULL, 0, &saved) == FT_OK;
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
    // flash-tree stores B-Tree pages in erase-sized log records (must be power-of-two).
    // To reliably fit 4KB chunk values inline (no overflow pages), use a larger logical erase
    // granularity than the chunk size.
    st->bdev.geom.erase_granularity = 16384;

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

/* -------------------------------------------------------------------------- */
/* Streaming stats (resettable)                                               */
/* -------------------------------------------------------------------------- */

ft_status_t fs_stream_stats_reset(FsKvStore* st)
{
    if (!st) return FT_ERR_INVALID_ARG;
    st->stats.blocks_read = 0;
    st->stats.blocks_written = 0;
    return FT_OK;
}

ft_status_t fs_stream_stats_get(const FsKvStore* st, FsStreamStats* out)
{
    if (!st || !out) return FT_ERR_INVALID_ARG;
    *out = st->stats;
    return FT_OK;
}

static inline size_t fs_clamp_app_chunk(size_t max_chunk)
{
    if (max_chunk == 0) return 0;
    if (max_chunk > (size_t)FS_APP_MAX_CHUNK_SIZE) return (size_t)FS_APP_MAX_CHUNK_SIZE;
    return max_chunk;
}

static ft_status_t fs_emit_sliced(const uint8_t* data, size_t len,
                                 size_t max_chunk,
                                 fs_stream_sink_cb cb, void* arg)
{
    if (!cb) return FT_ERR_INVALID_ARG;
    if (!data && len != 0) return FT_ERR_INVALID_ARG;
    if (len == 0) return FT_OK;

    size_t pos = 0;
    while (pos < len) {
        size_t n = len - pos;
        if (n > max_chunk) n = max_chunk;
        ft_status_t st = cb(data + pos, n, arg);
        if (st != FT_OK) return st;
        pos += n;
    }
    return FT_OK;
}

static int fs_parse_u32_field(const char* s, const char* needle, uint32_t* out)
{
    if (!s || !needle || !out) return 0;
    const char* p = strstr(s, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '-' || *p == '\0') return 0;
    uint32_t v = 0;
    int any = 0;
    while (*p >= '0' && *p <= '9') {
        any = 1;
        uint32_t d = (uint32_t)(*p - '0');
        if (v > (UINT32_MAX - d) / 10u) return 0;
        v = v * 10u + d;
        p++;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Streaming read/write (KV + FS)                                             */
/* -------------------------------------------------------------------------- */

ft_status_t fs_kv_stream_read_key_bytes(FsKvStore* st,
                                        const uint8_t* key, size_t key_len,
                                        size_t max_chunk,
                                        fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !key || key_len == 0 || !cb) return FT_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return FT_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    ft_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc != FT_OK) return stc;

    if (has_meta) {
        // Chunked value: enumerate chunk keys by prefix cursor (Option 3).
        // Prefix: K || tag || ver_be32
        uint8_t prefix[FS_KV_KEY_MAX + 1 + 4];
        if (key_len + 1 + 4 > sizeof(prefix)) return FT_ERR_INVALID_ARG;
        size_t pfx_len = 0;
        memcpy(prefix, key, key_len);
        pfx_len += key_len;
        prefix[pfx_len++] = (uint8_t)FS_KV_CHUNK_TAG;
        fs_write_be32(prefix + pfx_len, meta.version);
        pfx_len += 4;

        ft_cursor_t* cur = NULL;
        stc = ft_cursor_open_prefix(st->db, prefix, pfx_len, &cur);
        if (stc != FT_OK) return stc;

        size_t remaining_total = (size_t)meta.total_len;
        uint32_t seen_chunks = 0;
        while (seen_chunks < meta.chunks) {
            int has = 0;
            stc = ft_cursor_next(cur, &has);
            if (stc != FT_OK) { ft_cursor_close(cur); return stc; }
            if (!has) break;

            ft_blob_t v = {0};
            stc = ft_cursor_val(cur, &v);
            if (stc != FT_OK) { ft_cursor_close(cur); return stc; }

            if (v.len > (size_t)FS_STORE_CHUNK_SIZE) { ft_cursor_close(cur); return FT_ERR_CORRUPT; }
            if (v.len > remaining_total) { ft_cursor_close(cur); return FT_ERR_CORRUPT; }

            st->stats.blocks_read++;
            if (v.len) {
                stc = fs_emit_sliced((const uint8_t*)v.data, v.len, chunk_cap, cb, arg);
                if (stc != FT_OK) { ft_cursor_close(cur); return stc; }
            }
            remaining_total -= v.len;
            seen_chunks++;
            if (remaining_total == 0) break;
        }

        ft_cursor_close(cur);
        if (seen_chunks != meta.chunks) return FT_ERR_CORRUPT;
        if (remaining_total != 0) return FT_ERR_CORRUPT;
        return FT_OK;
    }

    // Legacy inline value: emit it in <= max_chunk slices.
    ft_blob_t out = {0};
    stc = ft_get(st->db, key, key_len, &out);
    if (stc != FT_OK) return stc;
    st->stats.blocks_read++; // single stored value
    return fs_emit_sliced((const uint8_t*)out.data, out.len, chunk_cap, cb, arg);
}

ft_status_t fs_kv_stream_read_key_bytes_from(FsKvStore* st,
                                             const uint8_t* key, size_t key_len,
                                             size_t offset,
                                             size_t max_chunk,
                                             fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !key || key_len == 0 || !cb) return FT_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return FT_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    ft_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc != FT_OK) return stc;

    if (has_meta) {
        const size_t total_len = (size_t)meta.total_len;
        if (offset > total_len) return FT_ERR_INVALID_ARG;
        if (offset == total_len) return FT_OK;

        const uint32_t start_idx = (uint32_t)(offset / (size_t)FS_STORE_CHUNK_SIZE);
        size_t start_off = offset % (size_t)FS_STORE_CHUNK_SIZE;
        size_t remaining_total = total_len - offset;

        uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
        size_t ckey_len = 0;
        uint8_t buf[FS_STORE_CHUNK_SIZE];

        for (uint32_t i = start_idx; i < meta.chunks && remaining_total > 0; i++) {
            stc = fs_kv_make_chunk_key_bytes(key, key_len, meta.version, i, ckey, sizeof(ckey), &ckey_len);
            if (stc != FT_OK) return stc;

            size_t saved = 0;
            stc = ft_get_into(st->db, ckey, ckey_len, buf, sizeof(buf), &saved);
            if (stc != FT_OK) return stc;
            if (saved == 0) return FT_ERR_CORRUPT;
            if (start_off > saved) return FT_ERR_INVALID_ARG;

            size_t avail = saved - start_off;
            size_t take = (remaining_total < avail) ? remaining_total : avail;
            if (take) {
                st->stats.blocks_read++;
                stc = fs_emit_sliced(buf + start_off, take, chunk_cap, cb, arg);
                if (stc != FT_OK) return stc;
                remaining_total -= take;
            } else {
                st->stats.blocks_read++;
            }
            start_off = 0;
        }

        if (remaining_total != 0) return FT_ERR_CORRUPT;
        return FT_OK;
    }

    // Legacy inline: offset into single stored value.
    ft_blob_t out = {0};
    stc = ft_get(st->db, key, key_len, &out);
    if (stc != FT_OK) return stc;
    if (offset > out.len) return FT_ERR_INVALID_ARG;
    if (offset == out.len) return FT_OK;
    st->stats.blocks_read++;
    return fs_emit_sliced((const uint8_t*)out.data + offset, out.len - offset, chunk_cap, cb, arg);
}

ft_status_t fs_kv_stream_write_key_bytes(FsKvStore* st,
                                         const uint8_t* key, size_t key_len,
                                         fs_stream_source_cb next, void* arg,
                                         size_t* out_total_len)
{
    if (out_total_len) *out_total_len = 0;
    if (!st || !st->db || !key || key_len == 0 || !next) return FT_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return FT_ERR_INVALID_ARG;

    // Determine new version (meta must be committed last).
    FsKvMeta old_meta = {0};
    int has_meta = 0;
    ft_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &old_meta, &has_meta);
    if (stc != FT_OK && stc != FT_ERR_NOT_FOUND) return stc;

    uint32_t new_ver = has_meta ? (old_meta.version + 1u) : 1u;
    if (new_ver == 0) new_ver = 1u;

    uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
    size_t ckey_len = 0;

    // Pull into a moderately sized buffer so the source can yield large chunks.
    // Keep this bounded; the write path re-chunks into FS_STORE_CHUNK_SIZE.
    uint8_t inbuf[16384];
    uint8_t chunkbuf[FS_STORE_CHUNK_SIZE];
    size_t chunk_fill = 0;

    uint32_t chunks_written = 0;
    size_t total = 0;

    while (1) {
        size_t got = 0;
        stc = next(inbuf, sizeof(inbuf), &got, arg);
        if (stc != FT_OK) return stc;
        if (got == 0) break; // EOF

        if (total > SIZE_MAX - got) return FT_ERR_INVALID_ARG;
        total += got;
        if (total > (size_t)UINT32_MAX) return FT_ERR_INVALID_ARG;

        size_t pos = 0;
        while (pos < got) {
            size_t n = got - pos;
            size_t space = (size_t)FS_STORE_CHUNK_SIZE - chunk_fill;
            if (n > space) n = space;
            memcpy(chunkbuf + chunk_fill, inbuf + pos, n);
            chunk_fill += n;
            pos += n;

            if (chunk_fill == (size_t)FS_STORE_CHUNK_SIZE) {
                stc = fs_kv_make_chunk_key_bytes(key, key_len, new_ver, chunks_written, ckey, sizeof(ckey), &ckey_len);
                if (stc != FT_OK) return stc;
                stc = ft_put(st->db, ckey, ckey_len, chunkbuf, chunk_fill);
                if (stc != FT_OK) return stc;
                st->stats.blocks_written++;
                chunks_written++;
                chunk_fill = 0;
            }
        }
    }

    // Write last partial chunk if any.
    if (chunk_fill != 0) {
        stc = fs_kv_make_chunk_key_bytes(key, key_len, new_ver, chunks_written, ckey, sizeof(ckey), &ckey_len);
        if (stc != FT_OK) return stc;
        stc = ft_put(st->db, ckey, ckey_len, chunkbuf, chunk_fill);
        if (stc != FT_OK) return stc;
        st->stats.blocks_written++;
        chunks_written++;
    }

    // Commit meta last.
    FsKvMeta meta = {
        .magic = FS_KV_META_MAGIC,
        .version = new_ver,
        .total_len = (uint32_t)total,
        .chunks = chunks_written,
    };
    stc = ft_put(st->db, key, key_len, &meta, sizeof(meta));
    if (stc != FT_OK) return stc;

    if (out_total_len) *out_total_len = total;
    return FT_OK;
}

ft_status_t fs_file_stream_read(FsKvStore* st,
                                const char* path,
                                size_t max_chunk,
                                fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !fs_is_valid_path(path) || fs_is_dir_path(path) || !cb) return FT_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return FT_ERR_INVALID_ARG;

    // Read file meta bytes (EDN) and parse only the numeric fields we need.
    size_t saved = 0;
    (void)fs_kv_get(st, path, NULL, 0, &saved);
    if (saved == 0) return FT_ERR_NOT_FOUND;

    char* meta = (char*)malloc(saved + 1);
    if (!meta) return FT_ERR_NO_MEMORY;
    size_t got = fs_kv_get(st, path, (uint8_t*)meta, saved, &saved);
    meta[got] = '\0';

    uint32_t ver = 0;
    uint32_t chunks = 0;
    uint32_t size_u32 = 0;
    int ok_ver = fs_parse_u32_field(meta, ":version", &ver);
    int ok_chunks = fs_parse_u32_field(meta, ":chunks", &chunks);
    int ok_size = fs_parse_u32_field(meta, ":size", &size_u32);
    free(meta);

    if (!ok_ver || !ok_chunks || !ok_size) return FT_ERR_CORRUPT;
    if (ver == 0 || chunks == 0) return FT_ERR_CORRUPT;
    size_t total = (size_t)size_u32;

    uint8_t buf[FS_STORE_CHUNK_SIZE];
    size_t remaining_total = total;

    for (uint32_t i = 0; i < chunks; i++) {
        char ckey[FS_KEY_MAX];
        if (fs_make_chunk_key(ckey, path, ver, i) != FS_NO_ERR) return FT_ERR_INVALID_ARG;

        size_t want = remaining_total;
        if (want > (size_t)FS_STORE_CHUNK_SIZE) want = (size_t)FS_STORE_CHUNK_SIZE;

        size_t saved_chunk = 0;
        ft_status_t stc = fs_kv_get_status(st, ckey, buf, want, &saved_chunk);
        if (stc != FT_OK) return stc;
        if (saved_chunk < want && remaining_total != 0) return FT_ERR_CORRUPT;

        st->stats.blocks_read++; // count storage chunks, not app slices
        if (want) {
            stc = fs_emit_sliced(buf, want, chunk_cap, cb, arg);
            if (stc != FT_OK) return stc;
        }

        if (remaining_total >= want) remaining_total -= want;
        else remaining_total = 0;

        if (remaining_total == 0) break;
    }

    if (remaining_total != 0) return FT_ERR_CORRUPT;
    return FT_OK;
}

ft_status_t fs_file_stream_read_from(FsKvStore* st,
                                     const char* path,
                                     size_t offset,
                                     size_t max_chunk,
                                     fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !fs_is_valid_path(path) || fs_is_dir_path(path) || !cb) return FT_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return FT_ERR_INVALID_ARG;

    // Read meta bytes and parse fields (same as fs_file_stream_read).
    size_t saved = 0;
    (void)fs_kv_get(st, path, NULL, 0, &saved);
    if (saved == 0) return FT_ERR_NOT_FOUND;

    char* meta = (char*)malloc(saved + 1);
    if (!meta) return FT_ERR_NO_MEMORY;
    size_t got = fs_kv_get(st, path, (uint8_t*)meta, saved, &saved);
    meta[got] = '\0';

    uint32_t ver = 0;
    uint32_t chunks = 0;
    uint32_t size_u32 = 0;
    int ok_ver = fs_parse_u32_field(meta, ":version", &ver);
    int ok_chunks = fs_parse_u32_field(meta, ":chunks", &chunks);
    int ok_size = fs_parse_u32_field(meta, ":size", &size_u32);
    free(meta);
    if (!ok_ver || !ok_chunks || !ok_size) return FT_ERR_CORRUPT;
    if (ver == 0 || chunks == 0) return FT_ERR_CORRUPT;

    const size_t total = (size_t)size_u32;
    if (offset > total) return FT_ERR_INVALID_ARG;
    if (offset == total) return FT_OK;

    const uint32_t start_idx = (uint32_t)(offset / (size_t)FS_STORE_CHUNK_SIZE);
    size_t start_off = offset % (size_t)FS_STORE_CHUNK_SIZE;
    size_t remaining_total = total - offset;

    uint8_t buf[FS_STORE_CHUNK_SIZE];

    for (uint32_t i = start_idx; i < chunks && remaining_total > 0; i++) {
        char ckey[FS_KEY_MAX];
        if (fs_make_chunk_key(ckey, path, ver, i) != FS_NO_ERR) return FT_ERR_INVALID_ARG;

        size_t saved_chunk = 0;
        ft_status_t stc = fs_kv_get_status(st, ckey, buf, sizeof(buf), &saved_chunk);
        if (stc != FT_OK) return stc;
        if (saved_chunk == 0 && total != 0) return FT_ERR_CORRUPT;
        if (start_off > saved_chunk) return FT_ERR_INVALID_ARG;

        size_t avail = saved_chunk - start_off;
        size_t take = (remaining_total < avail) ? remaining_total : avail;
        st->stats.blocks_read++;
        if (take) {
            stc = fs_emit_sliced(buf + start_off, take, chunk_cap, cb, arg);
            if (stc != FT_OK) return stc;
            remaining_total -= take;
        }
        start_off = 0;
    }

    if (remaining_total != 0) return FT_ERR_CORRUPT;
    return FT_OK;
}

ft_status_t fs_file_stream_write(FsKvStore* st, EvalState* eval,
                                 const char* path,
                                 fs_stream_source_cb next, void* arg,
                                 size_t* out_total_len)
{
    if (out_total_len) *out_total_len = 0;
    if (!st || !st->db || !eval || !fs_is_valid_path(path) || fs_is_dir_path(path) || !next) return FT_ERR_INVALID_ARG;
    if (strlen(path) >= FS_KEY_MAX) return FT_ERR_INVALID_ARG;

    ID old_meta = fs_meta_get_map(st, eval, path);
    uint32_t old_ver = fs_meta_version(old_meta);
    uint32_t new_ver = old_ver + 1u;
    if (new_ver == 0) new_ver = 1u;

    ID ctime = fs_meta_ctime(old_meta);
    if (!ctime) ctime = fs_now_instant();
    ID mtime = fs_now_instant();

    uint8_t inbuf[16384];
    uint8_t chunkbuf[FS_STORE_CHUNK_SIZE];
    size_t chunk_fill = 0;
    uint32_t chunk_idx = 0;
    size_t total = 0;

    while (1) {
        size_t got = 0;
        ft_status_t stc = next(inbuf, sizeof(inbuf), &got, arg);
        if (stc != FT_OK) return stc;
        if (got == 0) break; // EOF

        if (total > SIZE_MAX - got) return FT_ERR_INVALID_ARG;
        total += got;
        if (total > (size_t)INT32_MAX) return FT_ERR_INVALID_ARG;

        size_t pos = 0;
        while (pos < got) {
            size_t n = got - pos;
            size_t space = (size_t)FS_STORE_CHUNK_SIZE - chunk_fill;
            if (n > space) n = space;
            memcpy(chunkbuf + chunk_fill, inbuf + pos, n);
            chunk_fill += n;
            pos += n;

            if (chunk_fill == (size_t)FS_STORE_CHUNK_SIZE) {
                char ckey[FS_KEY_MAX];
                fs_err_t e = fs_make_chunk_key(ckey, path, new_ver, chunk_idx);
                if (e != FS_NO_ERR) return FT_ERR_INVALID_ARG;
                if (!fs_kv_put(st, ckey, chunkbuf, chunk_fill)) return FT_ERR_IO;
                st->stats.blocks_written++;
                chunk_idx++;
                chunk_fill = 0;
            }
        }
    }

    // Files always store at least one chunk (even empty) to keep meta consistent with existing fs_write_bytes.
    if (chunk_fill != 0 || chunk_idx == 0) {
        char ckey[FS_KEY_MAX];
        fs_err_t e = fs_make_chunk_key(ckey, path, new_ver, chunk_idx);
        if (e != FS_NO_ERR) return FT_ERR_INVALID_ARG;
        if (!fs_kv_put(st, ckey, chunkbuf, chunk_fill)) return FT_ERR_IO;
        st->stats.blocks_written++;
        chunk_idx++;
        chunk_fill = 0;
    }

    // Commit meta last (makes the new version visible).
    CljMap* m = make_map(8);
    m = map_assoc(m, (ID)kw(":type"), (ID)kw(":file"));
    m = map_assoc(m, (ID)kw(":version"), fixnum((int32_t)new_ver));
    m = map_assoc(m, (ID)kw(":size"), fixnum((int32_t)total));
    m = map_assoc(m, (ID)kw(":chunks"), fixnum((int32_t)chunk_idx));
    m = map_assoc(m, (ID)kw(":ctime"), ctime);
    if (mtime != ctime) {
        m = map_assoc(m, (ID)kw(":mtime"), mtime);
    }

    fs_err_t fe = fs_meta_put_map(st, path, (ID)m);
    if (fe != FS_NO_ERR) return FT_ERR_IO;

    if (out_total_len) *out_total_len = total;
    return FT_OK;
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
    return ft_get_into(st->db, key, strlen(key), out, out_len, saved_len_out);
}

ft_status_t fs_kv_del_status(FsKvStore *st, const char *key)
{
    if (!st || !st->db || !key) return FT_ERR_INVALID_ARG;
    return ft_del(st->db, key, strlen(key));
}

ft_status_t fs_kv_put_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len)
{
    if (!st || !st->db || !key || key_len == 0) return FT_ERR_INVALID_ARG;
    if (len > (size_t)INT32_MAX) return FT_ERR_INVALID_ARG;
    return fs_kv_put_chunked_bytes(st->db, key, key_len, data, len);
}

ft_status_t fs_kv_get_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len, uint8_t *out, size_t out_len, size_t *saved_len_out)
{
    if (saved_len_out) *saved_len_out = 0;
    if (!st || !st->db || !key || key_len == 0) return FT_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    ft_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc == FT_OK && has_meta) {
        return fs_kv_get_chunked_bytes(st->db, key, key_len, out, out_len, saved_len_out);
    }
    // Legacy inline value.
    if (stc == FT_OK && !has_meta) {
        return ft_get_into(st->db, key, key_len, out, out_len, saved_len_out);
    }
    return stc;
}

ft_status_t fs_kv_del_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len)
{
    if (!st || !st->db || !key || key_len == 0) return FT_ERR_INVALID_ARG;
    return ft_del(st->db, key, key_len);
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
    if (!out || !path) return FS_ERR_INVALID_PATH;
    size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;

    size_t pos = 0;
    if (pos + path_len >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;
    memcpy(out + pos, path, path_len);
    pos += path_len;

    if (pos + 1 >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;
    out[pos++] = '@';
    pos = fs_append_u32_dec((uint8_t*)out, pos, FS_KEY_MAX, version);
    if (pos > FS_KEY_MAX) return FS_ERR_INVALID_PATH;

    if (pos + 1 >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;
    out[pos++] = '#';
    pos = fs_append_u32_dec_zeropad((uint8_t*)out, pos, FS_KEY_MAX, chunk_idx, 4);
    if (pos > FS_KEY_MAX) return FS_ERR_INVALID_PATH;

    out[pos] = '\0';
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

    uint32_t chunks = (uint32_t)((len + FS_STORE_CHUNK_SIZE - 1) / FS_STORE_CHUNK_SIZE);
    if (chunks == 0) chunks = 1;

    /* Write chunk keys first (new version). */
    for (uint32_t i = 0; i < chunks; i++) {
        size_t off = (size_t)i * FS_STORE_CHUNK_SIZE;
        size_t remaining = len > off ? (len - off) : 0;
        size_t chunk_len = remaining > FS_STORE_CHUNK_SIZE ? FS_STORE_CHUNK_SIZE : remaining;

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
        if (want > FS_STORE_CHUNK_SIZE) want = FS_STORE_CHUNK_SIZE;

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

