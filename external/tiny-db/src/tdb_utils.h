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

/* ============== Endian Configuration ============== */

#define TDB_ENDIAN_LITTLE 1234
#define TDB_ENDIAN_BIG 4321

/*
 * Unified on-wire byte order for tiny-db metadata and key suffix fields.
 * Default is little-endian (ESP32-native).
 */
#ifndef TDB_WIRE_ENDIAN
#define TDB_WIRE_ENDIAN TDB_ENDIAN_LITTLE
#endif

#if (TDB_WIRE_ENDIAN != TDB_ENDIAN_LITTLE) && (TDB_WIRE_ENDIAN != TDB_ENDIAN_BIG)
#error "TDB_WIRE_ENDIAN must be TDB_ENDIAN_LITTLE or TDB_ENDIAN_BIG"
#endif

/* ============== Fixed byte order encoding helpers ============== */

static inline void tdb_u16_le_write(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline uint16_t tdb_u16_le_read(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static inline void tdb_u16_be_write(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static inline uint16_t tdb_u16_be_read(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

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

static inline void tdb_u32_be_write(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static inline uint32_t tdb_u32_be_read(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

/* ============== Configured on-wire encoding helpers ============== */

static inline void tdb_u16_wire_write(uint8_t* p, uint16_t v) {
#if (TDB_WIRE_ENDIAN == TDB_ENDIAN_LITTLE)
    tdb_u16_le_write(p, v);
#else
    tdb_u16_be_write(p, v);
#endif
}

static inline uint16_t tdb_u16_wire_read(const uint8_t* p) {
#if (TDB_WIRE_ENDIAN == TDB_ENDIAN_LITTLE)
    return tdb_u16_le_read(p);
#else
    return tdb_u16_be_read(p);
#endif
}

static inline void tdb_u32_wire_write(uint8_t* p, uint32_t v) {
#if (TDB_WIRE_ENDIAN == TDB_ENDIAN_LITTLE)
    tdb_u32_le_write(p, v);
#else
    tdb_u32_be_write(p, v);
#endif
}

static inline uint32_t tdb_u32_wire_read(const uint8_t* p) {
#if (TDB_WIRE_ENDIAN == TDB_ENDIAN_LITTLE)
    return tdb_u32_le_read(p);
#else
    return tdb_u32_be_read(p);
#endif
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
