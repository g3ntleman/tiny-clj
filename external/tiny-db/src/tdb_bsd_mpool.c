/*
 * mpool.c - Copy-on-Write Memory Pool for Log-Structured B-Tree
 *
 * Flash-friendly implementation: all writes are append-only.
 * No Read-Modify-Write, no in-place updates.
 *
 * Design:
 * - Pages stored with headers in append-only log
 * - In-memory page-map tracks logical page -> physical offset
 * - Small RAM cache for active pages
 * - CRC32 checksums for integrity
 */

#include "tdb_bsd_mpool.h"
#include "tdb_alloc.h"
#include "tdb_crc32.h"
#include "tdb_kv_bind.h"
#include "tdb_kv_internal.h"
#include "tdb_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<esp_heap_caps.h>)
#define TDB_MPOOL_HAVE_ESP_IDF 1
#include <esp_heap_caps.h>
#else
#define TDB_MPOOL_HAVE_ESP_IDF 0
#endif
#else
#define TDB_MPOOL_HAVE_ESP_IDF 0
#endif

static uint32_t g_cache_pagecount = TDB_MPOOL_CACHE_PAGES_DEFAULT;
static int g_psram_autosize = 1;

tdb_status_t tdb_mpool_set_cache_pagecount(uint32_t count) {
    if (count < 3u || count > (uint32_t)TDB_MPOOL_CACHE_PAGES_MAX)
        return TDB_ERR_INVALID_ARG;
    g_cache_pagecount = count;
    return TDB_OK;
}

uint32_t tdb_mpool_get_cache_pagecount(void) {
    return g_cache_pagecount;
}

tdb_status_t tdb_mpool_enable_psram_autosize(int enable) {
    g_psram_autosize = enable ? 1 : 0;
    return TDB_OK;
}

static int tdb_mpool_have_psram(void) {
#if TDB_MPOOL_HAVE_ESP_IDF
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
    return 0;
#endif
}

/* ============== On-wire header encoding (ESP32 byte order = little-endian) ============== */

/*
 * The mpool log header is written in a fixed byte order to allow moving storage
 * media (e.g. SD cards) between different CPU architectures.
 *
 * On-wire byte order: little-endian (ESP32).
 *
 * Layout (16 bytes):
 *   0  magic  u32 LE
 *   4  pgno   u32 LE
 *   8  crc32  u32 LE  (CRC over header bytes with this field set to 0, + page bytes)
 *   12 flags  u32 LE
 */
static inline void tdb_page_hdr_encode(uint8_t out[sizeof(tdb_page_hdr_t)], uint32_t magic,
                                      uint32_t pgno, uint32_t crc32, uint32_t flags) {
    tdb_u32_le_write(&out[0], magic);
    tdb_u32_le_write(&out[4], pgno);
    tdb_u32_le_write(&out[8], crc32);
    tdb_u32_le_write(&out[12], flags);
}

static inline void tdb_page_hdr_zero_crc(uint8_t io[sizeof(tdb_page_hdr_t)]) {
    tdb_u32_le_write(&io[8], 0u);
}

static inline uint32_t tdb_page_hdr_get_magic(const uint8_t in[sizeof(tdb_page_hdr_t)]) {
    return tdb_u32_le_read(&in[0]);
}

static inline uint32_t tdb_page_hdr_get_pgno(const uint8_t in[sizeof(tdb_page_hdr_t)]) {
    return tdb_u32_le_read(&in[4]);
}

static inline uint32_t tdb_page_hdr_get_crc(const uint8_t in[sizeof(tdb_page_hdr_t)]) {
    return tdb_u32_le_read(&in[8]);
}

static inline uint32_t tdb_page_hdr_get_flags(const uint8_t in[sizeof(tdb_page_hdr_t)]) {
    return tdb_u32_le_read(&in[12]);
}

static inline uint32_t tdb_page_hdr_crc_calc(const uint8_t hdr_bytes[sizeof(tdb_page_hdr_t)],
                                            const uint8_t* page_bytes, size_t page_len) {
    uint8_t tmp[sizeof(tdb_page_hdr_t)];
    memcpy(tmp, hdr_bytes, sizeof(tmp));
    tdb_page_hdr_zero_crc(tmp);
    uint32_t crc = tdb_crc32_ieee(tmp, sizeof(tmp), 0);
    crc = tdb_crc32_ieee(page_bytes, page_len, crc);
    return crc;
}

/*
 * Compute CRC for a record stored on flash without allocating a full (header+payload) buffer.
 * Reads payload in small chunks and feeds it into the CRC incrementally.
 */
