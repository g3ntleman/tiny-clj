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

#include "ft_bsd_mpool.h"
#include "ft_crc32.h"
#include "ft_kv_bind.h"
#include "ft_kv_internal.h"
#include "ft_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

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
static inline void ft_page_hdr_encode(uint8_t out[sizeof(ft_page_hdr_t)], uint32_t magic,
                                      uint32_t pgno, uint32_t crc32, uint32_t flags) {
    ft_u32_le_write(&out[0], magic);
    ft_u32_le_write(&out[4], pgno);
    ft_u32_le_write(&out[8], crc32);
    ft_u32_le_write(&out[12], flags);
}

static inline void ft_page_hdr_zero_crc(uint8_t io[sizeof(ft_page_hdr_t)]) {
    ft_u32_le_write(&io[8], 0u);
}

static inline uint32_t ft_page_hdr_get_magic(const uint8_t in[sizeof(ft_page_hdr_t)]) {
    return ft_u32_le_read(&in[0]);
}

static inline uint32_t ft_page_hdr_get_pgno(const uint8_t in[sizeof(ft_page_hdr_t)]) {
    return ft_u32_le_read(&in[4]);
}

static inline uint32_t ft_page_hdr_get_crc(const uint8_t in[sizeof(ft_page_hdr_t)]) {
    return ft_u32_le_read(&in[8]);
}

static inline uint32_t ft_page_hdr_get_flags(const uint8_t in[sizeof(ft_page_hdr_t)]) {
    return ft_u32_le_read(&in[12]);
}

static inline uint32_t ft_page_hdr_crc_calc(const uint8_t hdr_bytes[sizeof(ft_page_hdr_t)],
                                            const uint8_t* page_bytes, size_t page_len) {
    uint8_t tmp[sizeof(ft_page_hdr_t)];
    memcpy(tmp, hdr_bytes, sizeof(tmp));
    ft_page_hdr_zero_crc(tmp);
    uint32_t crc = ft_crc32_ieee(tmp, sizeof(tmp), 0);
    crc = ft_crc32_ieee(page_bytes, page_len, crc);
    return crc;
}

static inline uint32_t ft_rec_size(const MPOOL* mp) {
    return (uint32_t)(sizeof(ft_page_hdr_t) + mp->pagesize);
}

/**
 * Calculate the data region start offset.
 *
 * This must match ft_bsd_blockfile's header region calculation
 * to avoid collision between B-Tree metadata and mpool pages.
 */
static uint32_t ft_calc_data_base(void) {
    // Simplified: the mpool log starts exactly at the bound base offset.
    return ft_kv_bound_base_offset();
}

/* Forward declaration (used by recovery in O(1)-RAM mode). */
static uint32_t gc_half_start(MPOOL* mp, int half);

/* ============== pgno -> offset mapping ============== */

#define FT_OFF_INVALID 0xFFFFFFFFu

#if FT_MPOOL_O1_RAM

static void ft_pg_cache_init(MPOOL* mp) {
    if (!mp)
        return;
    for (size_t i = 0; i < (size_t)FT_MPOOL_PG_CACHE_ENTRIES; i++) {
        mp->pg_cache[i].pgno = PGNO_INVALID;
        mp->pg_cache[i].log_offset = FT_OFF_INVALID;
    }
    mp->pg_cache_rr = 0;
}

static int ft_pg_cache_lookup(MPOOL* mp, pgno_t pgno, uint32_t* out_off) {
    if (out_off)
        *out_off = FT_OFF_INVALID;
    if (!mp || pgno == PGNO_INVALID || !out_off)
        return 0;
    for (size_t i = 0; i < (size_t)FT_MPOOL_PG_CACHE_ENTRIES; i++) {
        if (mp->pg_cache[i].pgno == pgno) {
            *out_off = mp->pg_cache[i].log_offset;
            return 1;
        }
    }
    return 0;
}

static void ft_pg_cache_insert(MPOOL* mp, pgno_t pgno, uint32_t off) {
    if (!mp || pgno == PGNO_INVALID)
        return;
    /* Update in place if present. */
    for (size_t i = 0; i < (size_t)FT_MPOOL_PG_CACHE_ENTRIES; i++) {
        if (mp->pg_cache[i].pgno == pgno) {
            mp->pg_cache[i].log_offset = off;
            return;
        }
    }
    /* Insert using round-robin eviction. */
    if (FT_MPOOL_PG_CACHE_ENTRIES == 0)
        return;
    size_t idx = (size_t)(mp->pg_cache_rr % (uint32_t)FT_MPOOL_PG_CACHE_ENTRIES);
    mp->pg_cache[idx].pgno = pgno;
    mp->pg_cache[idx].log_offset = off;
    mp->pg_cache_rr++;
}

