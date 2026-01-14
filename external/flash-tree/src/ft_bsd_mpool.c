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
#include "ft_utils.h"
#include "ft_kv_bind.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* CRC32 from ft_crc32.c */
extern uint32_t ft_crc32_ieee(const void* data, size_t len, uint32_t seed);

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

/* ============== Page-Map Operations ============== */

/**
 * Find page-map entry for a given logical page number.
 * @return Pointer to entry, or NULL if not found
 */
static ft_page_map_entry_t* ft_pagemap_find(MPOOL* mp, pgno_t pgno) {
    for (size_t i = 0; i < mp->page_map_count; i++) {
        if (mp->page_map[i].pgno == pgno) {
            return &mp->page_map[i];
        }
    }
    return NULL;
}

/**
 * Insert or update page-map entry.
 * @return 0 on success, -1 if page-map is full
 */
static int ft_pagemap_insert(MPOOL* mp, pgno_t pgno, uint32_t log_offset) {
    /* Update existing entry */
    ft_page_map_entry_t* e = ft_pagemap_find(mp, pgno);
    if (e) {
        e->log_offset = log_offset;
        return 0;
    }
    
    /* Insert new entry */
    if (mp->page_map_count >= FT_MPOOL_MAX_PAGES) {
        return -1;  /* Page-map full */
    }
    
    mp->page_map[mp->page_map_count].pgno = pgno;
    mp->page_map[mp->page_map_count].log_offset = log_offset;
    mp->page_map_count++;
    return 0;
}

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
    
    return NULL;  /* All slots busy */
}

/**
 * Flush a dirty cache slot to the log.
 * @return 0 on success, -1 on error
 */
static int ft_cache_flush_slot(MPOOL* mp, ft_cache_slot_t* slot) {
    if (!slot->dirty) return 0;
    
    /* Prepare page header */
    ft_page_hdr_t hdr = {0};
    hdr.magic = FT_PAGE_MAGIC;
    hdr.pgno = slot->pgno;
    hdr.flags = 0;
    
    /* Calculate CRC over header (with crc=0) + data */
    hdr.crc32 = 0;
    uint32_t crc = ft_crc32_ieee(&hdr, sizeof(hdr), 0);
    crc = ft_crc32_ieee(slot->data, mp->pagesize, crc);
    hdr.crc32 = crc;
    
    /* Call pgout filter if set */
    if (mp->pgout) {
        mp->pgout(mp->pgcookie, slot->pgno, slot->data);
    }
    
    /* Check space */
    uint32_t total = sizeof(hdr) + mp->pagesize;
    if ((uint64_t)mp->write_off + total > mp->bdev->geom.total_size_bytes) {
        return -1;  /* Log full */
    }
    
    /* Write header */
    ft_status_t st = ft_blockdev_prog(mp->bdev, mp->write_off, &hdr, sizeof(hdr));
    if (st != FT_OK) return -1;
    
    /* Write page data */
    st = ft_blockdev_prog(mp->bdev, mp->write_off + sizeof(hdr), slot->data, mp->pagesize);
    if (st != FT_OK) return -1;
    
    /* Update page-map with new location */
    uint32_t new_offset = mp->write_off;
    mp->write_off += total;
    
    ft_pagemap_insert(mp, slot->pgno, new_offset);
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
    mp->page_map_count = 0;
    mp->npages = 0;
    
    /* Start scanning from data_base */
    uint32_t offset = mp->data_base;
    ft_page_hdr_t hdr;
    
    while (offset + sizeof(hdr) + mp->pagesize <= mp->bdev->geom.total_size_bytes) {
        ft_status_t st = ft_blockdev_read(mp->bdev, offset, &hdr, sizeof(hdr));
        if (st != FT_OK) break;
        
        /* Check for end of log (erased = 0xFF) */
        if (ft_is_all_erased(&hdr, sizeof(hdr))) break;
        
        if (hdr.magic != FT_PAGE_MAGIC) break;
        
        /* Valid page header - update page-map (later entries override earlier) */
        ft_pagemap_insert(mp, hdr.pgno, offset);
        
        /* Track highest page number seen (+1 for next allocation) */
        if (hdr.pgno >= mp->npages) {
            mp->npages = hdr.pgno + 1;
        }
        
        offset += sizeof(hdr) + mp->pagesize;
    }
    
    mp->write_off = offset;
    return 0;
}

