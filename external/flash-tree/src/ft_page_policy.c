// ft_page_policy.c - Derive page/record sizing policy from blockdev geometry.

#include "ft_page_policy.h"

#include "ft_utils.h"

ft_status_t ft_page_policy_compute_variant_b(const ft_blockdev_geom_t* geom, size_t header_size,
                                             ft_page_policy_t* out) {
    if (!geom || !out)
        return FT_ERR_INVALID_ARG;

    const uint32_t eg = geom->erase_granularity;
    const uint32_t rg = geom->read_granularity;
    const uint32_t pg = geom->prog_granularity;

    if (eg == 0 || rg == 0 || pg == 0)
        return FT_ERR_INVALID_ARG;
    if (!ft_is_pow2(eg) || !ft_is_pow2(rg) || !ft_is_pow2(pg))
        return FT_ERR_INVALID_ARG;

    if (header_size == 0)
        return FT_ERR_INVALID_ARG;
    if (header_size >= (size_t)eg)
        return FT_ERR_INVALID_ARG;

    const uint32_t hs = (uint32_t)header_size;
    const uint32_t ps = eg - hs;

    // Ensure each individual operation length is compatible with device wrappers:
    // - mpool writes header and page payload via separate prog() calls
    // - mpool reads header and page payload via separate read() calls
    if ((hs % pg) != 0 || (ps % pg) != 0)
        return FT_ERR_INVALID_ARG;
    if ((hs % rg) != 0 || (ps % rg) != 0)
        return FT_ERR_INVALID_ARG;

    // Ensure record increment keeps subsequent headers aligned.
    if (((hs + ps) % pg) != 0)
        return FT_ERR_INVALID_ARG;
    if (((hs + ps) % rg) != 0)
        return FT_ERR_INVALID_ARG;

    out->record_size = eg;
    out->page_size = ps;
    out->header_size = hs;
    return FT_OK;
}