static tdb_status_t tdb_page_hdr_crc_calc_from_flash(MPOOL* mp, uint32_t rec_off,
                                                   const uint8_t hdr_bytes[sizeof(tdb_page_hdr_t)],
                                                   uint32_t* out_crc) {
    if (out_crc)
        *out_crc = 0;
    if (!mp || !mp->bdev || !hdr_bytes || !out_crc)
        return TDB_ERR_INVALID_ARG;

    uint8_t tmp_hdr[sizeof(tdb_page_hdr_t)];
    memcpy(tmp_hdr, hdr_bytes, sizeof(tmp_hdr));
    tdb_page_hdr_zero_crc(tmp_hdr);

    uint32_t crc = tdb_crc32_ieee(tmp_hdr, sizeof(tmp_hdr), 0);

    uint32_t payload_off = rec_off + (uint32_t)sizeof(tdb_page_hdr_t);
    size_t remaining = mp->pagesize;
    uint8_t buf[64];
    while (remaining) {
        const size_t take = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        if (tdb_blockdev_read(mp->bdev, payload_off, buf, take) != TDB_OK)
            return TDB_ERR_IO;
        crc = tdb_crc32_ieee(buf, take, crc);
        payload_off += (uint32_t)take;
        remaining -= take;
    }

    *out_crc = crc;
    return TDB_OK;
}

/*
 * Compute CRC for a tombstone payload without allocating a full pagesize buffer.
 * Tombstone payload layout:
 * - first 4 bytes: next_free (u32 LE)
 * - remaining bytes: left as erased (0xFF)
 */
static uint32_t tdb_page_hdr_crc_calc_tombstone_erased_tail(
    const uint8_t hdr_bytes[sizeof(tdb_page_hdr_t)], const uint8_t next_free_le[4], size_t page_len) {
    uint8_t tmp_hdr[sizeof(tdb_page_hdr_t)];
    memcpy(tmp_hdr, hdr_bytes, sizeof(tmp_hdr));
    tdb_page_hdr_zero_crc(tmp_hdr);

    uint32_t crc = tdb_crc32_ieee(tmp_hdr, sizeof(tmp_hdr), 0);
    crc = tdb_crc32_ieee(next_free_le, 4u, crc);

    size_t rem = (page_len > 4u) ? (page_len - 4u) : 0u;
    uint8_t ff[64];
    memset(ff, 0xFF, sizeof(ff));
    while (rem) {
        const size_t take = (rem < sizeof(ff)) ? rem : sizeof(ff);
        crc = tdb_crc32_ieee(ff, take, crc);
        rem -= take;
    }
    return crc;
}

static inline uint32_t tdb_rec_size(const MPOOL* mp) {
    return (uint32_t)(sizeof(tdb_page_hdr_t) + mp->pagesize);
}

/**
 * Calculate the data region start offset.
 *
 * This must match tdb_bsd_blockfile's header region calculation
 * to avoid collision between B-Tree metadata and mpool pages.
 */
static uint32_t tdb_calc_data_base(void) {
    // Simplified: the mpool log starts exactly at the bound base offset.
    return tdb_kv_bound_base_offset();
}

/* Forward declaration (used by recovery in O(1)-RAM mode). */
static uint32_t gc_half_start(MPOOL* mp, int half);

/* ============== pgno -> offset mapping ============== */

#define TDB_OFF_INVALID 0xFFFFFFFFu

static void tdb_pg_cache_init(MPOOL* mp) {
    if (!mp)
        return;
    for (size_t i = 0; i < (size_t)TDB_MPOOL_PG_CACHE_ENTRIES; i++) {
        mp->pg_cache[i].pgno = PGNO_INVALID;
        mp->pg_cache[i].log_offset = TDB_OFF_INVALID;
    }
    mp->pg_cache_rr = 0;
}

static int tdb_pg_cache_lookup(MPOOL* mp, pgno_t pgno, uint32_t* out_off) {
    if (out_off)
        *out_off = TDB_OFF_INVALID;
    if (!mp || pgno == PGNO_INVALID || !out_off)
        return 0;
    for (size_t i = 0; i < (size_t)TDB_MPOOL_PG_CACHE_ENTRIES; i++) {
        if (mp->pg_cache[i].pgno == pgno) {
            *out_off = mp->pg_cache[i].log_offset;
            return 1;
        }
    }
    return 0;
}

