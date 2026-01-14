// ft_btree.c - Minimal sorted-key index helpers.
//
// Provides lexicographic comparison and prefix iteration for key-value pairs.

#include "ft_btree.h"
#include "ft_utils.h"

#include <string.h>

int ft_lex_bytes_cmp(const void* a, size_t a_len, const void* b, size_t b_len) {
    const size_t min_len = (a_len < b_len) ? a_len : b_len;
    int c = 0;
    if (min_len)
        c = memcmp(a, b, min_len);
    if (c != 0)
        return c;
    if (a_len < b_len)
        return -1;
    if (a_len > b_len)
        return 1;
    return 0;
}

size_t ft_lower_bound_kv(const ft_kv_ref_t* entries, size_t n, const void* key, size_t key_len) {
    size_t lo = 0;
    size_t hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = ft_lex_bytes_cmp(entries[mid].key, entries[mid].key_len, key, key_len);
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/**
 * Iterate over all entries with the given prefix.
 * Entries must be sorted in lexicographic order.
 */
ft_status_t ft_iter_prefix_kv(const ft_kv_ref_t* entries, size_t n, const void* prefix,
                              size_t prefix_len, ft_key_cb cb, void* arg) {
    if (!entries || (!prefix && prefix_len != 0) || !cb)
        return FT_ERR_INVALID_ARG;

    /* Find first entry with prefix */
    size_t i = ft_lower_bound_kv(entries, n, prefix, prefix_len);

    /* Iterate while keys have the prefix */
    for (; i < n; i++) {
        if (!ft_has_prefix(entries[i].key, entries[i].key_len, prefix, prefix_len)) {
            break;
        }
        ft_status_t st =
            cb(entries[i].key, entries[i].key_len, entries[i].val, entries[i].val_len, arg);
        if (st != FT_OK)
            return st;
    }
    return FT_OK;
}