#else

static int ft_offsets_reserve(MPOOL* mp, size_t want_cap) {
    if (mp->page_offsets_cap >= want_cap)
        return 0;
    size_t new_cap = (mp->page_offsets_cap == 0) ? 64 : mp->page_offsets_cap;
    while (new_cap < want_cap) {
        new_cap *= 2;
        if (new_cap < mp->page_offsets_cap)
            return -1; /* overflow */
    }
    uint32_t* p = (uint32_t*)realloc(mp->page_offsets, new_cap * sizeof(*p));
    if (!p)
        return -1;
    for (size_t i = mp->page_offsets_cap; i < new_cap; i++) {
        p[i] = FT_OFF_INVALID;
    }
    mp->page_offsets = p;
    mp->page_offsets_cap = new_cap;
    return 0;
}

static inline uint32_t ft_offsets_get(MPOOL* mp, pgno_t pgno) {
    if (!mp || pgno == PGNO_INVALID)
        return FT_OFF_INVALID;
    if ((size_t)pgno >= mp->page_offsets_cap)
        return FT_OFF_INVALID;
    return mp->page_offsets[pgno];
}

static int ft_offsets_set(MPOOL* mp, pgno_t pgno, uint32_t log_offset) {
    if (!mp || pgno == PGNO_INVALID)
        return -1;
    if (ft_offsets_reserve(mp, (size_t)pgno + 1) != 0)
        return -1;
    mp->page_offsets[pgno] = log_offset;
    return 0;
}

#endif /* FT_MPOOL_O1_RAM */

/* ============== Cache Operations ============== */

/**
 * Find cache slot containing a specific page.
 * @return Pointer to slot, or NULL if page not in cache
 */