static void tdb_pg_cache_insert(MPOOL* mp, pgno_t pgno, uint32_t off) {
    if (!mp || pgno == PGNO_INVALID)
        return;
    /* Update in place if present. */
    for (size_t i = 0; i < (size_t)TDB_MPOOL_PG_CACHE_ENTRIES; i++) {
        if (mp->pg_cache[i].pgno == pgno) {
            mp->pg_cache[i].log_offset = off;
            return;
        }
    }
    /* Insert using round-robin eviction. */
    if (TDB_MPOOL_PG_CACHE_ENTRIES == 0)
        return;
    size_t idx = (size_t)(mp->pg_cache_rr % (uint32_t)TDB_MPOOL_PG_CACHE_ENTRIES);
    mp->pg_cache[idx].pgno = pgno;
    mp->pg_cache[idx].log_offset = off;
    mp->pg_cache_rr++;
}

/* ============== Cache Operations ============== */

static inline void tdb_pin_slot(MPOOL* mp, tdb_cache_slot_t* slot) {
    if (!mp || !slot)
        return;
    if (!slot->pinned) {
        slot->pinned = 1;
        mp->pin_count++;
        if (mp->pin_count > mp->pin_peak)
            mp->pin_peak = mp->pin_count;
    }
}

static inline void tdb_unpin_slot(MPOOL* mp, tdb_cache_slot_t* slot) {
    if (!mp || !slot)
        return;
    if (slot->pinned) {
        slot->pinned = 0;
        if (mp->pin_count)
            mp->pin_count--;
    }
}

/**
 * Find cache slot containing a specific page.
 * @return Pointer to slot, or NULL if page not in cache
 */
static tdb_cache_slot_t* tdb_cache_find(MPOOL* mp, pgno_t pgno) {
    for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
        if (mp->cache[i].pgno == pgno) {
            return &mp->cache[i];
        }
    }
    return NULL;
}

/**
 * Find a free cache slot for loading a page.
 * Evicts clean, unpinned pages if necessary.
 * @return Pointer to available slot, or NULL if all slots are busy
 */
static tdb_cache_slot_t* tdb_cache_find_free(MPOOL* mp) {
    /* Find empty slot */
    for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
        if (mp->cache[i].pgno == PGNO_INVALID) {
            return &mp->cache[i];
        }
    }

    /* Find unpinned, non-dirty slot (evict) */
    for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
        if (!mp->cache[i].pinned && !mp->cache[i].dirty) {
            mp->cache[i].pgno = PGNO_INVALID;
            return &mp->cache[i];
        }
    }

    return NULL; /* All slots busy */
}

/**
 * Flush a dirty cache slot to the log.
 * @return 0 on success, -1 on error
 */
static int tdb_cache_flush_slot(MPOOL* mp, tdb_cache_slot_t* slot) {
    if (!slot->dirty)
        return 0;

    /* Call pgout filter if set */
    if (mp->pgout) {
        mp->pgout(mp->pgcookie, slot->pgno, slot->data);
    }

    /* Write record (header+payload) without a full scratch buffer. */
    const uint32_t total = tdb_rec_size(mp);
    uint8_t hdrb[sizeof(tdb_page_hdr_t)];

    tdb_page_hdr_encode(hdrb, TDB_PAGE_MAGIC, slot->pgno, 0u, 0u);
    uint32_t crc = tdb_page_hdr_crc_calc(hdrb, (const uint8_t*)slot->data, mp->pagesize);
    tdb_u32_le_write(&hdrb[8], crc);

    /* Check space */
    if ((uint64_t)mp->write_off + total > mp->bdev->geom.total_size_bytes) {
        return -1; /* Log full */
    }

    /* Program header and payload separately (both aligned). */
    tdb_status_t st = tdb_blockdev_prog(mp->bdev, mp->write_off, hdrb, sizeof(hdrb));
    if (st != TDB_OK)
        return -1;
    st = tdb_blockdev_prog(mp->bdev, mp->write_off + (uint32_t)sizeof(hdrb), slot->data, mp->pagesize);
    if (st != TDB_OK)
        return -1;

    /* Update mapping with new location */
    uint32_t new_offset = mp->write_off;
    mp->write_off += total;

    tdb_pg_cache_insert(mp, slot->pgno, new_offset);
    slot->log_offset = new_offset;
    slot->dirty = 0;

    return 0;
}

/* ============== Recovery ============== */

/**
 * Recover page-map by scanning the log from the beginning.
 * Rebuilds the page-map by reading all page headers in the log.
 * Later entries for the same page number override earlier ones.
 * @return 0 on success, non-zero on error
 */
