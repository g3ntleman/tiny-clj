#include <subjective-c/subjective-c.h>
#include <stdbool.h>
extern struct CljSymbol *SYM_KW_SIZE, *SYM_KW_CHUNKS, *SYM_KW_PATH, *SYM_KW_META;
#include "fs_layer.h"

#include "byte_array.h"
#include "event_loop.h"
#include "function.h"
#include "memory.h"
#include "mini_format.h"
#include "strings.h"
#include "symbol.h"
#include "vector.h"

#include "tiny_db.h"
#include "tdb_blockdev.h"
#include "tdb_utils.h"

#include <string.h>
#if defined(ESP_PLATFORM)
#include <esp_partition.h>
#include <esp_err.h>
#endif

#if !defined(TINYCLJ_RAM_BACKEND_ENABLED)
#if defined(ESP_PLATFORM)
#define TINYCLJ_RAM_BACKEND_ENABLED 0
#else
#define TINYCLJ_RAM_BACKEND_ENABLED 1
#endif
#endif

#if defined(ESP_PLATFORM) && (TINYCLJ_RAM_BACKEND_ENABLED != 0)
#error "RAM backend must be disabled on ESP32 builds"
#endif

#define FS_APP_MAX_CHUNK_SIZE 4096u
// Internal storage chunk size. This is NOT an app-facing limit.
// App-facing streaming APIs (future) must cap chunks to FS_APP_MAX_CHUNK_SIZE.
#define FS_STORE_CHUNK_SIZE 4096u
#define FS_KV_SYNC_DEBOUNCE_MS 1000
#define FS_KV_SYNC_TIMER_KEY_NAME "fs.kv.sync"

// Forward declarations for FS helpers.
static bool fs_is_valid_path(const char *path);
static bool fs_is_dir_path(const char *path);
static fs_err_t fs_make_chunk_key(char out[FS_KEY_MAX], const char *path, uint32_t version, uint32_t chunk_idx);
static bool fs_is_reserved_public_path_byte(uint8_t byte);
static bool fs_key_is_internal_meta_sidecar(const void *key, size_t key_len);

// Binary file metadata (16 bytes, no EDN parsing needed)
#define FS_FILE_META_MAGIC 0x454C4946u /* 'F''I''L''E' */
typedef struct __attribute__((packed)) FsFileMeta {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t chunks;
} FsFileMeta;

static bool fs_meta_get(FsKvStore *st, const char *path, FsFileMeta *out);
static bool fs_meta_put(FsKvStore *st, const char *path, const FsFileMeta *meta);

// Integer formatting: use mini_format.h helpers (DRY)

// -----------------------------------------------------------------------------
// tiny-db.kv long-value storage (chunked) using blob keys (no C-strings).
// -----------------------------------------------------------------------------
#define FS_KV_META_MAGIC 0x4D564B46u /* 'F''K''V''M' */
#define FS_KV_CHUNK_KEY_SUFFIX_MAX 9u
#define FS_KV_KEY_MAX 1024u
#define FS_KV_CHUNK_TAG 0x01u

typedef struct __attribute__((packed)) FsKvMeta {
    uint32_t magic;
    uint32_t version;
    uint32_t total_len;
    uint32_t chunks;
} FsKvMeta;

static tdb_status_t fs_kv_make_chunk_key_bytes(const uint8_t* key, size_t key_len,
                                              uint32_t version, uint32_t chunk_idx,
                                              uint8_t* out, size_t out_cap, size_t* out_len)
{
    if (out_len) *out_len = 0;
    if (!key || key_len == 0 || !out || !out_len) return TDB_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return TDB_ERR_INVALID_ARG;

    // Chunk key: K || tag(1B) || ver_wire32 || idx_wire32
    const size_t need = key_len + 1 + 4 + 4;
    if (need > out_cap) return TDB_ERR_INVALID_ARG;
    size_t pos = 0;
    memcpy(out + pos, key, key_len);
    pos += key_len;
    out[pos++] = (uint8_t)FS_KV_CHUNK_TAG;
    tdb_u32_wire_write(out + pos, version);
    pos += 4;
    tdb_u32_wire_write(out + pos, chunk_idx);
    pos += 4;
    *out_len = pos;
    return TDB_OK;
}

static tdb_status_t fs_kv_read_meta_bytes(tdb_kv_t* db, const uint8_t* key, size_t key_len,
                                        FsKvMeta* out_meta, int* out_has_meta)
{
    if (out_has_meta) *out_has_meta = 0;
    if (!db || !key || key_len == 0 || !out_meta || !out_has_meta) return TDB_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    size_t saved = 0;
    tdb_status_t stc = tdb_kv_get_into(db, key, key_len, &meta, sizeof(meta), &saved);
    if (stc != TDB_OK) return stc;

    if (saved == sizeof(meta) && meta.magic == FS_KV_META_MAGIC) {
        *out_meta = meta;
        *out_has_meta = 1;
    }
    return TDB_OK;
}

static tdb_status_t fs_kv_put_chunked_bytes(tdb_kv_t* db, const uint8_t* key, size_t key_len,
                                          const uint8_t* data, size_t len)
{
    if (!db || !key || key_len == 0 || (!data && len != 0)) return TDB_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return TDB_ERR_INVALID_ARG;
    if (len > UINT32_MAX) return TDB_ERR_INVALID_ARG;

    FsKvMeta old_meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(db, key, key_len, &old_meta, &has_meta);
    if (stc != TDB_OK && stc != TDB_ERR_NOT_FOUND) return stc;

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
        if (stc != TDB_OK) return stc;

        stc = tdb_kv_put(db, ckey, ckey_len, data + off, chunk_len);
        if (stc != TDB_OK) return stc;
    }

    FsKvMeta meta = {
        .magic = FS_KV_META_MAGIC,
        .version = new_ver,
        .total_len = (uint32_t)len,
        .chunks = chunks,
    };
    return tdb_kv_put(db, key, key_len, &meta, sizeof(meta));
}

