#ifndef TINY_CLJ_VALUE_H
#define TINY_CLJ_VALUE_H

#include "object.h"
#include "clj_string.h"
#include <stdint.h>
#include <math.h>
#include <string.h>

// CljValue als Alias für CljObject* (Phase 0: Kein Refactoring!)
typedef CljObject* CljValue;

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
#define TAG_SPECIAL  5   // true, false (nil ist NULL)
#define TAG_FLOAT16  7   // 16-bit half-precision float

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

// === 32-bit Tagged Pointer Immediates ===

// SPECIAL sub-types: false nutzt 0 für Bit-Trick
#define SPECIAL_FALSE 0   // encoded: (0 << 3) | 5 = 5
#define SPECIAL_TRUE  8   // encoded: (8 << 3) | 5 = 69
#define SPECIAL_NIL   NULL 

// Falsy-Werte: nil(0) und false(5) haben niedrigstes Byte < 8
// nil   = 0x00000000
// false = 0x00000005
// true  = 0x00000045 (69 decimal)

// Immediate-Helpers (Phase 1: 32-bit Tagged Pointer)
static inline CljValue make_special(uint8_t special) {
    return (CljValue)(((uintptr_t)special << TAG_BITS) | TAG_SPECIAL);
}

// make_nil() removed - nil ist NULL

// Direct access to constants (Phase 8: use immediates directly)
// nil ist NULL, true/false sind make_special(SPECIAL_TRUE/FALSE)


