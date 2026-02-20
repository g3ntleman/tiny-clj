// tdb_page_policy.c - Derive page/record sizing policy from blockdev geometry.

#include "tdb_page_policy.h"

#include "tdb_utils.h"

/**
 * @brief tdb_page_policy_compute_variant_b.
 * @param geom Block-device geometry descriptor.
 * @param header_size Length in bytes.
 * @param out Page-policy configuration.
 * @return Status code (TDB_OK on success).
 */
tdb_status_t tdb_page_policy_compute_variant_b(const tdb_blockdev_geom_t* geom, size_t header_size,
                                             tdb_page_policy_t* out) {
    if (!geom || !out)
        return TDB_ERR_INVALID_ARG;

    const uint32_t eg = geom->erase_granularity;
    const uint32_t rg = geom->read_granularity;
    const uint32_t pg = geom->prog_granularity;

    if (eg == 0 || rg == 0 || pg == 0)
        return TDB_ERR_INVALID_ARG;
    if (!tdb_is_pow2(eg) || !tdb_is_pow2(rg) || !tdb_is_pow2(pg))
        return TDB_ERR_INVALID_ARG;

    if (header_size == 0)
        return TDB_ERR_INVALID_ARG;
    if (header_size >= (size_t)eg)
        return TDB_ERR_INVALID_ARG;

    const uint32_t hs = (uint32_t)header_size;
    const uint32_t ps = eg - hs;

    // Ensure each individual operation length is compatible with device wrappers:
    // - mpool writes header and page payload via separate prog() calls
    // - mpool reads header and page payload via separate read() calls
    if ((hs % pg) != 0 || (ps % pg) != 0)
        return TDB_ERR_INVALID_ARG;
    if ((hs % rg) != 0 || (ps % rg) != 0)
        return TDB_ERR_INVALID_ARG;

    // Ensure record increment keeps subsequent headers aligned.
    if (((hs + ps) % pg) != 0)
        return TDB_ERR_INVALID_ARG;
    if (((hs + ps) % rg) != 0)
        return TDB_ERR_INVALID_ARG;

    out->record_size = eg;
    out->page_size = ps;
    out->header_size = hs;
    return TDB_OK;
}
