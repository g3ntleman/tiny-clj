/*
 * mpool.h - Copy-on-Write Memory Pool for Log-Structured B-Tree
 *
 * Replaces BSD mpool with flash-friendly append-only writes.
 * API-compatible with original mpool for bt_*.c integration.
 *
 * Design:
 * - Pages written append-only to log (no Read-Modify-Write)
 * - In-memory page-map tracks pgno → log_offset
 * - Single-page cache for current working page
 * - Recovery scans log to rebuild page-map
 */

#pragma once

#include "flash_tree.h"
#include "ft_blockdev.h"
#include "ft_bsd_db.h"
#include <stdint.h>

/* Configuration */
#ifndef FT_MPOOL_DEFAULT_PAGE_SIZE
#define FT_MPOOL_DEFAULT_PAGE_SIZE 512 /* Used if caller passes pagesize==0 */
#endif

/*
 * Pages in RAM cache.
 * Note: top-down split aims for a peak of 3 pinned pages.
 */
#ifndef FT_MPOOL_CACHE_PAGES_DEFAULT
#define FT_MPOOL_CACHE_PAGES_DEFAULT 3
#endif

#ifndef FT_MPOOL_CACHE_PAGES_MAX
#define FT_MPOOL_CACHE_PAGES_MAX 32
#endif

/*
 * O(1)-RAM mode for pgno->offset mapping:
 * - Use a small fixed cache + scan-on-demand in the data log.
 * - Avoids a pgno-sized array and therefore avoids OOM on large partitions.
 */
#define FT_MPOOL_O1_RAM 1

/* Fixed cache size (only used when FT_MPOOL_O1_RAM=1). */
#ifndef FT_MPOOL_PG_CACHE_ENTRIES
#define FT_MPOOL_PG_CACHE_ENTRIES 32
#endif

struct ft_kv;

/* On-flash page header (prepended to each page in log) */
typedef struct __attribute__((packed)) ft_page_hdr {
    uint32_t magic; /* FT_PAGE_MAGIC */
    uint32_t pgno;  /* Logical page number */
    uint32_t crc32; /* CRC of header + page data */
    uint32_t flags; /* FT_PAGE_FLAG_* */
} ft_page_hdr_t;

#define FT_PAGE_MAGIC 0x50475446u /* 'FTGP' */
/* Tombstone record: pgno is considered freed (page_offsets entry invalid). */
#define FT_PAGE_FLAG_TOMBSTONE 0x00000001u

/* Invalid page number sentinel (page 0 is the valid B-Tree meta page!) */
#define PGNO_INVALID 0xFFFFFFFF

/* Cached page buffer */
typedef struct ft_cache_slot {
    pgno_t pgno;         /* Page number (PGNO_INVALID = empty) */
    uint32_t log_offset; /* Where this version came from */
    int dirty;           /* Needs to be written */
    int pinned;          /* Currently in use */
    uint8_t* data;       /* Owned by MPOOL (cache_mem), size = mp->pagesize */
} ft_cache_slot_t;

#if FT_MPOOL_O1_RAM
typedef struct ft_pg_cache_entry {
    pgno_t pgno;
    uint32_t log_offset; /* FT_OFF_INVALID means empty */
} ft_pg_cache_entry_t;
#endif

/* Memory pool handle */
typedef struct MPOOL {
    ft_blockdev_t* bdev;
    uint32_t data_base; /* Start offset for mpool data (after header region) */
    uint32_t write_off; /* Next write offset in log (absolute) */
    uint32_t pagesize;  /* Page size (without header) */
    pgno_t npages;      /* Highest allocated page number */

    /* Pin accounting (debug/testing): number of pinned cache slots now/peak. */
    uint32_t pin_count;
    uint32_t pin_peak;

#if FT_MPOOL_O1_RAM
    /* Small fixed cache for hot pages. */
    ft_pg_cache_entry_t pg_cache[FT_MPOOL_PG_CACHE_ENTRIES];
    uint32_t pg_cache_rr; /* round-robin eviction index */
#endif

    /* RAM cache */
    uint32_t cache_pagecount;
    ft_cache_slot_t cache[FT_MPOOL_CACHE_PAGES_MAX];
    uint8_t* cache_mem; /* cache_pagecount * pagesize bytes */

    /* Byte-swap callbacks (for B-tree) */
    void (*pgin)(void*, pgno_t, void*);
    void (*pgout)(void*, pgno_t, void*);
    void* pgcookie;

    /* GC state (ping-pong between two halves) */
    uint32_t gc_half_size; /* Size of each half */
    int gc_active_half;    /* 0 or 1 - which half is active */
    int gc_in_progress;    /* GC is running */
    pgno_t gc_next_pgno;   /* Next page to copy during incremental GC */

    /* Tombstone free-list (for pages freed via mpool_free_pgno) */
    pgno_t free_head; /* PGNO_INVALID when empty */

    /* Back-pointer for marking KV GC state dirty (optional, may be NULL). */
    struct ft_kv* owner_kv;
} MPOOL;

/*
 * Garbage collection - compact the log by copying live pages.
 * Call incrementally with budget_bytes to limit work per call.
 * Returns 0 when GC is complete, 1 when more work remains.
 */
int mpool_gc_step(MPOOL* mp, size_t budget_bytes);

/* Flags for mpool_put */
#define MPOOL_DIRTY 0x01

/* BSD-compatible API */

/*
 * Open a memory pool.
 * @param key       Unused (compatibility)
 * @param fd        Unused (we use bdev directly)
 * @param pagesize  Page size
 * @param maxcache  Unused (fixed cache size)
 * @return          Pool handle or NULL
 */
MPOOL* mpool_open(void* key, int fd, pgno_t pagesize, pgno_t maxcache);

/*
 * Set byte-swap filter callbacks.
 */
void mpool_filter(MPOOL* mp, void (*pgin)(void*, pgno_t, void*),
                  void (*pgout)(void*, pgno_t, void*), void* pgcookie);

/*
 * Allocate a new page.
 * @param mp        Pool handle
 * @param pgnoaddr  Output: new page number
 * @return          Pointer to page buffer (in cache)
 */
void* mpool_new(MPOOL* mp, pgno_t* pgnoaddr);

/*
 * Get an existing page.
 * @param mp    Pool handle
 * @param pgno  Page number
 * @param flags Unused
 * @return      Pointer to page buffer (in cache)
 */
void* mpool_get(MPOOL* mp, pgno_t pgno, unsigned int flags);

/*
 * Release a page (and optionally mark dirty).
 * @param mp    Pool handle
 * @param page  Page buffer pointer
 * @param flags MPOOL_DIRTY to mark for write
 * @return      0 on success
 */
int mpool_put(MPOOL* mp, void* page, unsigned int flags);

/*
 * Flush all dirty pages to log.
 * @return 0 on success
 */
int mpool_sync(MPOOL* mp);

/*
 * Close the pool.
 * @return 0 on success
 */
int mpool_close(MPOOL* mp);

/*
 * Free a page number (best-effort).
 *
 * Writes a tombstone record so recovery can rebuild freed state,
 * and updates the in-memory page map to invalidate this pgno.
 *
 * Note: This is flash-tree specific and not part of the BSD mpool API.
 */
int mpool_free_pgno(MPOOL* mp, pgno_t pgno);

/* Debug/testing helper: max simultaneously pinned pages observed. */
uint32_t mpool_peak_pinned(const MPOOL* mp);