static tdb_status_t fs_kv_get_chunked_bytes(tdb_kv_t* db, const uint8_t* key, size_t key_len,
                                          uint8_t* out, size_t out_len, size_t* saved_len_out)
{
    if (saved_len_out) *saved_len_out = 0;
    if (!db || !key || key_len == 0) return TDB_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return TDB_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(db, key, key_len, &meta, &has_meta);
    if (stc != TDB_OK) return stc;
    if (!has_meta) return TDB_ERR_CORRUPT;

    if (saved_len_out) *saved_len_out = (size_t)meta.total_len;
    if (!out || out_len == 0 || meta.total_len == 0) return TDB_OK;

    size_t want_total = (size_t)meta.total_len;
    size_t copied = 0;
    uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
    size_t ckey_len = 0;

    for (uint32_t i = 0; i < meta.chunks && copied < want_total; i++) {
        stc = fs_kv_make_chunk_key_bytes(key, key_len, meta.version, i, ckey, sizeof(ckey), &ckey_len);
        if (stc != TDB_OK) return stc;

        size_t remaining = want_total - copied;
        size_t want = remaining > FS_STORE_CHUNK_SIZE ? FS_STORE_CHUNK_SIZE : remaining;
        if (want > out_len - copied) want = out_len - copied;

        size_t saved_chunk = 0;
        stc = tdb_kv_get_into(db, ckey, ckey_len, out + copied, want, &saved_chunk);
        if (stc != TDB_OK) return stc;
        if (saved_chunk < want && copied + saved_chunk < want_total) return TDB_ERR_CORRUPT;
        copied += (saved_chunk < want ? saved_chunk : want);
    }

    return TDB_OK;
}

#if TINYCLJ_RAM_BACKEND_ENABLED
typedef struct {
    uint8_t* buf;
    size_t len;
} FsRamBdev;

static tdb_status_t fs_ram_read(void* ctx, uint32_t addr, void* out, size_t len) {
    FsRamBdev* r = (FsRamBdev*)ctx;
    if ((size_t)addr + len > r->len) return TDB_ERR_IO;
    memcpy(out, r->buf + addr, len);
    return TDB_OK;
}

static tdb_status_t fs_ram_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    FsRamBdev* r = (FsRamBdev*)ctx;
    if ((size_t)addr + len > r->len) return TDB_ERR_IO;
    const uint8_t* in = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        r->buf[addr + i] = (uint8_t)(r->buf[addr + i] & in[i]);
    }
    return TDB_OK;
}

static tdb_status_t fs_ram_erase(void* ctx, uint32_t addr, size_t len) {
    FsRamBdev* r = (FsRamBdev*)ctx;
    if ((size_t)addr + len > r->len) return TDB_ERR_IO;
    memset(r->buf + addr, 0xFF, len);
    return TDB_OK;
}
#endif

#if defined(ESP_PLATFORM)
#define TINYCLJ_TINYDB_PARTITION_LABEL "tiny-db"
#define TINYCLJ_FLASH_PROG_GRANULARITY 4u
#define TINYCLJ_FLASH_ERASE_GRANULARITY 4096u

typedef struct {
    const esp_partition_t* part;
} FsFlashBdev;

static tdb_status_t fs_flash_read(void* ctx, uint32_t addr, void* out, size_t len) {
    FsFlashBdev* f = (FsFlashBdev*)ctx;
    if (!f || !f->part || (!out && len != 0)) return TDB_ERR_INVALID_ARG;
    if (len == 0) return TDB_OK;
    return (esp_partition_read(f->part, addr, out, len) == ESP_OK) ? TDB_OK : TDB_ERR_IO;
}

static tdb_status_t fs_flash_prog(void* ctx, uint32_t addr, const void* data, size_t len) {
    FsFlashBdev* f = (FsFlashBdev*)ctx;
    if (!f || !f->part || (!data && len != 0)) return TDB_ERR_INVALID_ARG;
    if (len == 0) return TDB_OK;

    const uint8_t* in = (const uint8_t*)data;
    uint32_t start = addr;
    uint32_t end = addr + (uint32_t)len;
    uint32_t aligned_start = start & ~(TINYCLJ_FLASH_PROG_GRANULARITY - 1u);
    uint32_t aligned_end = (end + (TINYCLJ_FLASH_PROG_GRANULARITY - 1u)) &
                           ~(TINYCLJ_FLASH_PROG_GRANULARITY - 1u);

    for (uint32_t off = aligned_start; off < aligned_end; off += TINYCLJ_FLASH_PROG_GRANULARITY) {
        uint8_t buf[TINYCLJ_FLASH_PROG_GRANULARITY];
        if (esp_partition_read(f->part, off, buf, sizeof(buf)) != ESP_OK) {
            return TDB_ERR_IO;
        }
        for (uint32_t i = 0; i < TINYCLJ_FLASH_PROG_GRANULARITY; i++) {
            uint32_t pos = off + i;
            if (pos >= start && pos < end) {
                buf[i] = in[pos - start];
            }
        }
        if (esp_partition_write(f->part, off, buf, sizeof(buf)) != ESP_OK) {
            return TDB_ERR_IO;
        }
    }
    return TDB_OK;
}

static tdb_status_t fs_flash_erase(void* ctx, uint32_t addr, size_t len) {
    FsFlashBdev* f = (FsFlashBdev*)ctx;
    if (!f || !f->part) return TDB_ERR_INVALID_ARG;
    if (len == 0) return TDB_OK;
    return (esp_partition_erase_range(f->part, addr, len) == ESP_OK) ? TDB_OK : TDB_ERR_IO;
}
#endif

static bool fs_list_dir_key_is_direct_child(const char* dir_path,
                                            size_t prefix_len,
                                            const void* key, size_t key_len,
                                            char out_kstr[FS_KEY_MAX])
{
    if (!dir_path || !key || !out_kstr) return false;
    if (key_len == prefix_len) return false; // skip dir itself
    if (key_len <= prefix_len) return false;
    if (fs_key_is_internal_meta_sidecar(key, key_len)) return false;

    // Keys are path strings; make a temporary NUL-terminated view.
    if (key_len >= FS_KEY_MAX) return false;
    memcpy(out_kstr, key, key_len);
    out_kstr[key_len] = '\0';

    const char* rest = out_kstr + prefix_len;
    if (!rest || rest[0] == '\0') return false;

    // Skip chunk keys (versioned "@v#NNNN").
    const char* at = strchr(rest, '@');
    if (at && strchr(at, '#')) return false;

    // Only direct children: allow at most one '/' at end.
    const char* slash = strchr(rest, '/');
    if (slash && slash[1] != '\0') return false;

    return true;
}

struct FsKvStore {
    tdb_kv_t* db;
    tdb_blockdev_t bdev;
#if defined(ESP_PLATFORM)
    FsFlashBdev flash;
#elif TINYCLJ_RAM_BACKEND_ENABLED
    FsRamBdev ram;
#endif
    FsStreamStats stats;
    bool sync_dirty;
    struct FsKvStore *sync_next;
};

