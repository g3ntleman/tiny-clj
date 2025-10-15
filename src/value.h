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

// Pointer encoding/decoding macros (für zukünftige 32-bit Tagged Pointer)
// TODO: Implementiere diese für Phase 3 (32-bit Tagged Pointer)
/*
static inline CljValue make_pointer(void *ptr, uint8_t tag) {
    return ((uintptr_t)ptr & ~TAG_MASK) | tag;
}

static inline void* get_pointer(CljValue val) {
    return (void*)(val & ~TAG_MASK);
}

static inline uint8_t get_tag(CljValue val) {
    return val & TAG_MASK;
}
*/

// Immediate-Helpers (Phase 0: Wrapper um existierende Singletons)
static inline CljValue make_nil() {
    return clj_nil();
}

static inline CljValue make_bool(bool value) {
    return value ? clj_true() : clj_false();
}

static inline CljValue make_true() {
    return clj_true();
}

static inline CljValue make_false() {
    return clj_false();
}

// Type checking helpers
static inline bool is_nil(CljValue val) {
    return val == clj_nil();
}

static inline bool is_bool(CljValue val) {
    return val == clj_true() || val == clj_false();
}

static inline bool is_true(CljValue val) {
    return val == clj_true();
}

static inline bool is_false(CljValue val) {
    return val == clj_false();
}

// Wrapper für existierende Funktionen (Phase 0: Kompatibilität)
static inline CljValue make_int_v(int x) {
    return make_int(x);
}

static inline CljValue make_float_v(double x) {
    return make_float(x);
}

static inline CljValue make_string_v(const char *str) {
    return make_string(str);
}

static inline CljValue make_symbol_v(const char *name, const char *ns) {
    return make_symbol(name, ns);
}

#endif
