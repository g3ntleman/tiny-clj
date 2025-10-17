#ifndef TINY_CLJ_VALUE_H
#define TINY_CLJ_VALUE_H

#include "object.h"
#include "clj_string.h"
#include <stdint.h>

// CljValue als Alias für CljObject* (Phase 0: Kein Refactoring!)
typedef CljObject* CljValue;

// Tag-Definitionen für zukünftige Immediates (32-bit Tagged Pointer)
#define TAG_BITS 3
#define TAG_MASK 0x7

// === Immediate Types ===
#define TAG_FIXNUM   0   // 29-bit signed integer
#define TAG_CHAR     1   // 21-bit Unicode character  
#define TAG_SPECIAL  2   // nil, true, false
#define TAG_FLOAT16  3   // 16-bit half-precision float

// === Heap Types - Persistent (Pointer mit Tag) ===
#define TAG_STRING   4   // Pointer to CljString
#define TAG_VECTOR   5   // Pointer to CljPersistentVector
#define TAG_SYMBOL   6   // Pointer to CljSymbol
#define TAG_MAP      7   // Pointer to CljMap
#define TAG_LIST     8   // Pointer to CljList
#define TAG_SEQ      9   // Pointer to CljSeq

// === Heap Types - Transient (Pointer mit Tag) ===
#define TAG_TRANSIENT_VECTOR 10 // Pointer to CljPersistentVector (with transient flag)
#define TAG_TRANSIENT_MAP    11 // Pointer to CljMap (with transient flag)

// Pointer encoding/decoding macros (32-bit Tagged Pointer Implementation)
static inline CljValue make_pointer(void *ptr, uint8_t tag) {
    return (CljValue)(((uintptr_t)ptr & ~TAG_MASK) | tag);
}

static inline void* get_pointer(CljValue val) {
    return (void*)((uintptr_t)val & ~TAG_MASK);
}

static inline uint8_t get_tag(CljValue val) {
    return (uint8_t)((uintptr_t)val & TAG_MASK);
}

// === 32-bit Tagged Pointer Immediates ===

// Fixnum: 29-bit signed integer (Tag 0)
#define FIXNUM_BITS 29
#define FIXNUM_MAX ((1 << (FIXNUM_BITS - 1)) - 1)
#define FIXNUM_MIN (-(1 << (FIXNUM_BITS - 1)))

static inline CljValue make_fixnum(int32_t value) {
    // Check if value fits in 29 bits
    if (value < FIXNUM_MIN || value > FIXNUM_MAX) {
        // Fallback to heap allocation for large integers
        return make_int(value);
    }
    // Encode as tagged pointer: value << 3 | TAG_FIXNUM
    return (CljValue)(((uintptr_t)value << TAG_BITS) | TAG_FIXNUM);
}

static inline bool is_fixnum(CljValue val) {
    return get_tag(val) == TAG_FIXNUM;
}

static inline int32_t as_fixnum(CljValue val) {
    if (!is_fixnum(val)) return 0;
    // Extract value: right shift by TAG_BITS (arithmetic shift preserves sign)
    return (int32_t)((intptr_t)val >> TAG_BITS);
}

// Char: 21-bit Unicode character (Tag 1)
#define CHAR_BITS 21
#define CLJ_CHAR_MAX ((1 << CHAR_BITS) - 1)

static inline CljValue make_char(uint32_t codepoint) {
    if (codepoint > CLJ_CHAR_MAX) {
        // Fallback to heap allocation for invalid characters
        return make_string("?");
    }
    // Encode as tagged pointer: codepoint << 3 | TAG_CHAR
    return (CljValue)(((uintptr_t)codepoint << TAG_BITS) | TAG_CHAR);
}

static inline bool is_char(CljValue val) {
    return get_tag(val) == TAG_CHAR;
}

static inline uint32_t as_char(CljValue val) {
    if (!is_char(val)) return 0;
    return (uint32_t)((uintptr_t)val >> TAG_BITS);
}

// Special values: nil, true, false (Tag 2)
#define SPECIAL_NIL    0
#define SPECIAL_TRUE    1
#define SPECIAL_FALSE   2

static inline CljValue make_special(uint8_t special) {
    return (CljValue)(((uintptr_t)special << TAG_BITS) | TAG_SPECIAL);
}

static inline bool is_special(CljValue val) {
    return get_tag(val) == TAG_SPECIAL;
}

static inline uint8_t as_special(CljValue val) {
    if (!is_special(val)) return SPECIAL_NIL;
    return (uint8_t)((uintptr_t)val >> TAG_BITS);
}

// Float16: 16-bit half-precision float (Tag 3)
// Note: This is a simplified implementation - real Float16 would need proper conversion
static inline CljValue make_float16(float value) {
    // Simplified: store as 16-bit integer representation
    // In a real implementation, you'd need proper float16 conversion
    uint16_t bits = (uint16_t)(value * 1000.0f); // Simple scaling
    return (CljValue)(((uintptr_t)bits << TAG_BITS) | TAG_FLOAT16);
}

static inline bool is_float16(CljValue val) {
    return get_tag(val) == TAG_FLOAT16;
}

static inline float as_float16(CljValue val) {
    if (!is_float16(val)) return 0.0f;
    uint16_t bits = (uint16_t)((uintptr_t)val >> TAG_BITS);
    return (float)bits / 1000.0f; // Reverse scaling
}

// === Immediate Detection ===

/**
 * @brief Check if a CljValue is an immediate (32-bit tagged pointer)
 * @param val The value to check
 * @return true if immediate, false if heap object
 */
static inline bool is_immediate_value(CljValue val) {
    if (!val) return false;
    
    // Check if the value is a tagged pointer (low tag values)
    uint8_t tag = get_tag(val);
    return tag <= TAG_FLOAT16;  // Tags 0-3 are immediates
}

// Immediate-Helpers (Phase 1: 32-bit Tagged Pointer)
static inline CljValue make_nil() {
    return make_special(SPECIAL_NIL);
}

static inline CljValue make_bool(bool value) {
    return make_special(value ? SPECIAL_TRUE : SPECIAL_FALSE);
}

static inline CljValue make_true() {
    return make_special(SPECIAL_TRUE);
}

static inline CljValue make_false() {
    return make_special(SPECIAL_FALSE);
}

// Type checking helpers
static inline bool is_nil(CljValue val) {
    return is_special(val) && as_special(val) == SPECIAL_NIL;
}

static inline bool is_bool(CljValue val) {
    if (!is_special(val)) return false;
    uint8_t special = as_special(val);
    return special == SPECIAL_TRUE || special == SPECIAL_FALSE;
}

static inline bool is_true(CljValue val) {
    return is_special(val) && as_special(val) == SPECIAL_TRUE;
}

static inline bool is_false(CljValue val) {
    return is_special(val) && as_special(val) == SPECIAL_FALSE;
}

// Wrapper für existierende Funktionen (Phase 1: Immediates + Heap Fallback)
static inline CljValue make_int_v(int x) {
    // Try immediate first, fallback to heap for large numbers
    return make_fixnum(x);
}

static inline CljValue make_float_v(double x) {
    // For now, use heap allocation for all floats
    // TODO: Implement proper Float16 conversion
    return make_float(x);
}

static inline CljValue make_string_v(const char *str) {
    return make_string(str);
}

static inline CljValue make_symbol_v(const char *name, const char *ns) {
    return make_symbol(name, ns);
}

#endif
