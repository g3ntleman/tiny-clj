#ifndef TINY_CLJ_VALUE_H
#define TINY_CLJ_VALUE_H

#include "object.h"
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// Forward declarations for string functions
struct CljString;
extern struct CljString* string_empty_singleton;

// Forward declarations for namespace functions (to avoid circular dependency)
// namespace.h includes map.h, which would include value.h again
struct CljNamespace;
extern struct CljNamespace* ns_get_or_create(const char *name, const char *file);

// CljValue is already defined in object.h to avoid circular dependency

// ID-Typ ist in object.h definiert


// Tag-Definitionen für zukünftige Immediates (32-bit Tagged Pointer)
#define TAG_BITS 3
#define TAG_MASK 0x7

// 29-bit signed integer range
#define FIXNUM_BITS 29
#define FIXNUM_MAX ((1 << (FIXNUM_BITS - 1)) - 1)    // 536870911
#define FIXNUM_MIN (-(1 << (FIXNUM_BITS - 1)))       // -536870912

// === Immediate Types (ungerade Tags 1, 3, 5, 7) ===
#define TAG_FIXNUM   1   // 29-bit signed integer
#define TAG_CHAR     3   // 21-bit Unicode character
#define TAG_BOOL     5   // true, false (nil ist NULL)
#define TAG_FIXED    7   // 29-bit Q16.13 fixed-point

// === Heap Types (gerade Tags 0, 2, 4, 6) ===
#define TAG_POINTER  0   // Normaler Heap-Pointer (alle Standard-Objekte)
#define TAG_STRING   2   // Reserved für zukünftige Optimierung
#define TAG_VECTOR   4   // Reserved für zukünftige Optimierung
#define TAG_MAP      6   // Reserved für zukünftige Optimierung

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

// Note: GET_TAG() has been consolidated into TAG() macro in object.h
// TAG() returns CljType and works with both immediates and heap objects

// === 32-bit Tagged Pointer Immediates ===

// SPECIAL sub-types: false nutzt 0 für Bit-Trick
#define SPECIAL_FALSE 0   // encoded: (0 << 3) | 5 = 5
#define SPECIAL_NIL   NULL 

// Falsy-Werte: nil(0) und false(5) haben niedrigstes Byte < 8
// nil   = 0x00000000
// false = 0x00000005
// true  = 0x00000045 (69 decimal)

// Immediate-Helpers (Phase 1: 32-bit Tagged Pointer)
static inline CljValue make_special(uint8_t special) {
    return (CljValue)(((uintptr_t)special << TAG_BITS) | TAG_BOOL);
}

// make_nil() removed - nil ist NULL

// Direct access to constants (Phase 8: use immediates directly)
// nil ist NULL, true/false sind constants
// clj_true is the source of truth - SPECIAL_TRUE is derived from it
#define clj_true  ((CljValue)(((uintptr_t)8 << TAG_BITS) | TAG_BOOL))  // 8 << 3 | 5 = 69
#define clj_false ((CljValue)(((uintptr_t)SPECIAL_FALSE << TAG_BITS) | TAG_BOOL))

// SPECIAL_TRUE is now derived from clj_true (consolidation)
// For switch cases, we need the integer constant 8 directly
#define SPECIAL_TRUE  8  // Same as: as_special(clj_true), but constant for switch cases


static inline CljValue fixnum(int32_t value) {
    // Create a tagged pointer with the value
    // The value is stored in the upper bits, tag in the lower bits
    // We need to shift the value left by TAG_BITS to make room for the tag
    return (CljValue)(((uintptr_t)value << TAG_BITS) | TAG_FIXNUM);
}

static inline bool is_fixnum(CljValue val) {
    return get_tag(val) == TAG_FIXNUM;
}

static inline int as_fixnum(CljValue val) {
    CLJ_ASSERT(is_fixnum(val));
    // Extract value: right shift by TAG_BITS (arithmetic shift preserves sign)
    return (int)((intptr_t)val >> TAG_BITS);
}

// Char: 21-bit Unicode character (Tag 1)
#define CHAR_BITS 21
#define CLJ_CHAR_MAX ((1 << CHAR_BITS) - 1)