static FsKvStore *g_fs_global_store = NULL;
static FsKvStore *g_fs_sync_store_head = NULL;
static ID g_fs_sync_timer_fn_obj = NULL;
static ID g_fs_sync_timer_key = NULL;
static ID g_fs_sync_fn_symbol = NULL;
static const IdSymbolCacheEntry g_fs_sync_symbol_cache[] = {
    {&g_fs_sync_fn_symbol, "tiny-clj.fs/kv-sync-flush"},
    {&g_fs_sync_timer_key, FS_KV_SYNC_TIMER_KEY_NAME},
};

static ID fs_kv_sync_timer_callback(ID *args, unsigned int argc);

static void fs_kv_sync_register_store(FsKvStore *st)
{
    if (!st) return;
    st->sync_next = g_fs_sync_store_head;
    g_fs_sync_store_head = st;
}

static void fs_kv_sync_unregister_store(FsKvStore *st)
{
    if (!st) return;
    FsKvStore *prev = NULL;
    FsKvStore *cur = g_fs_sync_store_head;
    while (cur) {
        if (cur == st) {
            if (prev) prev->sync_next = cur->sync_next;
            else g_fs_sync_store_head = cur->sync_next;
            cur->sync_next = NULL;
            cur->sync_dirty = false;
            break;
        }
        prev = cur;
        cur = cur->sync_next;
    }
    if (!g_fs_sync_store_head) {
        if (g_fs_sync_timer_key) {
            (void)timer_cancel_named(g_fs_sync_timer_key);
        }
    }
}

static void fs_kv_sync_ensure_timer_fn(void)
{
    if (g_fs_sync_timer_fn_obj) return;
    if (!id_symbol_cache_init_global(g_fs_sync_symbol_cache,
                                     sizeof(g_fs_sync_symbol_cache) / sizeof(g_fs_sync_symbol_cache[0])) ||
        !g_fs_sync_fn_symbol) {
        return;
    }
    g_fs_sync_timer_fn_obj = make_named_func(fs_kv_sync_timer_callback, g_fs_sync_fn_symbol);
}

static void fs_kv_sync_schedule_debounced(void)
{
    fs_kv_sync_ensure_timer_fn();
    if (!g_fs_sync_timer_fn_obj) return;
    (void)id_symbol_cache_init_global(g_fs_sync_symbol_cache,
                                      sizeof(g_fs_sync_symbol_cache) / sizeof(g_fs_sync_symbol_cache[0]));
    (void)timer_upsert_named(g_fs_sync_timer_key,
                             RETAIN(g_fs_sync_timer_fn_obj),
                             FS_KV_SYNC_DEBOUNCE_MS,
                             false,
                             0);
}

static void fs_kv_sync_mark_dirty(FsKvStore *st)
{
    if (!st) return;
    st->sync_dirty = true;
    fs_kv_sync_schedule_debounced();
}

static ID fs_kv_sync_timer_callback(ID *args, unsigned int argc)
{
    (void)args;
    if (argc != 0) return NULL;

    bool retry_needed = false;
    FsKvStore *cur = g_fs_sync_store_head;
    while (cur) {
        if (cur->sync_dirty && cur->db) {
            tdb_status_t stc = tdb_kv_sync(cur->db);
            if (stc == TDB_OK) cur->sync_dirty = false;
            else retry_needed = true;
        }
        cur = cur->sync_next;
    }

    if (retry_needed && g_fs_sync_timer_fn_obj) {
        (void)id_symbol_cache_init_global(g_fs_sync_symbol_cache,
                                          sizeof(g_fs_sync_symbol_cache) / sizeof(g_fs_sync_symbol_cache[0]));
        (void)timer_upsert_named(g_fs_sync_timer_key,
                                 RETAIN(g_fs_sync_timer_fn_obj),
                                 FS_KV_SYNC_DEBOUNCE_MS,
                                 false,
                                 0);
    }
    return NULL;
}

FsKvStore *fs_global_store(void)
{
    if (!g_fs_global_store) {
        g_fs_global_store = fs_kv_store_new();
    }
    return g_fs_global_store;
}

FsKvStore *fs_global_store_if_initialized(void)
{
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

/**
 * @brief Create and initialize the global KV store backend.
 *
 * On ESP32 this binds tiny-db to the dedicated flash partition.
 * On non-ESP builds this can use the RAM backend when enabled.
 *
 * @return Pointer to initialized store on success, otherwise NULL.
 */
FsKvStore *fs_kv_store_new(void)
{
    FsKvStore *st = (FsKvStore *)CLJ_MALLOC(sizeof(FsKvStore));
    memset(st, 0, sizeof(*st));

#if defined(ESP_PLATFORM)
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, TINYCLJ_TINYDB_PARTITION_LABEL);
    if (!part || part->size == 0) {
        CLJ_FREE(st);
        return NULL;
    }

    st->flash.part = part;
    st->bdev.ctx = &st->flash;
    st->bdev.ops.read = fs_flash_read;
    st->bdev.ops.prog = fs_flash_prog;
    st->bdev.ops.erase = fs_flash_erase;
    st->bdev.geom.total_size_bytes = part->size;
    st->bdev.geom.read_granularity = 1;
    st->bdev.geom.prog_granularity = 1;
    st->bdev.geom.erase_granularity = TINYCLJ_FLASH_ERASE_GRANULARITY;
#elif TINYCLJ_RAM_BACKEND_ENABLED
    // Host default: RAM-backed block device for tiny-db.
    const size_t ram_bytes = 128 * 1024;
    st->ram.buf = (uint8_t*)CLJ_MALLOC(ram_bytes);
    st->ram.len = ram_bytes;
    memset(st->ram.buf, 0xFF, ram_bytes);

    st->bdev.ctx = &st->ram;
    st->bdev.ops.read = fs_ram_read;
    st->bdev.ops.prog = fs_ram_prog;
    st->bdev.ops.erase = fs_ram_erase;
    st->bdev.geom.total_size_bytes = (uint32_t)ram_bytes;
    st->bdev.geom.read_granularity = 1;
    st->bdev.geom.prog_granularity = 1;
    // tiny-db stores B-Tree pages in erase-sized log records (must be power-of-two).
    // To reliably fit 4KB chunk values inline (no overflow pages), use a larger logical erase
    // granularity than the chunk size.
    st->bdev.geom.erase_granularity = 16384;
#else
    CLJ_FREE(st);
    return NULL;
#endif

    tdb_status_t fst = tdb_kv_open(&st->db, &st->bdev, NULL);
    if (fst != TDB_OK) {
#if defined(ESP_PLATFORM)
#elif TINYCLJ_RAM_BACKEND_ENABLED
    CLJ_FREE(st->ram.buf);
#endif
        CLJ_FREE(st);
        return NULL;
    }
    fs_kv_sync_register_store(st);
    return st;
}

