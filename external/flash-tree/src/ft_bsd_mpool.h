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
#include <stdint.h>

/* Configuration */
#define FT_MPOOL_MAX_PAGES    64    /* Max pages in page-map */
#define FT_MPOOL_PAGE_SIZE   512    /* Page size (matches flash sector) */
#define FT_MPOOL_CACHE_PAGES   2    /* Pages in RAM cache */

/* Page number type (compatible with BSD btree) */
typedef uint32_t pgno_t;

/* On-flash page header (prepended to each page in log) */
typedef struct __attribute__((packed)) ft_page_hdr {
    uint32_t magic;       /* FT_PAGE_MAGIC */
    uint32_t pgno;        /* Logical page number */
    uint32_t crc32;       /* CRC of header + page data */
    uint32_t flags;       /* Reserved */
} ft_page_hdr_t;

#define FT_PAGE_MAGIC 0x50475446u  /* 'FTGP' */

/* Invalid page number sentinel (page 0 is the valid B-Tree meta page!) */
#define PGNO_INVALID  0xFFFFFFFF

/* Page-map entry: tracks where each logical page lives in log */
typedef struct ft_page_map_entry {
    uint32_t pgno;        /* Logical page number (PGNO_INVALID = unused) */
    uint32_t log_offset;  /* Offset in log where page data starts */
} ft_page_map_entry_t;

/* Cached page buffer */
typedef struct ft_cache_slot {
    pgno_t pgno;          /* Page number (PGNO_INVALID = empty) */
    uint32_t log_offset;  /* Where this version came from */
    int dirty;            /* Needs to be written */
    int pinned;           /* Currently in use */
    uint8_t data[FT_MPOOL_PAGE_SIZE];
} ft_cache_slot_t;

/* Memory pool handle */
typedef struct MPOOL {
    ft_blockdev_t* bdev;
    uint32_t data_base;           /* Start offset for mpool data (after header region) */
    uint32_t write_off;           /* Next write offset in log (absolute) */
    uint32_t pagesize;            /* Page size (without header) */
    pgno_t npages;                /* Highest allocated page number */
    
    /* Page-map: logical pgno → log offset */
    ft_page_map_entry_t page_map[FT_MPOOL_MAX_PAGES];
    size_t page_map_count;
    
    /* RAM cache */
    ft_cache_slot_t cache[FT_MPOOL_CACHE_PAGES];
    
    /* Byte-swap callbacks (for B-tree) */
    void (*pgin)(void*, pgno_t, void*);
    void (*pgout)(void*, pgno_t, void*);
    void* pgcookie;
    
    /* GC state (ping-pong between two halves) */
    uint32_t gc_half_size;        /* Size of each half */
    int gc_active_half;           /* 0 or 1 - which half is active */
    int gc_in_progress;           /* GC is running */
    size_t gc_next_page_idx;      /* Next page to copy during incremental GC */
} MPOOL;

/*
 * Garbage collection - compact the log by copying live pages.
 * Call incrementally with budget_bytes to limit work per call.
 * Returns 0 when GC is complete, 1 when more work remains.
 */
int mpool_gc_step(MPOOL* mp, size_t budget_bytes);

/* Flags for mpool_put */
#define MPOOL_DIRTY   0x01

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
void mpool_filter(MPOOL* mp,
                  void (*pgin)(void*, pgno_t, void*),
                  void (*pgout)(void*, pgno_t, void*),
                  void* pgcookie);

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