// Function declarations for large functions moved to value.c
CljValue character(uint32_t codepoint);
CljValue fixed(float value);
// make_string moved to strings.h - forward declaration for compatibility
struct CljString* make_string(const char *str);


static inline bool is_character(CljValue val) {
    return get_tag(val) == TAG_CHAR;
}

static inline uint32_t as_character(CljValue val) {
    if (!is_character(val)) return 0;
    return (uint32_t)((uintptr_t)val >> TAG_BITS);
}

// Legacy aliases for compatibility
static inline bool is_char(CljValue val) {
    return is_character(val);
}

static inline uint32_t as_char(CljValue val) {
    return as_character(val);
}

static inline bool is_special(CljValue val) {
    return get_tag(val) == TAG_BOOL;
}

static inline uint8_t as_special(CljValue val) {
    if (!is_special(val)) return 0;  // Default value for non-special
    return (uint8_t)((uintptr_t)val >> TAG_BITS);
}

// Fixed-Point: 29-bit Q16.13 fixed-point (Tag 7)
// 1 bit sign + 16 bits integer + 13 bits fraction
// Range: ±32767.9998, Precision: 1/8192 ≈ 0.00012


static inline bool is_fixed(CljValue val) {
    return get_tag(val) == TAG_FIXED;
}

static inline float as_fixed(CljValue val) {
    assert(is_fixed(val));
    int32_t fixed = (int32_t)((intptr_t)val >> TAG_BITS);
    return (float)fixed / 8192.0f;
}

// === Immediate Detection ===

/**
 * @brief Check if a CljValue is an immediate (32-bit tagged pointer)
 * @param val The value to check
 * @return true if immediate, false if heap object
 */
static inline bool is_immediate(CljValue val) {
    if (!val) return true;  // NULL ist kein Immediate
    return ((uintptr_t)val & 0x1);  // Ungerade = Immediate
}

static inline bool is_heap_object(CljValue val) {
    return !is_immediate(val);
}


// Type checking helpers

static inline bool is_bool(CljValue val) {
    if (!is_special(val)) return false;
    uint8_t special = as_special(val);
    return special == SPECIAL_TRUE || special == SPECIAL_FALSE;
}


// Ultra-schneller Bit-Trick für Truthy/Falsy
// clj_is_truthy() ist in object.h definiert

// Falsy: nil oder false (nicht clj_ Präfix für Konsistenz mit is_fixnum etc.)
static inline bool is_falsy(CljValue val) {
    return ((uintptr_t)val & 0xFF) < 8;
}

// Helper macro to check if a value is an immediate (not a heap object)
// Optimized: immediate values have odd tags (1,3,5,7), heap objects have even tags (0,2,4,6)
#define IS_IMMEDIATE(val) (((uintptr_t)(val) & TAG_MASK) & 1)

// Safe cast from ID to ID with debug checks
#ifdef DEBUG
static inline ID CHECKED(ID id) {
    if (!id) return NULL;
    CljValue val = (CljValue)id;
    // Check if it's an immediate or heap object
    if (IS_IMMEDIATE(val)) {
        // It's an immediate - return as-is
        return id;
    }
    // It's a heap object - verify it has a valid type
    CljObject* obj = (CljObject*)val;
    if (obj->type < CLJ_TYPE_COUNT) {
        return id;
    }
    fprintf(stderr, "ID_TO_OBJ: Invalid object type %d at %p\n", obj->type, obj);
    abort();
}
#else
#define ID_TO_OBJ(id) (id)
#endif


// Convenience macros for value extraction with ID or any pointer type
// These eliminate the need to cast to CljValue before extraction
// Note: IS_* macros removed - use TAG() and switch statements instead
#define AS_FIXNUM(val) as_fixnum((CljValue)(val))
#define AS_FIXED(val) as_fixed((CljValue)(val))
#define AS_CHAR(val) as_char((CljValue)(val))
#define AS_SPECIAL(val) as_special((CljValue)(val))

// Wrapper für existierende Funktionen (Phase 1: Immediates + Heap Fallback)
static inline CljValue integer(int x) {
    // Try immediate first, fallback to heap for large numbers
    return fixnum(x);
}




#endif
