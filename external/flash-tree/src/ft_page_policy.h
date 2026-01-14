// ft_page_policy.h - Derive page/record sizing policy from blockdev geometry.
//
// Internal helper (not part of the public API). Designed to keep sizing logic DRY
// across KV, mpool, and tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ft_blockdev.h"

typedef struct ft_page_policy {
    uint32_t record_size; /* Variant B: erase_granularity */
    uint32_t page_size;   /* B-Tree/mpool page payload size */
    uint32_t header_size; /* On-flash per-record header size */
} ft_page_policy_t;

/*
 * Compute sizing policy for "Variant B":
 *   record_size == erase_granularity
 *   record = header + page_payload
 *   page_payload == erase_granularity - header_size
 *
 * The function also validates alignment constraints against read/prog granularities.
 */
ft_status_t ft_page_policy_compute_variant_b(const ft_blockdev_geom_t* geom, size_t header_size,
                                             ft_page_policy_t* out);
