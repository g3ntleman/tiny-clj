// ft_log.h - Append-only record log + checkpoints (test-first).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "flash_tree.h"
#include "ft_blockdev.h"

typedef enum ft_log_rec_type {
    FT_LOG_REC_PUT = 1,
    FT_LOG_REC_DEL = 2,
    FT_LOG_REC_CHECKPOINT = 3,
    FT_LOG_REC_TS_APPEND = 10,
} ft_log_rec_type_t;

typedef struct ft_log {
    const ft_blockdev_t* bdev;
    uint32_t write_off; // next write offset (bytes)
    uint64_t next_seqno;
} ft_log_t;

typedef struct ft_log_checkpoint {
    uint64_t seqno;
    uint32_t root_off; // reserved for later (e.g. B-tree root page addr)
} ft_log_checkpoint_t;

ft_status_t ft_log_init(ft_log_t* log, const ft_blockdev_t* bdev);

// Append arbitrary payload record.
ft_status_t ft_log_append(ft_log_t* log, ft_log_rec_type_t type,
                          const void* payload, uint32_t payload_len,
                          uint32_t* out_rec_off, uint64_t* out_seqno);

// Commit a checkpoint (crash-safe boundary for recovery).
ft_status_t ft_log_checkpoint(ft_log_t* log, ft_log_checkpoint_t cp, uint32_t* out_rec_off);

// Scan log and return last valid checkpoint (if any).
ft_status_t ft_log_recover_last_checkpoint(const ft_blockdev_t* bdev, ft_log_checkpoint_t* out_cp);