/* ============== Public API ============== */

MPOOL* mpool_open(void* key, int fd, pgno_t pagesize, pgno_t maxcache) {
    (void)key;
    (void)fd;
    (void)maxcache;
    
    ft_blockdev_t* bdev = ft_kv_bound_bdev();
    if (!bdev) return NULL;
    if (pagesize == 0) pagesize = FT_MPOOL_PAGE_SIZE;
    if (pagesize > FT_MPOOL_PAGE_SIZE) return NULL;  /* Page too large for cache */
    
    MPOOL* mp = (MPOOL*)calloc(1, sizeof(MPOOL));
    if (!mp) return NULL;
    
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
    mp->gc_next_page_idx = 0;
    
    mp->npages = 0;
    mp->page_map_count = 0;
    
    /* Clear cache - mark all slots as empty */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
        mp->cache[i].pgno = PGNO_INVALID;
        mp->cache[i].log_offset = 0;
        mp->cache[i].dirty = 0;
        mp->cache[i].pinned = 0;
    }
    
    /* Recover page-map from existing log */
    if (ft_mpool_recover(mp) != 0) {
        free(mp);
        return NULL;
    }
    
    return mp;
}

void mpool_filter(MPOOL* mp,
                  void (*pgin)(void*, pgno_t, void*),
                  void (*pgout)(void*, pgno_t, void*),
                  void* pgcookie) {
    if (!mp) return;
    mp->pgin = pgin;
    mp->pgout = pgout;
    mp->pgcookie = pgcookie;
}

void* mpool_new(MPOOL* mp, pgno_t* pgnoaddr) {
    if (!mp || !pgnoaddr) return NULL;
    
    /* Allocate new page number (first page is 0) */
    pgno_t pgno = mp->npages++;  /* post-increment: first call returns 0 */
    
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
    if (!slot) return NULL;
    
    /* Initialize new page */
    slot->pgno = pgno;
    slot->log_offset = 0xFFFFFFFF;  /* Not yet in log */
    slot->dirty = 1;                 /* Will need to be written */
    slot->pinned = 1;
    memset(slot->data, 0, mp->pagesize);
    
    *pgnoaddr = pgno;
    return slot->data;
}

void* mpool_get(MPOOL* mp, pgno_t pgno, unsigned int flags) {
    (void)flags;
    if (!mp) return NULL;
    
    /* Check cache first */
    ft_cache_slot_t* slot = ft_cache_find(mp, pgno);
    if (slot) {
        slot->pinned = 1;
        return slot->data;
    }
    
    /* Find in page-map */
    ft_page_map_entry_t* e = ft_pagemap_find(mp, pgno);
    if (!e) {
        errno = EINVAL;  /* Page doesn't exist - B-Tree checks this! */
        return NULL;
    }
    
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
    if (!slot) return NULL;
    
    /* Read page from log */
    ft_page_hdr_t hdr;
    ft_status_t st = ft_blockdev_read(mp->bdev, e->log_offset, &hdr, sizeof(hdr));
    if (st != FT_OK) return NULL;
    
    st = ft_blockdev_read(mp->bdev, e->log_offset + sizeof(hdr), slot->data, mp->pagesize);
    if (st != FT_OK) return NULL;
    
    /* Verify CRC */
    uint32_t saved_crc = hdr.crc32;
    hdr.crc32 = 0;
    uint32_t crc = ft_crc32_ieee(&hdr, sizeof(hdr), 0);
    crc = ft_crc32_ieee(slot->data, mp->pagesize, crc);
    if (crc != saved_crc) return NULL;  /* Corrupt */
    
    /* Call pgin filter if set */
    if (mp->pgin) {
        mp->pgin(mp->pgcookie, pgno, slot->data);
    }
    
    slot->pgno = pgno;
    slot->log_offset = e->log_offset;
    slot->dirty = 0;
    slot->pinned = 1;
    
    return slot->data;
}