/* -------------------------------------------------------------------------- */
/* Streaming stats (resettable)                                               */
/* -------------------------------------------------------------------------- */

tdb_status_t fs_stream_stats_reset(FsKvStore* st)
{
    if (!st) return TDB_ERR_INVALID_ARG;
    st->stats.blocks_read = 0;
    st->stats.blocks_written = 0;
    return TDB_OK;
}

tdb_status_t fs_stream_stats_get(const FsKvStore* st, FsStreamStats* out)
{
    if (!st || !out) return TDB_ERR_INVALID_ARG;
    *out = st->stats;
    return TDB_OK;
}

static inline size_t fs_clamp_app_chunk(size_t max_chunk)
{
    if (max_chunk == 0) return 0;
    if (max_chunk > (size_t)FS_APP_MAX_CHUNK_SIZE) return (size_t)FS_APP_MAX_CHUNK_SIZE;
    return max_chunk;
}

static tdb_status_t fs_emit_sliced(const uint8_t* data, size_t len,
                                 size_t max_chunk,
                                 fs_stream_sink_cb cb, void* arg)
{
    if (!cb) return TDB_ERR_INVALID_ARG;
    if (!data && len != 0) return TDB_ERR_INVALID_ARG;
    if (len == 0) return TDB_OK;

    size_t pos = 0;
    while (pos < len) {
        size_t n = len - pos;
        if (n > max_chunk) n = max_chunk;
        tdb_status_t st = cb(data + pos, n, arg);
        if (st != TDB_OK) return st;
        pos += n;
    }
    return TDB_OK;
}

/* -------------------------------------------------------------------------- */
/* Streaming read/write (KV + FS)                                             */
/* -------------------------------------------------------------------------- */

tdb_status_t fs_kv_stream_read_key_bytes(FsKvStore* st,
                                        const uint8_t* key, size_t key_len,
                                        size_t max_chunk,
                                        fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !key || key_len == 0 || !cb) return TDB_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return TDB_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc != TDB_OK) return stc;

    if (has_meta) {
        /* Chunked value: read chunks by explicit index key to avoid any cursor sort dependence. */
        size_t remaining_total = (size_t)meta.total_len;
        uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
        size_t ckey_len = 0;
        uint8_t buf[FS_STORE_CHUNK_SIZE];

        for (uint32_t i = 0; i < meta.chunks && remaining_total > 0; i++) {
            stc = fs_kv_make_chunk_key_bytes(key, key_len, meta.version, i, ckey, sizeof(ckey), &ckey_len);
            if (stc != TDB_OK) return stc;

            size_t saved = 0;
            stc = tdb_kv_get_into(st->db, ckey, ckey_len, buf, sizeof(buf), &saved);
            if (stc != TDB_OK) return stc;
            if (saved == 0) return TDB_ERR_CORRUPT;
            if (saved > (size_t)FS_STORE_CHUNK_SIZE) return TDB_ERR_CORRUPT;
            if (saved > remaining_total) return TDB_ERR_CORRUPT;

            st->stats.blocks_read++;
            stc = fs_emit_sliced(buf, saved, chunk_cap, cb, arg);
            if (stc != TDB_OK) return stc;
            remaining_total -= saved;
        }
        if (remaining_total != 0) return TDB_ERR_CORRUPT;
        return TDB_OK;
    }

    // Legacy inline value: emit it in <= max_chunk slices.
    tdb_blob_t out = {0};
    stc = tdb_kv_get(st->db, key, key_len, &out);
    if (stc != TDB_OK) return stc;
    st->stats.blocks_read++; // single stored value
    return fs_emit_sliced((const uint8_t*)out.data, out.len, chunk_cap, cb, arg);
}

tdb_status_t fs_kv_stream_read_key_bytes_from(FsKvStore* st,
                                             const uint8_t* key, size_t key_len,
                                             size_t offset,
                                             size_t max_chunk,
                                             fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !key || key_len == 0 || !cb) return TDB_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return TDB_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc != TDB_OK) return stc;

    if (has_meta) {
        const size_t total_len = (size_t)meta.total_len;
        if (offset > total_len) return TDB_ERR_INVALID_ARG;
        if (offset == total_len) return TDB_OK;

        const uint32_t start_idx = (uint32_t)(offset / (size_t)FS_STORE_CHUNK_SIZE);
        size_t start_off = offset % (size_t)FS_STORE_CHUNK_SIZE;
        size_t remaining_total = total_len - offset;

        uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
        size_t ckey_len = 0;
        uint8_t buf[FS_STORE_CHUNK_SIZE];

        for (uint32_t i = start_idx; i < meta.chunks && remaining_total > 0; i++) {
            stc = fs_kv_make_chunk_key_bytes(key, key_len, meta.version, i, ckey, sizeof(ckey), &ckey_len);
            if (stc != TDB_OK) return stc;

            size_t saved = 0;
            stc = tdb_kv_get_into(st->db, ckey, ckey_len, buf, sizeof(buf), &saved);
            if (stc != TDB_OK) return stc;
            if (saved == 0) return TDB_ERR_CORRUPT;
            if (start_off > saved) return TDB_ERR_INVALID_ARG;

            size_t avail = saved - start_off;
            size_t take = (remaining_total < avail) ? remaining_total : avail;
            if (take) {
                st->stats.blocks_read++;
                stc = fs_emit_sliced(buf + start_off, take, chunk_cap, cb, arg);
                if (stc != TDB_OK) return stc;
                remaining_total -= take;
            } else {
                st->stats.blocks_read++;
            }
            start_off = 0;
        }

        if (remaining_total != 0) return TDB_ERR_CORRUPT;
        return TDB_OK;
    }

    // Legacy inline: offset into single stored value.
    tdb_blob_t out = {0};
    stc = tdb_kv_get(st->db, key, key_len, &out);
    if (stc != TDB_OK) return stc;
    if (offset > out.len) return TDB_ERR_INVALID_ARG;
    if (offset == out.len) return TDB_OK;
    st->stats.blocks_read++;
    return fs_emit_sliced((const uint8_t*)out.data + offset, out.len - offset, chunk_cap, cb, arg);
}

