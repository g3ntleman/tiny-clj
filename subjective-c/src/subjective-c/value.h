#ifndef SUBJECTIVE_C_VALUE_H
#define SUBJECTIVE_C_VALUE_H

#include "object.h"
#include "common.h"
#ifndef CLJ_ASSERT
#define CLJ_ASSERT(expr) ((void)0)
#endif
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

struct CljString;
extern struct CljString* string_empty_singleton;
struct CljNamespace;
extern struct CljNamespace* ns_get_or_create(const char *cname, const char *file);

#define TAG_BITS 3
#define TAG_MASK 0x7

#define FIXNUM_BITS 29
#define FIXNUM_MAX ((1 << (FIXNUM_BITS - 1)) - 1)
#define FIXNUM_MIN (-(1 << (FIXNUM_BITS - 1)))

#define TAG_FIXNUM   1
#define TAG_CHAR     3
#define TAG_BOOL     5
#define TAG_FIXED    7

#define TAG_POINTER  0
#define TAG_STRING   2
#define TAG_VECTOR   4
#define TAG_MAP      6

static inline CljValue make_pointer(void *ptr, uint8_t tag) {
    return (CljValue)(((uintptr_t)ptr & ~TAG_MASK) | tag);
}

static inline void* get_pointer(CljValue val) {
    return (void*)((uintptr_t)val & ~TAG_MASK);
}

static inline uint8_t get_tag(CljValue val) {
    return (uint8_t)((uintptr_t)val & TAG_MASK);
}

#define SPECIAL_FALSE 0
#define SPECIAL_NIL   NULL

#define clj_true  ((CljValue)(((uintptr_t)8 << TAG_BITS) | TAG_BOOL))
#define clj_false ((CljValue)(((uintptr_t)SPECIAL_FALSE << TAG_BITS) | TAG_BOOL))
#define SPECIAL_TRUE  8

static inline CljValue fixnum(int32_t value) {
    return (CljValue)(((uintptr_t)value << TAG_BITS) | TAG_FIXNUM);
}

static inline bool is_fixnum(CljValue val) {
    return get_tag(val) == TAG_FIXNUM;
}

static inline int as_fixnum(CljValue val) {
    CLJ_ASSERT(is_fixnum(val));
    return (int)((intptr_t)val >> TAG_BITS);
}

#define CHAR_BITS 21
#define CLJ_CHAR_MAX ((1 << CHAR_BITS) - 1)

/** @brief Create character value from codepoint
 * @param codepoint Unicode codepoint
 * @return Character value
 */
CljValue character(uint32_t codepoint);

/** @brief Create fixed-point number from float
 * @param value Float value
 * @return Fixed-point value
 */
CljValue fixed(float value);

/** @brief Create string from C string
 * @param str C string
 * @return New string object
 */
struct CljString* make_string(const char *str);

static inline bool is_character(CljValue val) {
    return get_tag(val) == TAG_CHAR;
}

static inline uint32_t as_character(CljValue val) {
    if (!is_character(val)) return 0;
    return (uint32_t)((uintptr_t)val >> TAG_BITS);
}

static inline bool is_special(CljValue val) {
    return get_tag(val) == TAG_BOOL;
}

static inline uint8_t as_special(CljValue val) {
    if (!is_special(val)) return 0;
    return (uint8_t)((uintptr_t)val >> TAG_BITS);
}

static inline bool is_fixed(CljValue val) {
    return get_tag(val) == TAG_FIXED;
}

static inline float as_fixed(CljValue val) {
    assert(is_fixed(val));
    int32_t fixed = (int32_t)((intptr_t)val >> TAG_BITS);
    return (float)fixed / 8192.0f;
}

static inline bool is_immediate(CljValue val) {
    if (!val) return true;
    return ((uintptr_t)val & 0x1);
}

static inline bool is_heap_object(CljValue val) {
    return !is_immediate(val);
}

static inline bool is_bool(CljValue val) {
    if (!is_special(val)) return false;
    uint8_t special = as_special(val);
    return special == SPECIAL_TRUE || special == SPECIAL_FALSE;
}

static inline bool is_falsy(CljValue val) {
    return ((uintptr_t)val & 0xFF) < 8;
}

#define IS_IMMEDIATE(val) (((uintptr_t)(val) & TAG_MASK) & 1)

#define AS_FIXNUM(val) as_fixnum((CljValue)(val))
#define AS_FIXED(val) as_fixed((CljValue)(val))
#define AS_CHAR(val) as_character((CljValue)(val))
#define AS_SPECIAL(val) as_special((CljValue)(val))

static inline CljValue integer(int x) {
    return fixnum(x);
}

#endif // SUBJECTIVE_C_VALUE_H