static ft_cache_slot_t* ft_cache_find(MPOOL* mp, pgno_t pgno) {
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
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
static ft_cache_slot_t* ft_cache_find_free(MPOOL* mp) {
    /* Find empty slot */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
        if (mp->cache[i].pgno == PGNO_INVALID) {
            return &mp->cache[i];
        }
    }

    /* Find unpinned, non-dirty slot (evict) */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
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
static int ft_cache_flush_slot(MPOOL* mp, ft_cache_slot_t* slot) {
    if (!slot->dirty)
        return 0;

    /* Call pgout filter if set */
    if (mp->pgout) {
        mp->pgout(mp->pgcookie, slot->pgno, slot->data);
    }

    /* Prepare full record (header+payload) and write in a single prog(). */
    const uint32_t total = ft_rec_size(mp);
    uint8_t* rec = mp->scratch_rec;
    uint8_t* hdrb = rec;
    uint8_t* payload = rec + sizeof(ft_page_hdr_t);
    memcpy(payload, slot->data, mp->pagesize);

    ft_page_hdr_encode(hdrb, FT_PAGE_MAGIC, slot->pgno, 0u, 0u);
    uint32_t crc = ft_page_hdr_crc_calc(hdrb, payload, mp->pagesize);
    ft_u32_le_write(&hdrb[8], crc);

    /* Check space */
    if ((uint64_t)mp->write_off + total > mp->bdev->geom.total_size_bytes) {
        return -1; /* Log full */
    }

    /* Write full record (header+payload) */
    ft_status_t st = ft_blockdev_prog(mp->bdev, mp->write_off, rec, total);
    if (st != FT_OK)
        return -1;

    /* Update mapping with new location */
    uint32_t new_offset = mp->write_off;
    mp->write_off += total;

#if FT_MPOOL_O1_RAM
    ft_pg_cache_insert(mp, slot->pgno, new_offset);
#else
    (void)ft_offsets_set(mp, slot->pgno, new_offset);
#endif
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
static int ft_mpool_recover(MPOOL* mp) {
    if (!mp || !mp->bdev)
        return -1;

    mp->npages = 0;

    const uint32_t total = ft_rec_size(mp);
    const uint32_t half0 = gc_half_start(mp, 0);
    const uint32_t half1 = gc_half_start(mp, 1);

    uint32_t end0 = half0;
    uint32_t end1 = half1;
    uint32_t last_meta0 = FT_OFF_INVALID;
    uint32_t last_meta1 = FT_OFF_INVALID;
    pgno_t max_pgno = 0;

    /* Helper lambda-like macro for scanning a half. */
#define SCAN_HALF(half_start, half_end, out_end, out_last_meta)                                  \
    do {                                                                                          \
        uint32_t off = (half_start);                                                              \
        while (off + total <= (half_end)) {                                                       \
            uint8_t hdrb[sizeof(ft_page_hdr_t)];                                                  \
            if (ft_blockdev_read(mp->bdev, off, hdrb, sizeof(hdrb)) != FT_OK)                     \
                break;                                                                            \
            if (ft_is_all_erased(hdrb, sizeof(hdrb)))                                             \
                break;                                                                            \
            if (ft_page_hdr_get_magic(hdrb) != FT_PAGE_MAGIC)                                     \
                break;                                                                            \
            /* Validate CRC to avoid accepting torn last records. */                              \
            uint8_t* rec = mp->scratch_rec;                                                       \
            if (ft_blockdev_read(mp->bdev, off, rec, total) != FT_OK)                             \
                break;                                                                            \
            const uint8_t* full_hdr = rec;                                                        \
            const uint8_t* payload = rec + sizeof(ft_page_hdr_t);                                 \
            const uint32_t saved_crc = ft_page_hdr_get_crc(full_hdr);                             \
            const uint32_t crc = ft_page_hdr_crc_calc(full_hdr, payload, mp->pagesize);           \
            if (crc != saved_crc)                                                                 \
                break;                                                                            \
            const pgno_t pgno = (pgno_t)ft_page_hdr_get_pgno(full_hdr);                            \
            const uint32_t flags = ft_page_hdr_get_flags(full_hdr);                               \
            if (pgno >= (max_pgno))                                                               \
                (max_pgno) = pgno;                                                                \
            if (pgno == 0 && !(flags & FT_PAGE_FLAG_TOMBSTONE))                                   \
                (out_last_meta) = off;                                                            \
#if FT_MPOOL_O1_RAM                                                                               \
            if (!(flags & FT_PAGE_FLAG_TOMBSTONE))                                                \
                ft_pg_cache_insert(mp, pgno, off);                                                 \
#else                                                                                             \
            if (ft_offsets_set(mp, pgno, off) != 0)                                                \
                return -1;                                                                        \
#endif                                                                                            \
            off += total;                                                                         \
        }                                                                                         \
        (out_end) = off;                                                                          \
    } while (0)

#if FT_MPOOL_O1_RAM
    /* Init cache before populating with last-seen records. */
    ft_pg_cache_init(mp);
#endif

#if FT_MPOOL_O1_RAM
    /* In O(1)-RAM mode, scan both halves to be robust across GC power-loss. */
    SCAN_HALF(half0, half0 + mp->gc_half_size, end0, last_meta0);
    SCAN_HALF(half1, half1 + mp->gc_half_size, end1, last_meta1);

    /* Pick the half containing the most recent valid meta page (pgno 0). */
    if (last_meta1 != FT_OFF_INVALID && (last_meta0 == FT_OFF_INVALID || last_meta1 > last_meta0)) {
        mp->gc_active_half = 1;
        mp->write_off = end1;
    } else {
        mp->gc_active_half = 0;
        mp->write_off = end0;
    }
#else
    /* Default (host) mode: scan forward from data_base. */
    uint32_t dummy_last_meta = FT_OFF_INVALID;
    SCAN_HALF(mp->data_base, mp->bdev->geom.total_size_bytes, mp->write_off, dummy_last_meta);
    mp->gc_active_half = 0;
#endif

    mp->npages = (max_pgno == 0 && (last_meta0 == FT_OFF_INVALID && last_meta1 == FT_OFF_INVALID))
                     ? 0
                     : (max_pgno + 1);

    return 0;
#undef SCAN_HALF
}

/* ============== Public API ============== */

MPOOL* mpool_open(void* key, int fd, pgno_t pagesize, pgno_t maxcache) {
    (void)key;
    (void)fd;
    (void)maxcache;

    ft_blockdev_t* bdev = ft_kv_bound_bdev();
    if (!bdev)
        return NULL;
    if (pagesize == 0)
        pagesize = FT_MPOOL_DEFAULT_PAGE_SIZE;

    MPOOL* mp = (MPOOL*)calloc(1, sizeof(MPOOL));
    if (!mp)
        return NULL;

    mp->bdev = bdev;
    mp->pagesize = pagesize;

    /* Calculate data region start */
    mp->data_base = ft_calc_data_base();
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
#if !FT_MPOOL_O1_RAM
    mp->page_offsets = NULL;
    mp->page_offsets_cap = 0;
#endif

#if !FT_MPOOL_O1_RAM
    /* Heuristic initial reserve: reduce realloc churn in host mode. */
    {
        uint32_t data_bytes = bdev->geom.total_size_bytes - mp->data_base;
        uint32_t rec_bytes = (uint32_t)(sizeof(ft_page_hdr_t) + mp->pagesize);
        size_t est = (rec_bytes > 0) ? (size_t)(data_bytes / rec_bytes + 16) : 64;
        if (est < 64)
            est = 64;
        if (ft_offsets_reserve(mp, est) != 0) {
            free(mp);
            return NULL;
        }
    }
#endif

    /* Allocate cache backing store and scratch buffer sized to pagesize. */
    mp->cache_mem = (uint8_t*)malloc((size_t)FT_MPOOL_CACHE_PAGES * (size_t)mp->pagesize);
    if (!mp->cache_mem) {
        free(mp);
        return NULL;
    }
    mp->scratch_rec = (uint8_t*)malloc((size_t)sizeof(ft_page_hdr_t) + (size_t)mp->pagesize);
    if (!mp->scratch_rec) {
        free(mp->cache_mem);
        free(mp);
        return NULL;
    }

    /* Clear cache - mark all slots as empty */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
        mp->cache[i].pgno = PGNO_INVALID;
        mp->cache[i].log_offset = 0;
        mp->cache[i].dirty = 0;
        mp->cache[i].pinned = 0;
        mp->cache[i].data = mp->cache_mem + ((size_t)i * (size_t)mp->pagesize);
    }

    /* Recover page-map from existing log */
    if (ft_mpool_recover(mp) != 0) {
        free(mp->scratch_rec);
        free(mp->cache_mem);
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
#if !FT_MPOOL_O1_RAM
    /* Prefer reusing a pgno freed via tombstone free-list. */
    if (mp->free_head != PGNO_INVALID) {
        const pgno_t head = mp->free_head;
        uint32_t tomb_off = ft_offsets_get(mp, head);
        if (tomb_off != FT_OFF_INVALID) {
            /*
             * Fast-path: only read the header + 4 bytes of payload (next pointer).
             * This avoids a full record read on every free-list pop.
             */
            uint8_t hdrb[sizeof(ft_page_hdr_t)];
            ft_status_t st = ft_blockdev_read(mp->bdev, tomb_off, hdrb, sizeof(hdrb));
            if (st == FT_OK && ft_page_hdr_get_magic(hdrb) == FT_PAGE_MAGIC &&
                (pgno_t)ft_page_hdr_get_pgno(hdrb) == head &&
                (ft_page_hdr_get_flags(hdrb) & FT_PAGE_FLAG_TOMBSTONE)) {
                uint8_t nextb[4];
                st = ft_blockdev_read(mp->bdev, tomb_off + (uint32_t)sizeof(ft_page_hdr_t), nextb,
                                      sizeof(nextb));
                if (st != FT_OK) {
                    mp->free_head = PGNO_INVALID;
                    goto done_pop;
                }
                pgno_t next = (pgno_t)ft_u32_le_read(nextb);
                mp->free_head = next;
                pgno = head;
            } else {
                /* Free-list head points to a bad record; drop the list. */
                mp->free_head = PGNO_INVALID;
            }
        } else {
            mp->free_head = PGNO_INVALID;
        }
    }
done_pop:

    /* Fall back to allocating a fresh pgno (first page is 0). */
    if (pgno == PGNO_INVALID) {
        pgno = mp->npages++; /* post-increment: first call returns 0 */
        if (ft_offsets_reserve(mp, (size_t)pgno + 1) != 0)
            return NULL;
    }
#else
    /* Simple O(1)-RAM policy: do not reuse freed pgno numbers. */
    mp->free_head = PGNO_INVALID;
    pgno = mp->npages++; /* post-increment: first call returns 0 */
#endif

    /* Find cache slot */
    ft_cache_slot_t* slot = ft_cache_find_free(mp);
    if (!slot) {
        /* Need to flush a dirty page first */
        for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
            if (mp->cache[i].dirty && !mp->cache[i].pinned) {
                ft_cache_flush_slot(mp, &mp->cache[i]);
                mp->cache[i].pgno = PGNO_INVALID;
                slot = &mp->cache[i];
                break;
            }
        }
    }
    if (!slot)
        return NULL;

    /* Initialize new page */
    slot->pgno = pgno;
    slot->log_offset = 0xFFFFFFFF; /* Not yet in log */
    slot->dirty = 1;               /* Will need to be written */
    slot->pinned = 1;
    memset(slot->data, 0, mp->pagesize);

    *pgnoaddr = pgno;

    if (mp->owner_kv) {
        mp->owner_kv->gc_dirty = 1;
        mp->owner_kv->free_head = (uint32_t)mp->free_head;
        mp->owner_kv->alloc_next = (uint32_t)mp->npages;
    }
    return slot->data;
}

#if FT_MPOOL_O1_RAM
static uint32_t ft_scan_back_in_range(MPOOL* mp, uint32_t start_off_exclusive, uint32_t stop_off_inclusive,
                                      pgno_t want_pgno, int* out_is_tombstone) {
    if (out_is_tombstone)
        *out_is_tombstone = 0;
    if (!mp)
        return FT_OFF_INVALID;

    const uint32_t total = ft_rec_size(mp);
    if (start_off_exclusive < total)
        return FT_OFF_INVALID;
    if (start_off_exclusive <= stop_off_inclusive)
        return FT_OFF_INVALID;

    uint32_t off = start_off_exclusive;
    while (off >= stop_off_inclusive + total) {
        off -= total;

        uint8_t hdrb[sizeof(ft_page_hdr_t)];
        if (ft_blockdev_read(mp->bdev, off, hdrb, sizeof(hdrb)) != FT_OK)
            goto next;
        if (ft_page_hdr_get_magic(hdrb) != FT_PAGE_MAGIC)
            goto next;
        if ((pgno_t)ft_page_hdr_get_pgno(hdrb) != want_pgno)
            goto next;

        /* Candidate: validate full record CRC. */
        uint8_t* rec = mp->scratch_rec;
        if (ft_blockdev_read(mp->bdev, off, rec, total) != FT_OK)
            continue;
        const uint8_t* full_hdr = rec;
        const uint8_t* payload = rec + sizeof(ft_page_hdr_t);
        const uint32_t saved_crc = ft_page_hdr_get_crc(full_hdr);
        const uint32_t crc = ft_page_hdr_crc_calc(full_hdr, payload, mp->pagesize);
        if (crc != saved_crc)
            goto next;

        if (ft_page_hdr_get_flags(full_hdr) & FT_PAGE_FLAG_TOMBSTONE) {
            if (out_is_tombstone)
                *out_is_tombstone = 1;
        }
        return off;

    next:
        if (off < stop_off_inclusive + total)
            break;
    }
    return FT_OFF_INVALID;
}

static uint32_t ft_scan_back_find_offset(MPOOL* mp, pgno_t want_pgno, int* out_is_tombstone) {
    if (out_is_tombstone)
        *out_is_tombstone = 0;
    if (!mp)
        return FT_OFF_INVALID;

    /* Prefer active half, then fall back to the other half if GC is in progress. */
    uint32_t active_start = gc_half_start(mp, mp->gc_active_half);
    uint32_t active_end = mp->write_off;
    uint32_t off = ft_scan_back_in_range(mp, active_end, active_start, want_pgno, out_is_tombstone);
    if (off != FT_OFF_INVALID || !mp->gc_in_progress)
        return off;

    int other_half = 1 - mp->gc_active_half;
    uint32_t other_start = gc_half_start(mp, other_half);
    uint32_t other_end = other_start + mp->gc_half_size;
    return ft_scan_back_in_range(mp, other_end, other_start, want_pgno, out_is_tombstone);
}
#endif

void* mpool_get(MPOOL* mp, pgno_t pgno, unsigned int flags) {
    (void)flags;
    if (!mp)
        return NULL;

    /* Check cache first */
    ft_cache_slot_t* slot = ft_cache_find(mp, pgno);
    if (slot) {
        slot->pinned = 1;
        return slot->data;
    }

    uint32_t log_off = FT_OFF_INVALID;
#if FT_MPOOL_O1_RAM
    if (!ft_pg_cache_lookup(mp, pgno, &log_off) || log_off == FT_OFF_INVALID) {
        int is_tomb = 0;
        log_off = ft_scan_back_find_offset(mp, pgno, &is_tomb);
        if (log_off == FT_OFF_INVALID || is_tomb) {
            errno = EINVAL;
            if (log_off != FT_OFF_INVALID)
                ft_pg_cache_insert(mp, pgno, log_off);
            return NULL;
        }
        ft_pg_cache_insert(mp, pgno, log_off);
    }
#else
    /* Find in page-map */
    log_off = ft_offsets_get(mp, pgno);
    if (log_off == FT_OFF_INVALID) {
        errno = EINVAL; /* Page doesn't exist - B-Tree checks this! */
        return NULL;
    }
#endif

    /* Allocate cache slot */
    slot = ft_cache_find_free(mp);
    if (!slot) {
        /* Flush a dirty page to make room */
        for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
            if (mp->cache[i].dirty && !mp->cache[i].pinned) {
                ft_cache_flush_slot(mp, &mp->cache[i]);
                mp->cache[i].pgno = PGNO_INVALID;
                slot = &mp->cache[i];
                break;
            }
        }
    }
    if (!slot)
        return NULL;

    /* Read full record (header+payload) in a single read(). */
    const uint32_t total = ft_rec_size(mp);
    uint8_t* rec = mp->scratch_rec;
    uint8_t* hdrb = rec;
    uint8_t* payload = rec + sizeof(ft_page_hdr_t);

    ft_status_t st = ft_blockdev_read(mp->bdev, log_off, rec, total);
    if (st != FT_OK)
        return NULL;

    /* Validate header quickly. */
    if (ft_page_hdr_get_magic(hdrb) != FT_PAGE_MAGIC)
        return NULL;
    if ((pgno_t)ft_page_hdr_get_pgno(hdrb) != pgno)
        return NULL;

    /* Verify CRC */
    const uint32_t saved_crc = ft_page_hdr_get_crc(hdrb);
    const uint32_t crc = ft_page_hdr_crc_calc(hdrb, payload, mp->pagesize);
    if (crc != saved_crc)
        return NULL; /* Corrupt */

    /* Tombstones are not readable as pages (treat as non-existent). */
    if (ft_page_hdr_get_flags(hdrb) & FT_PAGE_FLAG_TOMBSTONE) {
        errno = EINVAL;
        return NULL;
    }

    memcpy(slot->data, payload, mp->pagesize);

    /* Call pgin filter if set */
    if (mp->pgin) {
        mp->pgin(mp->pgcookie, pgno, slot->data);
    }

    slot->pgno = pgno;
    slot->log_offset = log_off;
    slot->dirty = 0;
    slot->pinned = 1;

    return slot->data;
}