tdb_status_t fs_kv_stream_write_key_bytes(FsKvStore* st,
                                         const uint8_t* key, size_t key_len,
                                         fs_stream_source_cb next, void* arg,
                                         size_t* out_total_len)
{
    if (out_total_len) *out_total_len = 0;
    if (!st || !st->db || !key || key_len == 0 || !next) return TDB_ERR_INVALID_ARG;
    if (key_len > FS_KV_KEY_MAX) return TDB_ERR_INVALID_ARG;

    // Determine new version (meta must be committed last).
    FsKvMeta old_meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &old_meta, &has_meta);
    if (stc != TDB_OK && stc != TDB_ERR_NOT_FOUND) return stc;

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
        if (stc != TDB_OK) return stc;
        if (got == 0) break; // EOF

        if (total > SIZE_MAX - got) return TDB_ERR_INVALID_ARG;
        total += got;
        if (total > (size_t)UINT32_MAX) return TDB_ERR_INVALID_ARG;

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
                if (stc != TDB_OK) return stc;
                stc = tdb_kv_put(st->db, ckey, ckey_len, chunkbuf, chunk_fill);
                if (stc != TDB_OK) return stc;
                st->stats.blocks_written++;
                chunks_written++;
                chunk_fill = 0;
            }
        }
    }

    // Write last partial chunk if any.
    if (chunk_fill != 0) {
        stc = fs_kv_make_chunk_key_bytes(key, key_len, new_ver, chunks_written, ckey, sizeof(ckey), &ckey_len);
        if (stc != TDB_OK) return stc;
        stc = tdb_kv_put(st->db, ckey, ckey_len, chunkbuf, chunk_fill);
        if (stc != TDB_OK) return stc;
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
    stc = tdb_kv_put(st->db, key, key_len, &meta, sizeof(meta));
    if (stc != TDB_OK) return stc;

    if (out_total_len) *out_total_len = total;
    return TDB_OK;
}

/* Set file size directly. Creates zero-filled chunks for new version and
 * deletes old-version chunk keys after committing the new metadata.
 */
fs_err_t fs_set_size(FsKvStore *st, const char *path, uint32_t new_size)
{
    if (!fs_is_valid_path(path) || fs_is_dir_path(path)) return FS_ERR_INVALID_PATH;
    if (!st) return FS_ERR_IO;
    if (strlen(path) >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;

    FsFileMeta old_meta = {0};
    bool has_old = fs_meta_get(st, path, &old_meta);
    uint32_t new_ver = has_old ? (old_meta.version + 1u) : 1u;
    if (new_ver == 0) new_ver = 1u;

    uint32_t chunks = (uint32_t)((new_size + FS_STORE_CHUNK_SIZE - 1) / FS_STORE_CHUNK_SIZE);
    if (chunks == 0) chunks = 1;

    uint8_t zero_chunk[FS_STORE_CHUNK_SIZE];
    memset(zero_chunk, 0, sizeof(zero_chunk));

    for (uint32_t i = 0; i < chunks; i++) {
        size_t off = (size_t)i * FS_STORE_CHUNK_SIZE;
        size_t remaining = new_size > off ? (size_t)(new_size - off) : 0;
        size_t chunk_len = remaining > FS_STORE_CHUNK_SIZE ? FS_STORE_CHUNK_SIZE : remaining;

        char ckey[FS_KEY_MAX];
        fs_err_t e = fs_make_chunk_key(ckey, path, new_ver, i);
        if (e != FS_NO_ERR) return e;

        if (!fs_kv_put(st, ckey, zero_chunk, chunk_len)) {
            return FS_ERR_IO;
        }
    }

    FsFileMeta meta = {
        .magic = FS_FILE_META_MAGIC,
        .version = new_ver,
        .size = new_size,
        .chunks = chunks
    };
    if (!fs_meta_put(st, path, &meta)) return FS_ERR_IO;

    /* Remove previous-version chunk keys (best-effort). */
    if (has_old && old_meta.version != 0) {
        for (uint32_t i = 0; i < old_meta.chunks; i++) {
            char ok[FS_KEY_MAX];
            if (fs_make_chunk_key(ok, path, old_meta.version, i) == FS_NO_ERR) {
                (void)fs_kv_del(st, ok);
            }
        }
    }

    return FS_NO_ERR;
}

tdb_status_t fs_file_stream_read(FsKvStore* st,
                                const char* path,
                                size_t max_chunk,
                                fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !fs_is_valid_path(path) || fs_is_dir_path(path) || !cb) return TDB_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return TDB_ERR_INVALID_ARG;

    // Read binary file meta.
    FsFileMeta meta;
    if (!fs_meta_get(st, path, &meta)) return TDB_ERR_NOT_FOUND;
    if (meta.version == 0 || meta.chunks == 0) return TDB_ERR_CORRUPT;

    uint8_t buf[FS_STORE_CHUNK_SIZE];
    size_t remaining_total = (size_t)meta.size;

    for (uint32_t i = 0; i < meta.chunks; i++) {
        char ckey[FS_KEY_MAX];
        if (fs_make_chunk_key(ckey, path, meta.version, i) != FS_NO_ERR) return TDB_ERR_INVALID_ARG;

        size_t want = remaining_total;
        if (want > (size_t)FS_STORE_CHUNK_SIZE) want = (size_t)FS_STORE_CHUNK_SIZE;

        size_t saved_chunk = 0;
        tdb_status_t stc = fs_kv_get_status(st, ckey, buf, want, &saved_chunk);
        if (stc != TDB_OK) return stc;
        if (saved_chunk < want && remaining_total != 0) return TDB_ERR_CORRUPT;

        st->stats.blocks_read++;
        if (want) {
            stc = fs_emit_sliced(buf, want, chunk_cap, cb, arg);
            if (stc != TDB_OK) return stc;
        }

        if (remaining_total >= want) remaining_total -= want;
        else remaining_total = 0;

        if (remaining_total == 0) break;
    }

    if (remaining_total != 0) return TDB_ERR_CORRUPT;
    return TDB_OK;
}