static int tdb_mpool_scan_half(MPOOL* mp, uint32_t start, uint32_t end, uint32_t* out_end,
                              uint32_t* out_last_meta, pgno_t* inout_max_pgno, int* inout_any) {
    if (out_end)
        *out_end = start;
    if (out_last_meta)
        *out_last_meta = TDB_OFF_INVALID;
    if (!mp || !mp->bdev || !inout_max_pgno)
        return -1;

    const uint32_t total = tdb_rec_size(mp);
    uint32_t off = start;
    while (off + total <= end) {
        uint8_t hdrb[sizeof(tdb_page_hdr_t)];
        if (tdb_blockdev_read(mp->bdev, off, hdrb, sizeof(hdrb)) != TDB_OK)
            break;
        if (tdb_is_all_erased(hdrb, sizeof(hdrb)))
            break;
        if (tdb_page_hdr_get_magic(hdrb) != TDB_PAGE_MAGIC)
            break;

        /* Validate CRC to avoid accepting torn last records. */
        const uint32_t saved_crc = tdb_page_hdr_get_crc(hdrb);
        uint32_t crc = 0;
        if (tdb_page_hdr_crc_calc_from_flash(mp, off, hdrb, &crc) != TDB_OK)
            break;
        if (crc != saved_crc) {
            /* Best-effort: erase torn last record so we can safely reuse the block. */
            const uint32_t eg = mp->bdev->geom.erase_granularity;
            if (eg && (off % eg) == 0 && off + eg <= end) {
                (void)tdb_blockdev_erase(mp->bdev, off, eg);
            }
            break;
        }

        const pgno_t pgno = (pgno_t)tdb_page_hdr_get_pgno(hdrb);
        const uint32_t flags = tdb_page_hdr_get_flags(hdrb);
        if (inout_any)
            *inout_any = 1;
        if (pgno > *inout_max_pgno)
            *inout_max_pgno = pgno;
        if (pgno == 0 && !(flags & TDB_PAGE_FLAG_TOMBSTONE) && out_last_meta)
            *out_last_meta = off;

        (void)flags; /* Do not pre-populate the cache during recovery. */

        off += total;
    }

    if (out_end)
        *out_end = off;
    return 0;
}

static int tdb_mpool_recover(MPOOL* mp) {
    if (!mp || !mp->bdev)
        return -1;

    mp->npages = 0;
    pgno_t max_pgno = 0;
    int any = 0;

    tdb_pg_cache_init(mp);

    uint32_t end0 = 0, end1 = 0;
    uint32_t last_meta0 = TDB_OFF_INVALID, last_meta1 = TDB_OFF_INVALID;
    const uint32_t half0 = gc_half_start(mp, 0);
    const uint32_t half1 = gc_half_start(mp, 1);

    if (tdb_mpool_scan_half(mp, half0, half0 + mp->gc_half_size, &end0, &last_meta0, &max_pgno, &any) != 0)
        return -1;
    if (tdb_mpool_scan_half(mp, half1, half1 + mp->gc_half_size, &end1, &last_meta1, &max_pgno, &any) != 0)
        return -1;

    if (last_meta1 != TDB_OFF_INVALID && (last_meta0 == TDB_OFF_INVALID || last_meta1 > last_meta0)) {
        mp->gc_active_half = 1;
        mp->write_off = end1;
    } else {
        mp->gc_active_half = 0;
        mp->write_off = end0;
    }
 

    mp->npages = any ? (max_pgno + 1) : 0;
    return 0;
}

/* ============== Public API ============== */

MPOOL* mpool_open(void* key, int fd, pgno_t pagesize, pgno_t maxcache) {
    (void)key;
    (void)fd;
    (void)maxcache;

    tdb_blockdev_t* bdev = tdb_kv_bound_bdev();
    if (!bdev)
        return NULL;
    if (pagesize == 0)
        pagesize = TDB_MPOOL_DEFAULT_PAGE_SIZE;

    MPOOL* mp = (MPOOL*)calloc(1, sizeof(MPOOL));
    if (!mp)
        return NULL;

    mp->bdev = bdev;
    mp->pagesize = pagesize;
    mp->pin_count = 0;
    mp->pin_peak = 0;

    /* Cache pagecount: default 3, autosize when PSRAM is available. */
    uint32_t want_cache = g_cache_pagecount;
    if (g_psram_autosize && tdb_mpool_have_psram()) {
        if (want_cache < 16u)
            want_cache = 16u;
    }
    if (want_cache < 3u)
        want_cache = 3u;
    if (want_cache > (uint32_t)TDB_MPOOL_CACHE_PAGES_MAX)
        want_cache = (uint32_t)TDB_MPOOL_CACHE_PAGES_MAX;
    mp->cache_pagecount = want_cache;

    /* Calculate data region start */
    mp->data_base = tdb_calc_data_base();
    mp->write_off = mp->data_base;

    /* GC: Split remaining space into two halves for ping-pong */
    uint32_t total_data = bdev->geom.total_size_bytes - mp->data_base;
    mp->gc_half_size = total_data / 2;
    mp->gc_active_half = 0;
    mp->gc_in_progress = 0;
    mp->gc_next_pgno = 0;
    mp->free_head = PGNO_INVALID;
    mp->owner_kv = NULL;

    mp->npages = 0;

    /* Allocate cache backing store and scratch buffer sized to pagesize. */
    mp->cache_mem = (uint8_t*)tdb_alloc((size_t)mp->cache_pagecount * (size_t)mp->pagesize,
                                       TDB_ALLOC_KIND_CACHE);
    if (!mp->cache_mem) {
        free(mp);
        return NULL;
    }
    /* Clear cache - mark all slots as empty */
    for (uint32_t i = 0; i < (uint32_t)TDB_MPOOL_CACHE_PAGES_MAX; i++) {
        mp->cache[i].pgno = PGNO_INVALID;
        mp->cache[i].log_offset = 0;
        mp->cache[i].dirty = 0;
        mp->cache[i].pinned = 0;
        mp->cache[i].data =
            (i < mp->cache_pagecount) ? (mp->cache_mem + ((size_t)i * (size_t)mp->pagesize)) : NULL;
    }

    /* Recover page-map from existing log */
    if (tdb_mpool_recover(mp) != 0) {
        tdb_free(mp->cache_mem);
        free(mp);
        return NULL;
    }

    return mp;
}