static inline CljValue make_fixnum(int32_t value) {
    // Simplified implementation for debugging
    // Just return a non-NULL pointer with the tag
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

static inline bool is_special(CljValue val) {
    return get_tag(val) == TAG_SPECIAL;
}

static inline uint8_t as_special(CljValue val) {
    if (!is_special(val)) return 0;  // Default value for non-special
    return (uint8_t)((uintptr_t)val >> TAG_BITS);
}

// Float16: 16-bit half-precision float (Tag 3)
// Prefer compiler half types if available; fallback to portable IEEE-754 pack/unpack

#if defined(__FLT16_MANT_DIG__) || defined(__FP16_FORMAT_IEEE) || (defined(__clang__) && __has_extension(half_precision))
// Cast-based path using _Float16/__fp16 with round-to-nearest-even
static inline uint16_t float_to_half_bits(float f) {
    uint16_t hbits;
#if defined(__FLT16_MANT_DIG__)
    _Float16 hf = (_Float16)f;
#else
    __fp16 hf = (__fp16)f;
#endif
    // Bit-pack via memcpy to avoid aliasing UB
    memcpy(&hbits, &hf, sizeof(hbits));
    return hbits;
}

static inline float half_bits_to_float(uint16_t h) {
    float out;
#if defined(__FLT16_MANT_DIG__)
    _Float16 hf;
#else
    __fp16 hf;
#endif
    memcpy(&hf, &h, sizeof(h));
    out = (float)hf;
    return out;
}

#else
// Portable IEEE-754 binary16 pack/unpack with round-to-nearest-even; internal math uses float32
static inline uint16_t float_to_half_bits(float f) {
    union { float f; uint32_t u; } v = { f };
    uint32_t sign = (v.u >> 31) & 0x1;
    uint32_t exp  = (v.u >> 23) & 0xFF;
    uint32_t mant =  v.u & 0x7FFFFF;

    uint16_t h;
    if (exp == 0xFF) {
        // Inf / NaN
        uint16_t h_mant = (mant ? 0x200 : 0x000); // preserve quiet NaN
        h = (uint16_t)((sign << 15) | (0x1F << 10) | h_mant);
    } else if (exp > 0x70 + 0x1E) {
        // Overflow to Inf (>|max_half|)
        h = (uint16_t)((sign << 15) | (0x1F << 10));
    } else if (exp < 0x70 - 14) {
        // Subnormal or zero in half
        if (exp < 0x70 - 24) {
            // Underflow to zero
            h = (uint16_t)(sign << 15);
        } else {
            // Subnormal: renormalize mantissa with implicit leading 1
            uint32_t shift = (uint32_t)( (0x70 - 15) - exp );
            uint32_t mantissa = (mant | 0x800000) >> (shift + 13);
            // Round-to-nearest-even using remaining bits
            uint32_t round_bit = (mant >> (shift + 12)) & 1;
            uint32_t sticky = (mant & ((1u << (shift + 12)) - 1)) != 0;
            mantissa += (round_bit && (sticky || (mantissa & 1))); 
            h = (uint16_t)((sign << 15) | mantissa);
        }
    } else {
        // Normalized
        uint16_t h_exp = (uint16_t)(exp - (0x70 - 15));
        uint32_t mant_round = mant + 0x1000; // add 0.5 ulp at half precision (13th bit)
        if (mant_round & 0x800000) {
            // Mantissa overflow rounds into exponent
            h_exp += 1;
            mant_round = 0;
            if (h_exp >= 0x1F) {
                // Overflow to infinity
                h = (uint16_t)((sign << 15) | (0x1F << 10));
                return h;
            }
        }
        uint16_t h_mant = (uint16_t)((mant_round >> 13) & 0x3FF);
        h = (uint16_t)((sign << 15) | (h_exp << 10) | h_mant);
    }
    return h;
}

static inline float half_bits_to_float(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant =  h & 0x3FF;
    uint32_t out_sign = sign << 31;
    uint32_t out_exp;
    uint32_t out_mant;
    if (exp == 0) {
        if (mant == 0) {
            out_exp = 0; out_mant = 0; // zero
        } else {
            // subnormal: normalize
            int e = -14; // half bias
            while ((mant & 0x400) == 0) { mant <<= 1; e--; }
            mant &= 0x3FF;
            out_exp = (uint32_t)(e + 127) << 23;
            out_mant = mant << 13;
        }
    } else if (exp == 0x1F) {
        // Inf/NaN
        out_exp = 0xFF << 23;
        out_mant = mant ? (mant << 13) : 0;
    } else {
        // normalized
        out_exp = (uint32_t)(exp - 15 + 127) << 23;
        out_mant = mant << 13;
    }
    union { uint32_t u; float f; } v = { out_sign | out_exp | out_mant };
    return v.f;
}
#endif

static inline CljValue make_float16(float value) {
    uint16_t bits = float_to_half_bits(value);
    return (CljValue)(((uintptr_t)bits << TAG_BITS) | TAG_FLOAT16);
}

static inline bool is_float16(CljValue val) {
    return get_tag(val) == TAG_FLOAT16;
}

static inline float as_float16(CljValue val) {
    if (!is_float16(val)) return 0.0f;
    uint16_t bits = (uint16_t)((uintptr_t)val >> TAG_BITS);
    return half_bits_to_float(bits);
}

// === Immediate Detection ===

/**
 * @brief Check if a CljValue is an immediate (32-bit tagged pointer)
 * @param val The value to check
 * @return true if immediate, false if heap object
 */
static inline bool is_immediate(CljValue val) {
    if (!val) return false;  // NULL ist kein Immediate
    return ((uintptr_t)val & 0x1);  // Ungerade = Immediate
}

static inline bool is_heap_object(CljValue val) {
    if (!val) return false;  // NULL ist kein Heap-Objekt
    return !((uintptr_t)val & 0x1);  // Gerade = Heap-Objekt
}


// Type checking helpers

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

// Ultra-schneller Bit-Trick für Truthy/Falsy
// clj_is_truthy() ist in object.h definiert

// Falsy: nil oder false (nicht clj_ Präfix für Konsistenz mit is_fixnum etc.)
static inline bool is_falsy(CljValue val) {
    return ((uintptr_t)val & 0xFF) < 8;
}

// Safe cast from ID to CljObject* with debug checks
#ifdef DEBUG
static inline CljObject* ID_TO_OBJ(ID id) {
    if (!id) return NULL;
    CljValue val = (CljValue)id;
    // Check if it's an immediate or heap object
    if (is_fixnum(val) || is_float16(val) || is_char(val) || is_special(val)) {
        // It's an immediate - return as-is (will be treated as CljObject*)
        return (CljObject*)val;
    }
    // It's a heap object - verify it has a valid type
    CljObject* obj = (CljObject*)val;
    if (obj->type < CLJ_TYPE_COUNT) {
        return obj;
    }
    fprintf(stderr, "ID_TO_OBJ: Invalid object type %d at %p\n", obj->type, obj);
    abort();
}
#else
#define ID_TO_OBJ(id) ((CljObject*)(id))
#endif

// Safe cast from CljObject* to ID (always safe, no check needed)
#define OBJ_TO_ID(obj) ((ID)(obj))

// Convenience macros for type checking with ID or any pointer type
// These eliminate the need to cast to CljValue before checking
#define IS_FIXNUM(val) is_fixnum((CljValue)(val))
#define AS_FIXNUM(val) as_fixnum((CljValue)(val))
#define IS_FLOAT16(val) is_float16((CljValue)(val))
#define AS_FLOAT16(val) as_float16((CljValue)(val))
#define IS_CHAR(val) is_char((CljValue)(val))
#define AS_CHAR(val) as_char((CljValue)(val))
#define IS_SPECIAL(val) is_special((CljValue)(val))
#define AS_SPECIAL(val) as_special((CljValue)(val))

// Wrapper für existierende Funktionen (Phase 1: Immediates + Heap Fallback)
static inline CljValue make_int_v(int x) {
    // Try immediate first, fallback to heap for large numbers
    return make_fixnum(x);
}

static inline CljValue make_float_v(double x) {
    // For now, use heap allocation for all floats
    // TODO: Implement proper Float16 conversion
    return (CljValue)make_float16((float)x);
}

static inline CljValue make_string_v(const char *str) {
    return make_string(str);
}

static inline CljValue make_symbol_v(const char *name, const char *ns) {
    return make_symbol(name, ns);
}

#endif