int mpool_put(MPOOL* mp, void* page, unsigned int flags) {
    if (!mp || !page) return -1;
    
    /* Find cache slot containing this page */
    for (int i = 0; i < FT_MPOOL_CACHE_PAGES; i++) {
        if (mp->cache[i].data == page || 
            (page >= (void*)mp->cache[i].data && 
             page < (void*)(mp->cache[i].data + mp->pagesize))) {
            
            if (flags & MPOOL_DIRTY) {
                mp->cache[i].dirty = 1;
            }
            mp->cache[i].pinned = 0;
            return 0;
        }
    }
    
    return -1;  /* Page not found in cache */
}

int mpool_sync(MPOOL* mp) {
    if (!mp) return -1;
    
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
    if (!mp) return -1;
    
    /* Sync before close */
    mpool_sync(mp);
    
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

/*
 * Copy a page from old location to new location in the other half.
 */
static int gc_copy_page(MPOOL* mp, size_t page_idx) {
    if (page_idx >= mp->page_map_count) return 0;
    
    ft_page_map_entry_t* e = &mp->page_map[page_idx];
    if (e->pgno == PGNO_INVALID) return 0;  /* Empty slot */
    
    /* Read page from old location */
    ft_page_hdr_t hdr;
    ft_status_t st = ft_blockdev_read(mp->bdev, e->log_offset, &hdr, sizeof(hdr));
    if (st != FT_OK) return -1;
    
    uint8_t page_data[FT_MPOOL_PAGE_SIZE];
    st = ft_blockdev_read(mp->bdev, e->log_offset + sizeof(hdr), page_data, mp->pagesize);
    if (st != FT_OK) return -1;
    
    /* Verify CRC */
    uint32_t saved_crc = hdr.crc32;
    hdr.crc32 = 0;
    uint32_t crc = ft_crc32_ieee(&hdr, sizeof(hdr), 0);
    crc = ft_crc32_ieee(page_data, mp->pagesize, crc);
    if (crc != saved_crc) return -1;  /* Corrupt, skip */
    
    /* Write to new location */
    hdr.crc32 = saved_crc;
    uint32_t new_off = mp->write_off;
    uint32_t total = sizeof(hdr) + mp->pagesize;
    
    st = ft_blockdev_prog(mp->bdev, new_off, &hdr, sizeof(hdr));
    if (st != FT_OK) return -1;
    
    st = ft_blockdev_prog(mp->bdev, new_off + sizeof(hdr), page_data, mp->pagesize);
    if (st != FT_OK) return -1;
    
    /* Update page-map entry */
    e->log_offset = new_off;
    mp->write_off += total;
    
    return 0;
}

int mpool_gc_step(MPOOL* mp, size_t budget_bytes) {
    if (!mp) return -1;
    
    /* Start GC if not in progress and needed */
    if (!mp->gc_in_progress) {
        if (!gc_needed(mp)) {
            return 0;  /* GC not needed */
        }
        
        /* Sync all dirty pages first */
        if (mpool_sync(mp) != 0) return -1;
        
        /* Switch to other half */
        int new_half = 1 - mp->gc_active_half;
        uint32_t new_start = gc_half_start(mp, new_half);
        
        /* Erase new half (align to erase granularity) */
        uint32_t eg = mp->bdev->geom.erase_granularity;
        uint32_t erase_start = (new_start / eg) * eg;
        uint32_t erase_len = ((mp->gc_half_size + eg - 1) / eg) * eg;
        
        ft_status_t st = ft_blockdev_erase(mp->bdev, erase_start, erase_len);
        if (st != FT_OK) return -1;
        
        /* Start writing to new half */
        mp->write_off = new_start;
        mp->gc_active_half = new_half;
        mp->gc_in_progress = 1;
        mp->gc_next_page_idx = 0;
    }
    
    /* Copy pages incrementally based on budget */
    size_t bytes_copied = 0;
    size_t page_total = sizeof(ft_page_hdr_t) + mp->pagesize;
    
    while (mp->gc_next_page_idx < mp->page_map_count) {
        if (budget_bytes > 0 && bytes_copied >= budget_bytes) {
            return 1;  /* More work to do */
        }
        
        if (gc_copy_page(mp, mp->gc_next_page_idx) == 0) {
            bytes_copied += page_total;
        }
        mp->gc_next_page_idx++;
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
    
    return 0;  /* GC complete */
}