void mpool_filter(MPOOL* mp, void (*pgin)(void*, pgno_t, void*),
                  void (*pgout)(void*, pgno_t, void*), void* pgcookie) {
    if (!mp)
        return;
    mp->pgin = pgin;
    mp->pgout = pgout;
    mp->pgcookie = pgcookie;
}

void* mpool_new(MPOOL* mp, pgno_t* pgnoaddr) {
    if (!mp || !pgnoaddr)
        return NULL;

    pgno_t pgno = PGNO_INVALID;
    /* Simple O(1)-RAM policy: do not reuse freed pgno numbers. */
    mp->free_head = PGNO_INVALID;
    pgno = mp->npages++; /* post-increment: first call returns 0 */

    /* Find cache slot */
    tdb_cache_slot_t* slot = tdb_cache_find_free(mp);
    if (!slot) {
        /* Need to flush a dirty page first */
        for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
            if (mp->cache[i].dirty && !mp->cache[i].pinned) {
                tdb_cache_flush_slot(mp, &mp->cache[i]);
                mp->cache[i].pgno = PGNO_INVALID;
                slot = &mp->cache[i];
                break;
            }
        }
    }
    if (!slot) {
        /* Cache exhausted: all pages are pinned (bt_split peak exceeded cache pages). */
        errno = ENOMEM;
        return NULL;
    }

    /* Initialize new page */
    slot->pgno = pgno;
    slot->log_offset = 0xFFFFFFFF; /* Not yet in log */
    slot->dirty = 1;               /* Will need to be written */
    tdb_pin_slot(mp, slot);
    memset(slot->data, 0, mp->pagesize);

    *pgnoaddr = pgno;

    if (mp->owner_kv) {
        mp->owner_kv->gc_dirty = 1;
        mp->owner_kv->free_head = (uint32_t)mp->free_head;
        mp->owner_kv->alloc_next = (uint32_t)mp->npages;
    }
    return slot->data;
}

static uint32_t tdb_scan_back_in_range(MPOOL* mp, uint32_t start_off_exclusive, uint32_t stop_off_inclusive,
                                      pgno_t want_pgno, int* out_is_tombstone) {
    if (out_is_tombstone)
        *out_is_tombstone = 0;
    if (!mp)
        return TDB_OFF_INVALID;

    const uint32_t total = tdb_rec_size(mp);
    if (start_off_exclusive < total)
        return TDB_OFF_INVALID;
    if (start_off_exclusive <= stop_off_inclusive)
        return TDB_OFF_INVALID;

    uint32_t off = start_off_exclusive;
    while (off >= stop_off_inclusive + total) {
        off -= total;

        uint8_t hdrb[sizeof(tdb_page_hdr_t)];
        if (tdb_blockdev_read(mp->bdev, off, hdrb, sizeof(hdrb)) != TDB_OK)
            goto next;
        if (tdb_page_hdr_get_magic(hdrb) != TDB_PAGE_MAGIC)
            goto next;
        if ((pgno_t)tdb_page_hdr_get_pgno(hdrb) != want_pgno)
            goto next;

        /* Candidate: validate record CRC (stream payload). */
        const uint32_t saved_crc = tdb_page_hdr_get_crc(hdrb);
        uint32_t crc = 0;
        if (tdb_page_hdr_crc_calc_from_flash(mp, off, hdrb, &crc) != TDB_OK)
            goto next;
        if (crc != saved_crc)
            goto next;

        if (tdb_page_hdr_get_flags(hdrb) & TDB_PAGE_FLAG_TOMBSTONE) {
            if (out_is_tombstone)
                *out_is_tombstone = 1;
        }
        return off;

    next:
        if (off < stop_off_inclusive + total)
            break;
    }
    return TDB_OFF_INVALID;
}