tdb_status_t fs_file_stream_read_from(FsKvStore* st,
                                     const char* path,
                                     size_t offset,
                                     size_t max_chunk,
                                     fs_stream_sink_cb cb, void* arg)
{
    if (!st || !st->db || !fs_is_valid_path(path) || fs_is_dir_path(path) || !cb) return TDB_ERR_INVALID_ARG;
    const size_t chunk_cap = fs_clamp_app_chunk(max_chunk);
    if (chunk_cap == 0) return TDB_ERR_INVALID_ARG;

    // Read binary file meta.
    FsFileMeta meta;
    if (!fs_meta_get(st, path, &meta)) return TDB_ERR_NOT_FOUND;
    if (meta.version == 0 || meta.chunks == 0) return TDB_ERR_CORRUPT;

    const size_t total = (size_t)meta.size;
    if (offset > total) return TDB_ERR_INVALID_ARG;
    if (offset == total) return TDB_OK;

    const uint32_t start_idx = (uint32_t)(offset / (size_t)FS_STORE_CHUNK_SIZE);
    size_t start_off = offset % (size_t)FS_STORE_CHUNK_SIZE;
    size_t remaining_total = total - offset;

    uint8_t buf[FS_STORE_CHUNK_SIZE];

    for (uint32_t i = start_idx; i < meta.chunks && remaining_total > 0; i++) {
        char ckey[FS_KEY_MAX];
        if (fs_make_chunk_key(ckey, path, meta.version, i) != FS_NO_ERR) return TDB_ERR_INVALID_ARG;

        size_t saved_chunk = 0;
        tdb_status_t stc = fs_kv_get_status(st, ckey, buf, sizeof(buf), &saved_chunk);
        if (stc != TDB_OK) return stc;
        if (saved_chunk == 0 && total != 0) return TDB_ERR_CORRUPT;
        if (start_off > saved_chunk) return TDB_ERR_INVALID_ARG;

        size_t avail = saved_chunk - start_off;
        size_t take = (remaining_total < avail) ? remaining_total : avail;
        st->stats.blocks_read++;
        if (take) {
            stc = fs_emit_sliced(buf + start_off, take, chunk_cap, cb, arg);
            if (stc != TDB_OK) return stc;
            remaining_total -= take;
        }
        start_off = 0;
    }

    if (remaining_total != 0) return TDB_ERR_CORRUPT;
    return TDB_OK;
}

tdb_status_t fs_file_stream_write(FsKvStore* st,
                                 const char* path,
                                 fs_stream_source_cb next, void* arg,
                                 size_t* out_total_len)
{
    if (out_total_len) *out_total_len = 0;
    if (!st || !st->db || !fs_is_valid_path(path) || fs_is_dir_path(path) || !next) return TDB_ERR_INVALID_ARG;
    if (strlen(path) >= FS_KEY_MAX) return TDB_ERR_INVALID_ARG;

    FsFileMeta old_meta;
    bool has_old = fs_meta_get(st, path, &old_meta);
    uint32_t new_ver = has_old ? (old_meta.version + 1u) : 1u;
    if (new_ver == 0) new_ver = 1u;

    uint8_t inbuf[16384];
    uint8_t chunkbuf[FS_STORE_CHUNK_SIZE];
    size_t chunk_fill = 0;
    uint32_t chunk_idx = 0;
    size_t total = 0;

    while (1) {
        size_t got = 0;
        tdb_status_t stc = next(inbuf, sizeof(inbuf), &got, arg);
        if (stc != TDB_OK) return stc;
        if (got == 0) break; // EOF

        if (total > SIZE_MAX - got) return TDB_ERR_INVALID_ARG;
        total += got;
        if (total > (size_t)UINT32_MAX) return TDB_ERR_INVALID_ARG;

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
                if (e != FS_NO_ERR) return TDB_ERR_INVALID_ARG;
                if (!fs_kv_put(st, ckey, chunkbuf, chunk_fill)) return TDB_ERR_IO;
                st->stats.blocks_written++;
                chunk_idx++;
                chunk_fill = 0;
            }
        }
    }

    // Files always store at least one chunk (even empty).
    if (chunk_fill != 0 || chunk_idx == 0) {
        char ckey[FS_KEY_MAX];
        fs_err_t e = fs_make_chunk_key(ckey, path, new_ver, chunk_idx);
        if (e != FS_NO_ERR) return TDB_ERR_INVALID_ARG;
        if (!fs_kv_put(st, ckey, chunkbuf, chunk_fill)) return TDB_ERR_IO;
        st->stats.blocks_written++;
        chunk_idx++;
    }

    // Commit binary meta last (makes the new version visible).
    FsFileMeta meta = {
        .magic = FS_FILE_META_MAGIC,
        .version = new_ver,
        .size = (uint32_t)total,
        .chunks = chunk_idx
    };
    if (!fs_meta_put(st, path, &meta)) return TDB_ERR_IO;

    if (out_total_len) *out_total_len = total;
    return TDB_OK;
}


/**
 * @brief Release a KV store and all backend resources.
 *
 * @param st Store instance to free. NULL is allowed.
 */
void fs_kv_store_free(FsKvStore *st)
{
    if (!st) return;
    fs_kv_sync_unregister_store(st);
    if (st->db) {
        tdb_kv_close(st->db);
        st->db = NULL;
    }
#if TINYCLJ_RAM_BACKEND_ENABLED
    CLJ_FREE(st->ram.buf);
    st->ram.buf = NULL;
    st->ram.len = 0;
#endif
    CLJ_FREE(st);
}

bool fs_kv_put(FsKvStore *st, const char *key, const uint8_t *data, size_t len)
{
    return fs_kv_put_status(st, key, data, len) == TDB_OK;
}

size_t fs_kv_get(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out)
{
    tdb_status_t stc = fs_kv_get_status(st, key, out, out_len, saved_len_out);
    if (stc != TDB_OK) return 0;
    if (!out || out_len == 0 || !saved_len_out) return 0;
    return (*saved_len_out < out_len) ? *saved_len_out : out_len;
}

bool fs_kv_del(FsKvStore *st, const char *key)
{
    if (!st || !key) return false;
    (void)fs_kv_del_status(st, key);
    return true;
}

tdb_status_t fs_kv_put_status(FsKvStore *st, const char *key, const uint8_t *data, size_t len)
{
    if (!st || !st->db || !key) return TDB_ERR_INVALID_ARG;
    if (len > (size_t)INT32_MAX) return TDB_ERR_INVALID_ARG;
    tdb_status_t stc = tdb_kv_put(st->db, key, strlen(key), data, len);
    if (stc == TDB_OK) fs_kv_sync_mark_dirty(st);
    return stc;
}

tdb_status_t fs_kv_get_status(FsKvStore *st, const char *key, uint8_t *out, size_t out_len, size_t *saved_len_out)
{
    if (saved_len_out) *saved_len_out = 0;
    if (!st || !st->db || !key) return TDB_ERR_INVALID_ARG;
    return tdb_kv_get_into(st->db, key, strlen(key), out, out_len, saved_len_out);
}

tdb_status_t fs_kv_del_status(FsKvStore *st, const char *key)
{
    if (!st || !st->db || !key) return TDB_ERR_INVALID_ARG;
    tdb_status_t stc = tdb_kv_del(st->db, key, strlen(key));
    if (stc == TDB_OK) fs_kv_sync_mark_dirty(st);
    return stc;
}

