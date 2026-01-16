// tdb_utils.h - Common utility functions for tiny-db.
//
// Centralizes frequently used helper functions to avoid code duplication
// and maintain consistent implementations across modules.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Math & Alignment Utilities ============== */

/**
 * Return maximum of two uint32_t values.
 */
static inline uint32_t tdb_max_u32(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
}

/**
 * Return minimum of two uint32_t values.
 */
static inline uint32_t tdb_min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

/**
 * Align value up to the next multiple of alignment.
 * @param x Value to align
 * @param align Alignment (must be power of 2)
 * @return Aligned value, or 0 if align is 0
 */
static inline uint32_t tdb_align_up_u32(uint32_t x, uint32_t align) {
    if (align == 0)
        return 0;
    uint32_t r = x % align;
    return r ? (x + (align - r)) : x;
}

/**
 * Check if value is a power of 2.
 */
static inline int tdb_is_pow2(uint32_t x) {
    return x && ((x & (x - 1u)) == 0);
}

/**
 * Check if address is aligned to granularity.
 */
static inline int tdb_is_aligned(uint32_t addr, uint32_t gran) {
    return (gran == 0) ? 0 : ((addr % gran) == 0);
}

/**
 * Check if length is aligned to granularity.
 */
static inline int tdb_is_len_aligned(size_t len, uint32_t gran) {
    return (gran == 0) ? 0 : ((len % (size_t)gran) == 0);
}

/* ============== Memory/String Utilities ============== */

/* ============== Fixed byte order encoding (little-endian) ============== */

/**
 * Write a uint32_t in little-endian order.
 */
static inline void tdb_u32_le_write(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/**
 * Read a uint32_t in little-endian order.
 */
static inline uint32_t tdb_u32_le_read(const uint8_t* p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * Check if key has the given prefix (byte-wise comparison).
 * @return 1 if key starts with prefix, 0 otherwise
 */
static inline int tdb_has_prefix(const void* key, size_t key_len, const void* prefix,
                                size_t prefix_len) {
    if (prefix_len == 0)
        return 1;
    if (key_len < prefix_len)
        return 0;
    // Use compiler builtin if available, otherwise falls back to loop
    const unsigned char* k = (const unsigned char*)key;
    const unsigned char* p = (const unsigned char*)prefix;
    for (size_t i = 0; i < prefix_len; i++) {
        if (k[i] != p[i])
            return 0;
    }
    return 1;
}

/* ============== All-0xFF Detection (for erased flash) ============== */

/**
 * Check if buffer contains all 0xFF bytes (erased flash marker).
 * @return 1 if all bytes are 0xFF, 0 otherwise
 */
static inline int tdb_is_all_erased(const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0xFF)
            return 0;
    }
    return 1;
}

#ifdef __cplusplus
}
#endif