static uint32_t tdb_scan_back_find_offset(MPOOL* mp, pgno_t want_pgno, int* out_is_tombstone) {
    if (out_is_tombstone)
        *out_is_tombstone = 0;
    if (!mp)
        return TDB_OFF_INVALID;

    /* Prefer active half, then fall back to the other half if GC is in progress. */
    uint32_t active_start = gc_half_start(mp, mp->gc_active_half);
    uint32_t active_end = mp->write_off;
    uint32_t off = tdb_scan_back_in_range(mp, active_end, active_start, want_pgno, out_is_tombstone);
    if (off != TDB_OFF_INVALID || !mp->gc_in_progress)
        return off;

    int other_half = 1 - mp->gc_active_half;
    uint32_t other_start = gc_half_start(mp, other_half);
    uint32_t other_end = other_start + mp->gc_half_size;
    return tdb_scan_back_in_range(mp, other_end, other_start, want_pgno, out_is_tombstone);
}

void* mpool_get(MPOOL* mp, pgno_t pgno, unsigned int flags) {
    (void)flags;
    if (!mp)
        return NULL;

    /* Check cache first */
    tdb_cache_slot_t* slot = tdb_cache_find(mp, pgno);
    if (slot) {
        tdb_pin_slot(mp, slot);
        return slot->data;
    }

    uint32_t log_off = TDB_OFF_INVALID;
    if (!tdb_pg_cache_lookup(mp, pgno, &log_off) || log_off == TDB_OFF_INVALID) {
        int is_tomb = 0;
        log_off = tdb_scan_back_find_offset(mp, pgno, &is_tomb);
        if (log_off == TDB_OFF_INVALID || is_tomb) {
            errno = EINVAL;
            if (log_off != TDB_OFF_INVALID)
                tdb_pg_cache_insert(mp, pgno, log_off);
            return NULL;
        }
        tdb_pg_cache_insert(mp, pgno, log_off);
    }

    /* Allocate cache slot */
    slot = tdb_cache_find_free(mp);
    if (!slot) {
        /* Flush a dirty page to make room */
        for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
            if (mp->cache[i].dirty && !mp->cache[i].pinned) {
                tdb_cache_flush_slot(mp, &mp->cache[i]);
                mp->cache[i].pgno = PGNO_INVALID;
                slot = &mp->cache[i];
                break;
            }
        }
    }
    if (!slot) {
        /* Cache exhausted: all pages are pinned (bt_split peak exceeded cache pages). */
        errno = ENOMEM;
        return NULL;
    }

    /* Read header then payload directly into the cache slot. */
    uint8_t hdrb[sizeof(tdb_page_hdr_t)];
    tdb_status_t st = tdb_blockdev_read(mp->bdev, log_off, hdrb, sizeof(hdrb));
    if (st != TDB_OK)
        return NULL;

    /* Validate header quickly. */
    if (tdb_page_hdr_get_magic(hdrb) != TDB_PAGE_MAGIC)
        return NULL;
    if ((pgno_t)tdb_page_hdr_get_pgno(hdrb) != pgno)
        return NULL;

    st = tdb_blockdev_read(mp->bdev, log_off + (uint32_t)sizeof(tdb_page_hdr_t), slot->data, mp->pagesize);
    if (st != TDB_OK)
        return NULL;

    /* Verify CRC */
    const uint32_t saved_crc = tdb_page_hdr_get_crc(hdrb);
    const uint32_t crc = tdb_page_hdr_crc_calc(hdrb, slot->data, mp->pagesize);
    if (crc != saved_crc)
        return NULL; /* Corrupt */

    /* Tombstones are not readable as pages (treat as non-existent). */
    if (tdb_page_hdr_get_flags(hdrb) & TDB_PAGE_FLAG_TOMBSTONE) {
        errno = EINVAL;
        return NULL;
    }

    /* Call pgin filter if set */
    if (mp->pgin) {
        mp->pgin(mp->pgcookie, pgno, slot->data);
    }

    slot->pgno = pgno;
    slot->log_offset = log_off;
    slot->dirty = 0;
    tdb_pin_slot(mp, slot);

    return slot->data;
}