tdb_status_t fs_kv_max_val_len_status(FsKvStore *st, const char *key, size_t *out_max_val_len)
{
    if (out_max_val_len) *out_max_val_len = 0;
    if (!st || !st->db || !key || !out_max_val_len) return TDB_ERR_INVALID_ARG;
    return tdb_kv_max_val_len(st->db, strlen(key), out_max_val_len);
}

tdb_status_t fs_kv_sync_status(FsKvStore *st)
{
    if (!st || !st->db) return TDB_ERR_INVALID_ARG;
    tdb_status_t stc = tdb_kv_sync(st->db);
    if (stc == TDB_OK) st->sync_dirty = false;
    return stc;
}

tdb_status_t fs_kv_put_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len)
{
    if (!st || !st->db || !key || key_len == 0) return TDB_ERR_INVALID_ARG;
    if (len > (size_t)INT32_MAX) return TDB_ERR_INVALID_ARG;
    tdb_status_t stc = fs_kv_put_chunked_bytes(st->db, key, key_len, data, len);
    if (stc == TDB_OK) fs_kv_sync_mark_dirty(st);
    return stc;
}

tdb_status_t fs_kv_get_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len, uint8_t *out, size_t out_len, size_t *saved_len_out)
{
    if (saved_len_out) *saved_len_out = 0;
    if (!st || !st->db || !key || key_len == 0) return TDB_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc == TDB_OK && has_meta) {
        return fs_kv_get_chunked_bytes(st->db, key, key_len, out, out_len, saved_len_out);
    }
    // Legacy inline value.
    if (stc == TDB_OK && !has_meta) {
        return tdb_kv_get_into(st->db, key, key_len, out, out_len, saved_len_out);
    }
    return stc;
}

tdb_status_t fs_kv_del_key_bytes_status(FsKvStore *st, const uint8_t *key, size_t key_len)
{
    if (!st || !st->db || !key || key_len == 0) return TDB_ERR_INVALID_ARG;

    FsKvMeta meta = {0};
    int has_meta = 0;
    tdb_status_t stc = fs_kv_read_meta_bytes(st->db, key, key_len, &meta, &has_meta);
    if (stc == TDB_OK && has_meta && meta.version != 0u) {
        uint8_t ckey[FS_KV_KEY_MAX + FS_KV_CHUNK_KEY_SUFFIX_MAX];
        size_t ckey_len = 0;
        for (uint32_t i = 0; i < meta.chunks; i++) {
            stc = fs_kv_make_chunk_key_bytes(key, key_len, meta.version, i, ckey, sizeof(ckey), &ckey_len);
            if (stc != TDB_OK) return stc;
            stc = tdb_kv_del(st->db, ckey, ckey_len);
            if (stc != TDB_OK && stc != TDB_ERR_NOT_FOUND) return stc;
        }
    } else if (stc != TDB_OK && stc != TDB_ERR_NOT_FOUND) {
        return stc;
    }

    stc = tdb_kv_del(st->db, key, key_len);
    if (stc == TDB_OK) fs_kv_sync_mark_dirty(st);
    return stc;
}

/* -------------------------------------------------------------------------- */
/* FS layer                                                                   */
/* -------------------------------------------------------------------------- */

static bool fs_is_valid_path(const char *path)
{
    if (!path || path[0] != '/') return false;

    for (const uint8_t *cursor = (const uint8_t *)path; *cursor != '\0'; cursor++) {
        if (fs_is_reserved_public_path_byte(*cursor)) {
            return false;
        }
    }

    return true;
}

static bool fs_is_dir_path(const char *path)
{
    if (!path) return false;
    size_t n = strlen(path);
    return n > 0 && path[n - 1] == '/';
}

static bool fs_is_reserved_public_path_byte(uint8_t byte)
{
    return (byte < 0x20u) || (byte == 0x7fu);
}

static bool fs_key_is_internal_meta_sidecar(const void *key, size_t key_len)
{
    if (!key || key_len == 0u) {
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)key;
    for (size_t i = 0; i < key_len; i++) {
        if (bytes[i] == (uint8_t)FS_META_SIDECAR_MARKER) {
            return true;
        }
    }

    return false;
}

tdb_status_t fs_make_meta_sidecar_key(const char *path,
                                      uint8_t *out,
                                      size_t out_cap,
                                      size_t *out_len)
{
    if (out_len) *out_len = 0u;
    if (!path || !out || !out_len) return TDB_ERR_INVALID_ARG;
    if (!fs_is_valid_path(path)) return TDB_ERR_INVALID_ARG;

    size_t path_len = strlen(path);
    if (path_len == 0u || path_len + 1u >= FS_KEY_MAX) return TDB_ERR_INVALID_ARG;
    if (path_len + 1u > out_cap) return TDB_ERR_INVALID_ARG;

    memcpy(out, path, path_len);
    out[path_len] = (uint8_t)FS_META_SIDECAR_MARKER;
    *out_len = path_len + 1u;
    return TDB_OK;
}

// Binary metadata functions (no EDN parsing)
static bool fs_meta_get(FsKvStore *st, const char *path, FsFileMeta *out)
{
    if (!st || !path || !out) return false;
    memset(out, 0, sizeof(*out));

    size_t saved = 0;
    FsFileMeta meta;
    fs_kv_get(st, path, (uint8_t *)&meta, sizeof(meta), &saved);
    if (saved != sizeof(meta)) return false;
    if (meta.magic != FS_FILE_META_MAGIC) return false;

    *out = meta;
    return true;
}

static bool fs_meta_put(FsKvStore *st, const char *path, const FsFileMeta *meta)
{
    if (!st || !path || !meta) return false;
    return fs_kv_put(st, path, (const uint8_t *)meta, sizeof(*meta));
}

