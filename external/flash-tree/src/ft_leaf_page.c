// ft_leaf_page.c - Minimal leaf page codec with key prefix-compression.

#include "ft_leaf_page.h"

#include "ft_btree.h" // for ft_lex_bytes_cmp

#include <string.h>

typedef struct __attribute__((packed)) ft_leaf_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint16_t n_entries;
    uint16_t reserved;
} ft_leaf_hdr_t;

static size_t common_prefix_len(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len) {
    const size_t n = (a_len < b_len) ? a_len : b_len;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return i;
    }
    return n;
}

static ft_status_t leaf_read_hdr(const uint8_t* page, size_t page_len, ft_leaf_hdr_t* out) {
    if (!page || !out) return FT_ERR_INVALID_ARG;
    if (page_len < sizeof(ft_leaf_hdr_t)) return FT_ERR_INVALID_ARG;
    memcpy(out, page, sizeof(*out));
    if (out->magic != FT_LEAF_MAGIC || out->version != FT_LEAF_VERSION) return FT_ERR_CORRUPT;
    if ((out->flags & FT_LEAF_FLAG_LEAF) == 0) return FT_ERR_CORRUPT;
    return FT_OK;
}

ft_status_t ft_leaf_init_empty(uint8_t* page, size_t page_len) {
    if (!page) return FT_ERR_INVALID_ARG;
    if (page_len < sizeof(ft_leaf_hdr_t)) return FT_ERR_INVALID_ARG;
    memset(page, 0xFF, page_len);
    ft_leaf_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = FT_LEAF_MAGIC;
    hdr.version = (uint16_t)FT_LEAF_VERSION;
    hdr.flags = (uint16_t)FT_LEAF_FLAG_LEAF;
    hdr.n_entries = 0;
    hdr.reserved = 0;
    memcpy(page, &hdr, sizeof(hdr));
    return FT_OK;
}

ft_status_t ft_leaf_encode(uint8_t* page, size_t page_len,
                           const ft_leaf_entry_ref_t* entries, size_t n_entries,
                           ft_leaf_stats_t* out_stats) {
    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    if (!page) return FT_ERR_INVALID_ARG;
    if (page_len < sizeof(ft_leaf_hdr_t)) return FT_ERR_INVALID_ARG;
    if (!entries && n_entries != 0) return FT_ERR_INVALID_ARG;
    if (n_entries > 0xFFFFu) return FT_ERR_UNSUPPORTED;

    // Rewrite whole page for NOR semantics (program is 1->0 only).
    ft_status_t st = ft_leaf_init_empty(page, page_len);
    if (st != FT_OK) return st;

    size_t off = sizeof(ft_leaf_hdr_t);

    const uint8_t* prev_key = NULL;
    size_t prev_len = 0;

    for (size_t i = 0; i < n_entries; i++) {
        const ft_leaf_entry_ref_t* e = &entries[i];
        if ((!e->key && e->key_len != 0) || (!e->val && e->val_len != 0)) return FT_ERR_INVALID_ARG;
        if (e->key_len > 0xFFFFu) return FT_ERR_UNSUPPORTED;
        if (e->val_len > 0xFFFFFFFFu) return FT_ERR_UNSUPPORTED;

        // Enforce sorted order.
        if (i > 0) {
            int c = ft_lex_bytes_cmp(entries[i - 1].key, entries[i - 1].key_len, e->key, e->key_len);
            if (c > 0) return FT_ERR_INVALID_ARG;
        }

        const uint8_t* k = (const uint8_t*)e->key;
        const size_t klen = e->key_len;
        const size_t shared = (i == 0) ? 0 : common_prefix_len(prev_key, prev_len, k, klen);
        const size_t suffix_len = klen - shared;

        if (shared > 0xFFFFu || suffix_len > 0xFFFFu) return FT_ERR_UNSUPPORTED;

        const size_t need = 2 + 2 + suffix_len + 4 + e->val_len;
        if (off + need > page_len) return FT_ERR_UNSUPPORTED;

        // shared_prefix_len (u16)
        uint16_t s16 = (uint16_t)shared;
        memcpy(page + off, &s16, sizeof(s16));
        off += sizeof(s16);

        // suffix_len (u16)
        uint16_t suf16 = (uint16_t)suffix_len;
        memcpy(page + off, &suf16, sizeof(suf16));
        off += sizeof(suf16);

        // suffix_bytes
        if (suffix_len) {
            memcpy(page + off, k + shared, suffix_len);
            off += suffix_len;
        }

        // val_len (u32)
        uint32_t v32 = (uint32_t)e->val_len;
        memcpy(page + off, &v32, sizeof(v32));
        off += sizeof(v32);

        // val_bytes
        if (e->val_len) {
            memcpy(page + off, e->val, e->val_len);
            off += e->val_len;
        }

        prev_key = k;
        prev_len = klen;

        if (out_stats) {
            out_stats->n_entries++;
            out_stats->raw_key_bytes += klen;
            out_stats->stored_key_bytes += suffix_len;
        }
    }

    // Patch header entry count.
    ft_leaf_hdr_t hdr;
    memcpy(&hdr, page, sizeof(hdr));
    hdr.n_entries = (uint16_t)n_entries;
    memcpy(page, &hdr, sizeof(hdr));
    return FT_OK;
}