int mpool_put(MPOOL* mp, void* page, unsigned int flags) {
    if (!mp || !page)
        return -1;

    /* Find cache slot containing this page */
    for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
        if (!mp->cache[i].data)
            continue;
        if (mp->cache[i].data == page || (page >= (void*)mp->cache[i].data &&
                                          page < (void*)(mp->cache[i].data + mp->pagesize))) {

            if (flags & MPOOL_DIRTY) {
                mp->cache[i].dirty = 1;
            }
            tdb_unpin_slot(mp, &mp->cache[i]);
            return 0;
        }
    }

    return -1; /* Page not found in cache */
}

int mpool_sync(MPOOL* mp) {
    if (!mp)
        return -1;

    /* Flush all dirty pages */
    for (uint32_t i = 0; i < mp->cache_pagecount; i++) {
        if (mp->cache[i].dirty) {
            if (tdb_cache_flush_slot(mp, &mp->cache[i]) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

int mpool_close(MPOOL* mp) {
    if (!mp)
        return -1;

    /* Sync before close */
    mpool_sync(mp);

    tdb_free(mp->cache_mem);
    free(mp);
    return 0;
}

uint32_t mpool_peak_pinned(const MPOOL* mp) {
    return mp ? mp->pin_peak : 0;
}

/* ============== Garbage Collection ============== */

/*
 * Get the start offset for a given half (0 or 1).
 */
static uint32_t gc_half_start(MPOOL* mp, int half) {
    return mp->data_base + (half * mp->gc_half_size);
}

/*
 * Check if GC is needed (active half is more than 75% full).
 */
static int gc_needed(MPOOL* mp) {
    uint32_t half_start = gc_half_start(mp, mp->gc_active_half);
    uint32_t used = mp->write_off - half_start;
    return used > (mp->gc_half_size * 3 / 4);
}

/* ============== Garbage Collection (O(1)-RAM) ============== */
static uint32_t gc_half_end(MPOOL* mp, int half) {
    return gc_half_start(mp, half) + mp->gc_half_size;
}

static int gc_copy_page_o1ram(MPOOL* mp, int old_half, pgno_t pgno) {
    const uint32_t total = tdb_rec_size(mp);
    const uint32_t new_start = gc_half_start(mp, mp->gc_active_half);
    const uint32_t new_end = mp->write_off;
    const uint32_t old_start = gc_half_start(mp, old_half);
    const uint32_t old_end = gc_half_end(mp, old_half);

    /* If pgno already exists in the new half, keep it (do not overwrite). */
    {
        int is_tomb = 0;
        uint32_t new_off = tdb_scan_back_in_range(mp, new_end, new_start, pgno, &is_tomb);
        if (new_off != TDB_OFF_INVALID) {
            return 0;
        }
    }

    int is_tomb = 0;
    uint32_t old_off = tdb_scan_back_in_range(mp, old_end, old_start, pgno, &is_tomb);
    if (old_off == TDB_OFF_INVALID || is_tomb)
        return 0;

    /* Read + verify old record (header + streaming CRC). */
    uint8_t hdrb[sizeof(tdb_page_hdr_t)];
    tdb_status_t st = tdb_blockdev_read(mp->bdev, old_off, hdrb, sizeof(hdrb));
    if (st != TDB_OK)
        return -1;
    if (tdb_page_hdr_get_magic(hdrb) != TDB_PAGE_MAGIC)
        return -1;
    if ((pgno_t)tdb_page_hdr_get_pgno(hdrb) != pgno)
        return -1;
    const uint32_t saved_crc = tdb_page_hdr_get_crc(hdrb);
    uint32_t crc = 0;
    if (tdb_page_hdr_crc_calc_from_flash(mp, old_off, hdrb, &crc) != TDB_OK)
        return -1;
    if (crc != saved_crc)
        return -1;
    if (tdb_page_hdr_get_flags(hdrb) & TDB_PAGE_FLAG_TOMBSTONE)
        return 0;

    /* Write to new location. */
    if ((uint64_t)mp->write_off + (uint64_t)total > mp->bdev->geom.total_size_bytes)
        return -1;
    const uint32_t new_off = mp->write_off;
    st = tdb_blockdev_prog(mp->bdev, new_off, hdrb, sizeof(hdrb));
    if (st != TDB_OK)
        return -1;
    {
        uint32_t src = old_off + (uint32_t)sizeof(tdb_page_hdr_t);
        uint32_t dst = new_off + (uint32_t)sizeof(tdb_page_hdr_t);
        size_t remaining = mp->pagesize;
        uint8_t buf[64];
        while (remaining) {
            const size_t take = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
            if (tdb_blockdev_read(mp->bdev, src, buf, take) != TDB_OK)
                return -1;
            if (tdb_blockdev_prog(mp->bdev, dst, buf, take) != TDB_OK)
                return -1;
            src += (uint32_t)take;
            dst += (uint32_t)take;
            remaining -= take;
        }
    }
    mp->write_off += total;
    tdb_pg_cache_insert(mp, pgno, new_off);
    return 0;
}

int mpool_gc_step(MPOOL* mp, size_t budget_bytes) {
    if (!mp)
        return -1;

    /* Start GC if not in progress and needed. */
    if (!mp->gc_in_progress) {
        if (!gc_needed(mp)) {
            return 0;
        }

        if (mpool_sync(mp) != 0)
            return -1;

        int old_half = mp->gc_active_half;
        int new_half = 1 - old_half;
        uint32_t new_start = gc_half_start(mp, new_half);

        /* Erase new half. */
        uint32_t eg = mp->bdev->geom.erase_granularity;
        uint32_t erase_start = (new_start / eg) * eg;
        uint32_t erase_len = ((mp->gc_half_size + eg - 1) / eg) * eg;
        if (tdb_blockdev_erase(mp->bdev, erase_start, erase_len) != TDB_OK)
            return -1;

        /* Switch active writes to the new half. */
        mp->write_off = new_start;
        mp->gc_active_half = new_half;
        mp->gc_in_progress = 1;
        mp->gc_next_pgno = 0;
        mp->free_head = PGNO_INVALID; /* no reuse in O(1)-RAM mode */
        tdb_pg_cache_init(mp);
    }

    int old_half = 1 - mp->gc_active_half;
    const uint32_t total = tdb_rec_size(mp);
    size_t bytes = 0;

    while (mp->gc_next_pgno < mp->npages) {
        if (budget_bytes > 0 && bytes >= budget_bytes) {
            return 1;
        }
        if (gc_copy_page_o1ram(mp, old_half, mp->gc_next_pgno) != 0)
            return -1;
        bytes += total;
        mp->gc_next_pgno++;
    }

    /* GC complete: stop dual-half reads and erase old half. */
    mp->gc_in_progress = 0;
    {
        uint32_t old_start = gc_half_start(mp, old_half);
        uint32_t eg = mp->bdev->geom.erase_granularity;
        uint32_t erase_start = (old_start / eg) * eg;
        uint32_t erase_len = ((mp->gc_half_size + eg - 1) / eg) * eg;
        (void)tdb_blockdev_erase(mp->bdev, erase_start, erase_len);
    }

    return 0;
}

int mpool_free_pgno(MPOOL* mp, pgno_t pgno) {
    if (!mp || pgno == PGNO_INVALID)
        return -1;

    tdb_cache_slot_t* slot = tdb_cache_find(mp, pgno);
    if (slot) {
        if (slot->pinned)
            return -1;
        slot->pgno = PGNO_INVALID;
        slot->dirty = 0;
        slot->log_offset = 0;
    }

    const uint32_t total = tdb_rec_size(mp);
    uint8_t hdrb[sizeof(tdb_page_hdr_t)];
    tdb_page_hdr_encode(hdrb, TDB_PAGE_MAGIC, pgno, 0u, TDB_PAGE_FLAG_TOMBSTONE);

    uint8_t nextb[4];
    tdb_u32_le_write(nextb, (uint32_t)PGNO_INVALID);
    const uint32_t crc = tdb_page_hdr_crc_calc_tombstone_erased_tail(hdrb, nextb, mp->pagesize);
    tdb_u32_le_write(&hdrb[8], crc);
    if ((uint64_t)mp->write_off + (uint64_t)total > mp->bdev->geom.total_size_bytes)
        return -1;

    const uint32_t tomb_off = mp->write_off;
    if (tdb_blockdev_prog(mp->bdev, tomb_off, hdrb, sizeof(hdrb)) != TDB_OK)
        return -1;
    if (tdb_blockdev_prog(mp->bdev, tomb_off + (uint32_t)sizeof(tdb_page_hdr_t), nextb, sizeof(nextb)) != TDB_OK)
        return -1;
    mp->write_off += total;

    /* Cache tombstone so lookups can fail fast. */
    tdb_pg_cache_insert(mp, pgno, tomb_off);

    if (mp->owner_kv) {
        mp->owner_kv->gc_dirty = 1;
        mp->owner_kv->free_head = (uint32_t)PGNO_INVALID;
        mp->owner_kv->alloc_next = (uint32_t)mp->npages;
    }
    return 0;
}