static fs_err_t fs_make_chunk_key(char out[FS_KEY_MAX], const char *path, uint32_t version, uint32_t chunk_idx)
{
    if (!out || !path) return FS_ERR_INVALID_PATH;
    size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;

    // Format: "<path>@<version>#<chunk_idx_4digit>"
    int n = mini_snprintf(out, FS_KEY_MAX, "%s@%u#%04u", path, version, chunk_idx);
    if (n < 0 || (size_t)n >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;

    return FS_NO_ERR;
}

fs_err_t fs_write_bytes(FsKvStore *st, const char *path, const uint8_t *data, size_t len)
{
    if (!fs_is_valid_path(path) || fs_is_dir_path(path)) return FS_ERR_INVALID_PATH;
    if (strlen(path) >= FS_KEY_MAX) return FS_ERR_INVALID_PATH;
    if (!st) return FS_ERR_IO;
    if (len > (size_t)UINT32_MAX) return FS_ERR_IO;

    // Get old version (if any) to increment
    FsFileMeta old_meta;
    bool has_old = fs_meta_get(st, path, &old_meta);
    uint32_t new_ver = has_old ? (old_meta.version + 1u) : 1u;
    if (new_ver == 0) new_ver = 1u;

    // Calculate chunks
    uint32_t chunks = (uint32_t)((len + FS_STORE_CHUNK_SIZE - 1) / FS_STORE_CHUNK_SIZE);
    if (chunks == 0) chunks = 1;

    // Write chunk keys
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

    // Commit binary meta last
    FsFileMeta meta = {
        .magic = FS_FILE_META_MAGIC,
        .version = new_ver,
        .size = (uint32_t)len,
        .chunks = chunks
    };
    return fs_meta_put(st, path, &meta) ? FS_NO_ERR : FS_ERR_IO;
}

ID fs_read_bytes(FsKvStore *st, const char *path)
{
    if (!fs_is_valid_path(path) || fs_is_dir_path(path)) return NULL;
    if (!st) return NULL;

    // Read binary meta
    FsFileMeta meta;
    if (!fs_meta_get(st, path, &meta)) return NULL;

    ID arr = make_byte_array((int)meta.size);
    if (!arr) return NULL;

    CljByteArray *ba = as_byte_array(arr);
    size_t copied = 0;
    for (uint32_t i = 0; i < meta.chunks && copied < meta.size; i++) {
        char ckey[FS_KEY_MAX];
        if (fs_make_chunk_key(ckey, path, meta.version, i) != FS_NO_ERR) break;

        size_t want = meta.size - copied;
        if (want > FS_STORE_CHUNK_SIZE) want = FS_STORE_CHUNK_SIZE;

        size_t saved = 0;
        size_t got = fs_kv_get(st, ckey, ba->data + copied, want, &saved);
        copied += got;
    }

    return AUTORELEASE(arr);
}

int64_t fs_stat_size(FsKvStore *st, const char *path)
{
    if (!fs_is_valid_path(path)) return -1;
    if (!st) return -1;

    FsFileMeta meta;
    if (!fs_meta_get(st, path, &meta)) return -1;

    return (int64_t)meta.size;
}

bool fs_exists(FsKvStore *st, const char *path)
{
    if (!fs_is_valid_path(path)) return false;
    if (!st) return false;
    FsFileMeta meta;
    return fs_meta_get(st, path, &meta);
}

bool fs_delete(FsKvStore *st, const char *path)
{
    if (!fs_is_valid_path(path)) return false;
    if (!st) return false;
    /* Remove chunk keys for current version (if present), then delete meta key. */
    FsFileMeta meta = {0};
    if (fs_meta_get(st, path, &meta)) {
        if (meta.version != 0) {
            for (uint32_t i = 0; i < meta.chunks; i++) {
                char ckey[FS_KEY_MAX];
                if (fs_make_chunk_key(ckey, path, meta.version, i) == FS_NO_ERR) {
                    (void)fs_kv_del(st, ckey);
                }
            }
        }
    }
    return fs_kv_del(st, path);
}

ID fs_list_dir_batch(FsKvStore *st,
                     const char *dir_path,
                     const char *after_key,
                     size_t batch_size,
                     char *out_last_key,
                     size_t out_last_key_cap)
{
    if (out_last_key && out_last_key_cap) out_last_key[0] = '\0';
    if (!fs_is_valid_path(dir_path) || !fs_is_dir_path(dir_path)) return NULL;
    if (!st || !st->db) return NULL;
    if (!out_last_key || out_last_key_cap == 0) return NULL;

    size_t prefix_len = strlen(dir_path);
    size_t after_len = after_key ? strlen(after_key) : 0;

    CljPersistentVector *vec = make_vector(8, STRONG);
    if (!vec) return NULL;

    tdb_kv_cursor_t* cur = NULL;
    tdb_status_t stc = tdb_kv_cursor_open_ge(st->db,
                                            dir_path, prefix_len,
                                            after_key, after_len,
                                            &cur);
    if (stc != TDB_OK) return NULL;

    size_t returned = 0;
    int has = 0;
    while (1) {
        stc = tdb_kv_cursor_next(cur, &has);
        if (stc != TDB_OK) { tdb_kv_cursor_close(cur); return NULL; }
        if (!has) {
            out_last_key[0] = '\0';
            break;
        }

        tdb_blob_t k = {0};
        stc = tdb_kv_cursor_key(cur, &k);
        if (stc != TDB_OK) { tdb_kv_cursor_close(cur); return NULL; }

        if (k.len + 1 > out_last_key_cap) { tdb_kv_cursor_close(cur); return NULL; }
        memcpy(out_last_key, k.data, k.len);
        out_last_key[k.len] = '\0';

        // Exclusive continuation: skip after_key itself if cursor positioned exactly there.
        if (after_key && after_len == k.len && memcmp(k.data, after_key, k.len) == 0) {
            continue;
        }

        char kstr[FS_KEY_MAX];
        if (fs_list_dir_key_is_direct_child(dir_path, prefix_len, k.data, k.len, kstr)) {
            // Entry map: {:path <string> :meta <map>}
            FsFileMeta meta = {0};
            bool has_formal = fs_meta_get(st, kstr, &meta);

            CljPersistentMap *meta_map = make_map(4);
            if (!meta_map) { tdb_kv_cursor_close(cur); return NULL; }
            if (has_formal) {
                map_assoc_inplace(&meta_map, SYM_KW_SIZE, fixnum((int32_t)meta.size));
                map_assoc_inplace(&meta_map, SYM_KW_CHUNKS, fixnum((int32_t)meta.chunks));
            }

            CljPersistentMap *entry_map = make_map(2);
            if (!entry_map) { RELEASE(meta_map); tdb_kv_cursor_close(cur); return NULL; }

            ID path_str = make_string(kstr);
            map_assoc_inplace(&entry_map, SYM_KW_PATH, path_str);
            RELEASE(path_str);

            map_assoc_inplace(&entry_map, SYM_KW_META, meta_map);
            RELEASE(meta_map);

            vector_conj_inplace(&vec, entry_map);
            RELEASE(entry_map);
            returned++;
            if (batch_size && returned >= batch_size) {
                break;
            }
        }
    }

    tdb_kv_cursor_close(cur);
    return AUTORELEASE(vec);
}