static ft_status_t leaf_iter_internal(const uint8_t* page, size_t page_len,
                                      const void* prefix, size_t prefix_len,
                                      uint8_t* key_scratch, size_t key_scratch_cap,
                                      ft_key_cb cb, void* arg,
                                      ft_leaf_stats_t* out_stats) {
    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    if (!page || !key_scratch || !cb) return FT_ERR_INVALID_ARG;
    if ((!prefix && prefix_len != 0)) return FT_ERR_INVALID_ARG;

    ft_leaf_hdr_t hdr;
    ft_status_t st = leaf_read_hdr(page, page_len, &hdr);
    if (st != FT_OK) return st;

    size_t off = sizeof(ft_leaf_hdr_t);
    size_t prev_len = 0;

    for (uint16_t i = 0; i < hdr.n_entries; i++) {
        if (off + 2 + 2 + 4 > page_len) return FT_ERR_CORRUPT;

        uint16_t shared = 0;
        uint16_t suffix_len = 0;
        memcpy(&shared, page + off, sizeof(shared));
        off += sizeof(shared);
        memcpy(&suffix_len, page + off, sizeof(suffix_len));
        off += sizeof(suffix_len);

        if ((size_t)shared > prev_len) return FT_ERR_CORRUPT;
        if (off + (size_t)suffix_len + 4 > page_len) return FT_ERR_CORRUPT;

        const size_t key_len = (size_t)shared + (size_t)suffix_len;
        if (key_len > key_scratch_cap) return FT_ERR_UNSUPPORTED;

        // Reconstruct key in scratch: keep prefix part from previous key, append suffix.
        // shared bytes are already present from previous iteration in key_scratch[0:prev_len].
        if (suffix_len) memcpy(key_scratch + shared, page + off, suffix_len);
        off += (size_t)suffix_len;

        uint32_t val_len = 0;
        memcpy(&val_len, page + off, sizeof(val_len));
        off += sizeof(val_len);
        if (off + (size_t)val_len > page_len) return FT_ERR_CORRUPT;

        const void* val_ptr = (val_len ? (const void*)(page + off) : NULL);
        off += (size_t)val_len;

        prev_len = key_len;

        if (out_stats) {
            out_stats->n_entries++;
            out_stats->raw_key_bytes += key_len;
            out_stats->stored_key_bytes += (size_t)suffix_len;
        }

        // Prefix filter (linear, keeps code small).
        if (prefix_len) {
            if (key_len < prefix_len) continue;
            if (memcmp(key_scratch, prefix, prefix_len) != 0) continue;
        }

        st = cb(key_scratch, key_len, val_ptr, (size_t)val_len, arg);
        if (st != FT_OK) return st;
    }

    return FT_OK;
}

ft_status_t ft_leaf_iter(const uint8_t* page, size_t page_len,
                         uint8_t* key_scratch, size_t key_scratch_cap,
                         ft_key_cb cb, void* arg,
                         ft_leaf_stats_t* out_stats) {
    return leaf_iter_internal(page, page_len, NULL, 0, key_scratch, key_scratch_cap, cb, arg, out_stats);
}

ft_status_t ft_leaf_iter_prefix(const uint8_t* page, size_t page_len,
                                const void* prefix, size_t prefix_len,
                                uint8_t* key_scratch, size_t key_scratch_cap,
                                ft_key_cb cb, void* arg,
                                ft_leaf_stats_t* out_stats) {
    return leaf_iter_internal(page, page_len, prefix, prefix_len, key_scratch, key_scratch_cap, cb, arg, out_stats);
}