int mpool_put(MPOOL* mp, void* page, unsigned int flags) {
    if (!mp || !page)
        return -1;

    /* Find cache slot containing this page */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
        if (mp->cache[i].data == page || (page >= (void*)mp->cache[i].data &&
                                          page < (void*)(mp->cache[i].data + mp->pagesize))) {

            if (flags & MPOOL_DIRTY) {
                mp->cache[i].dirty = 1;
            }
            mp->cache[i].pinned = 0;
            return 0;
        }
    }

    return -1; /* Page not found in cache */
}

int mpool_sync(MPOOL* mp) {
    if (!mp)
        return -1;

    /* Flush all dirty pages */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
        if (mp->cache[i].dirty) {
            if (ft_cache_flush_slot(mp, &mp->cache[i]) != 0) {
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

    free(mp->scratch_rec);
    free(mp->cache_mem);
#if !FT_MPOOL_O1_RAM
    free(mp->page_offsets);
#endif
    free(mp);
    return 0;
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

#if !FT_MPOOL_O1_RAM

/*
 * Copy a page from old location to new location in the other half.
 */
static int gc_copy_page(MPOOL* mp, pgno_t pgno) {
    uint32_t old_off = ft_offsets_get(mp, pgno);
    if (old_off == FT_OFF_INVALID)
        return 0;

    /* Read full record (header+payload) in a single read(). */
    const uint32_t total = ft_rec_size(mp);
    uint8_t* rec = mp->scratch_rec;
    uint8_t* hdrb = rec;
    uint8_t* payload = rec + sizeof(ft_page_hdr_t);
    ft_status_t st = ft_blockdev_read(mp->bdev, old_off, rec, total);
    if (st != FT_OK)
        return -1;

    /* Verify CRC */
    const uint32_t saved_crc = ft_page_hdr_get_crc(hdrb);
    const uint32_t crc = ft_page_hdr_crc_calc(hdrb, payload, mp->pagesize);
    if (crc != saved_crc)
        return -1; /* Corrupt, skip */

    if (ft_page_hdr_get_flags(hdrb) & FT_PAGE_FLAG_TOMBSTONE)
        return 1; /* Freed page - skip copy. */

    /* Write to new location */
    uint32_t new_off = mp->write_off;
    st = ft_blockdev_prog(mp->bdev, new_off, rec, total);
    if (st != FT_OK)
        return -1;

    /* Update page-map entry */
    if (ft_offsets_set(mp, pgno, new_off) != 0)
        return -1;
    mp->write_off += total;

    return 0;
}

int mpool_gc_step(MPOOL* mp, size_t budget_bytes) {
    if (!mp)
        return -1;

    /* Start GC if not in progress and needed */
    if (!mp->gc_in_progress) {
        if (!gc_needed(mp)) {
            return 0; /* GC not needed */
        }

        /* Sync all dirty pages first */
        if (mpool_sync(mp) != 0)
            return -1;

        /* Switch to other half */
        int new_half = 1 - mp->gc_active_half;
        uint32_t new_start = gc_half_start(mp, new_half);

        /* Erase new half (align to erase granularity) */
        uint32_t eg = mp->bdev->geom.erase_granularity;
        uint32_t erase_start = (new_start / eg) * eg;
        uint32_t erase_len = ((mp->gc_half_size + eg - 1) / eg) * eg;

        ft_status_t st = ft_blockdev_erase(mp->bdev, erase_start, erase_len);
        if (st != FT_OK)
            return -1;

        /* Start writing to new half */
        mp->write_off = new_start;
        mp->gc_active_half = new_half;
        mp->gc_in_progress = 1;
        mp->gc_next_pgno = 0;
    }

    /* Copy pages incrementally based on budget */
    size_t bytes_copied = 0;
    size_t page_total = sizeof(ft_page_hdr_t) + mp->pagesize;

    while (mp->gc_next_pgno < mp->npages) {
        if (budget_bytes > 0 && bytes_copied >= budget_bytes) {
            return 1; /* More work to do */
        }

        uint32_t off = ft_offsets_get(mp, mp->gc_next_pgno);
        if (off != FT_OFF_INVALID) {
            if (gc_copy_page(mp, mp->gc_next_pgno) == 0) {
                bytes_copied += page_total;
            }
        }
        mp->gc_next_pgno++;
    }

    /* GC complete */
    mp->gc_in_progress = 0;

    /* Erase old half */
    int old_half = 1 - mp->gc_active_half;
    uint32_t old_start = gc_half_start(mp, old_half);
    uint32_t eg = mp->bdev->geom.erase_granularity;
    uint32_t erase_start = (old_start / eg) * eg;
    uint32_t erase_len = ((mp->gc_half_size + eg - 1) / eg) * eg;

    ft_blockdev_erase(mp->bdev, erase_start, erase_len);

    return 0; /* GC complete */
}

/* ============== flash-tree extension: freeing a pgno ============== */

int mpool_free_pgno(MPOOL* mp, pgno_t pgno) {
    if (!mp || pgno == PGNO_INVALID)
        return -1;

    /* If present in cache, invalidate (must not be pinned). */
    ft_cache_slot_t* slot = ft_cache_find(mp, pgno);
    if (slot) {
        if (slot->pinned)
            return -1;
        slot->pgno = PGNO_INVALID;
        slot->dirty = 0;
        slot->log_offset = 0;
    }

    /* Append a tombstone record so recovery can restore the freed state. */
    const uint32_t total = ft_rec_size(mp);
    uint8_t* rec = mp->scratch_rec;
    uint8_t* hdrb = rec;
    uint8_t* payload = rec + sizeof(ft_page_hdr_t);
    ft_page_hdr_encode(hdrb, FT_PAGE_MAGIC, pgno, 0u, FT_PAGE_FLAG_TOMBSTONE);

    /* CRC covers header bytes (crc32=0) + a full pagesize data region. */
    memset(payload, 0, mp->pagesize);
    /* Store the current free-list head as next pointer (LE u32). */
    ft_u32_le_write(payload, (uint32_t)mp->free_head);
    uint32_t crc = ft_page_hdr_crc_calc(hdrb, payload, mp->pagesize);
    ft_u32_le_write(&hdrb[8], crc);
    if ((uint64_t)mp->write_off + (uint64_t)total > mp->bdev->geom.total_size_bytes) {
        return -1;
    }

    const uint32_t tomb_off = mp->write_off;
    ft_status_t st = ft_blockdev_prog(mp->bdev, tomb_off, rec, total);
    if (st != FT_OK)
        return -1;

    mp->write_off += total;

    /* Point map entry at tombstone record (so we can read next pointers). */
    if (ft_offsets_set(mp, pgno, tomb_off) != 0)
        return -1;

    /* Push on free-list. */
    mp->free_head = pgno;
    if (mp->owner_kv) {
        mp->owner_kv->gc_dirty = 1;
        mp->owner_kv->free_head = (uint32_t)mp->free_head;
        mp->owner_kv->alloc_next = (uint32_t)mp->npages;
    }
    return 0;
}

#else /* FT_MPOOL_O1_RAM */

static uint32_t gc_half_end(MPOOL* mp, int half) {
    return gc_half_start(mp, half) + mp->gc_half_size;
}

static int gc_copy_page_o1ram(MPOOL* mp, int old_half, pgno_t pgno) {
    const uint32_t total = ft_rec_size(mp);
    const uint32_t new_start = gc_half_start(mp, mp->gc_active_half);
    const uint32_t new_end = mp->write_off;
    const uint32_t old_start = gc_half_start(mp, old_half);
    const uint32_t old_end = gc_half_end(mp, old_half);

    /* If pgno already exists in the new half, keep it (do not overwrite). */
    {
        int is_tomb = 0;
        uint32_t new_off = ft_scan_back_in_range(mp, new_end, new_start, pgno, &is_tomb);
        if (new_off != FT_OFF_INVALID) {
            return 0;
        }
    }

    int is_tomb = 0;
    uint32_t old_off = ft_scan_back_in_range(mp, old_end, old_start, pgno, &is_tomb);
    if (old_off == FT_OFF_INVALID || is_tomb)
        return 0;

    /* Read + verify old record. */
    uint8_t* rec = mp->scratch_rec;
    ft_status_t st = ft_blockdev_read(mp->bdev, old_off, rec, total);
    if (st != FT_OK)
        return -1;
    const uint8_t* hdrb = rec;
    const uint8_t* payload = rec + sizeof(ft_page_hdr_t);
    if (ft_page_hdr_get_magic(hdrb) != FT_PAGE_MAGIC)
        return -1;
    if ((pgno_t)ft_page_hdr_get_pgno(hdrb) != pgno)
        return -1;
    const uint32_t saved_crc = ft_page_hdr_get_crc(hdrb);
    const uint32_t crc = ft_page_hdr_crc_calc(hdrb, payload, mp->pagesize);
    if (crc != saved_crc)
        return -1;
    if (ft_page_hdr_get_flags(hdrb) & FT_PAGE_FLAG_TOMBSTONE)
        return 0;

    /* Write to new location. */
    if ((uint64_t)mp->write_off + (uint64_t)total > mp->bdev->geom.total_size_bytes)
        return -1;
    st = ft_blockdev_prog(mp->bdev, mp->write_off, rec, total);
    if (st != FT_OK)
        return -1;
    uint32_t new_off = mp->write_off;
    mp->write_off += total;
    ft_pg_cache_insert(mp, pgno, new_off);
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
        if (ft_blockdev_erase(mp->bdev, erase_start, erase_len) != FT_OK)
            return -1;

        /* Switch active writes to the new half. */
        mp->write_off = new_start;
        mp->gc_active_half = new_half;
        mp->gc_in_progress = 1;
        mp->gc_next_pgno = 0;
        mp->free_head = PGNO_INVALID; /* no reuse in O(1)-RAM mode */
        ft_pg_cache_init(mp);
    }

    int old_half = 1 - mp->gc_active_half;
    const uint32_t total = ft_rec_size(mp);
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
        (void)ft_blockdev_erase(mp->bdev, erase_start, erase_len);
    }

    return 0;
}

int mpool_free_pgno(MPOOL* mp, pgno_t pgno) {
    if (!mp || pgno == PGNO_INVALID)
        return -1;

    ft_cache_slot_t* slot = ft_cache_find(mp, pgno);
    if (slot) {
        if (slot->pinned)
            return -1;
        slot->pgno = PGNO_INVALID;
        slot->dirty = 0;
        slot->log_offset = 0;
    }

    const uint32_t total = ft_rec_size(mp);
    uint8_t* rec = mp->scratch_rec;
    uint8_t* hdrb = rec;
    uint8_t* payload = rec + sizeof(ft_page_hdr_t);
    ft_page_hdr_encode(hdrb, FT_PAGE_MAGIC, pgno, 0u, FT_PAGE_FLAG_TOMBSTONE);

    memset(payload, 0, mp->pagesize);
    uint32_t crc = ft_page_hdr_crc_calc(hdrb, payload, mp->pagesize);
    ft_u32_le_write(&hdrb[8], crc);
    if ((uint64_t)mp->write_off + (uint64_t)total > mp->bdev->geom.total_size_bytes)
        return -1;

    const uint32_t tomb_off = mp->write_off;
    if (ft_blockdev_prog(mp->bdev, tomb_off, rec, total) != FT_OK)
        return -1;
    mp->write_off += total;

    /* Cache tombstone so lookups can fail fast. */
    ft_pg_cache_insert(mp, pgno, tomb_off);

    if (mp->owner_kv) {
        mp->owner_kv->gc_dirty = 1;
        mp->owner_kv->free_head = (uint32_t)PGNO_INVALID;
        mp->owner_kv->alloc_next = (uint32_t)mp->npages;
    }
    return 0;
}

#endif /* FT_MPOOL_O1_RAM */
