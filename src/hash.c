// Hash functions for all Clojure types (registered via runtime_init)

#include "hash.h"
#include "symbol.h"
#include <subjective-c/vector.h>
#include <subjective-c/map.h>
#include "list.h"
#include <subjective-c/strings.h>
#include "value.h"
#include <subjective-c/object.h>
#include "kv_macros.h"
#include <subjective-c/callbacks.h>

#include <subjective-c/instant.h>
#include <subjective-c/uuid.h>

#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME  16777619u
#define FNV_MIX(h, v) (((h) ^ (v)) * FNV1A_PRIME)

static inline uint32_t fnv1a(const char *s) {
    uint32_t h = FNV1A_OFFSET;
    for (; *s; s++) h = FNV_MIX(h, (uint8_t)*s);
    return h;
}

static inline uint32_t fnv1a_continue(uint32_t h, const char *s) {
    for (; *s; s++) h = FNV_MIX(h, (uint8_t)*s);
    return h;
}

static uint32_t hash_symbol(CljSymbol *sym) {
    if (!sym || !sym->cname) return 0;
    if (sym->ns_name && sym->ns_name->cname) {
        uint32_t h = fnv1a(sym->ns_name->cname);
        return fnv1a_continue(FNV_MIX(h, '/'), sym->cname);
    }
    return fnv1a(sym->cname);
}

static uint32_t hash_vector(CljVector *vec) {
    if (!vec) return 0;
    uint32_t h = 0;
    int n = vector_count(vec);
    for (int i = 0; i < n; i++)
        h = FNV_MIX(h, clj_hash_full(vector_nth(vec, i)));
    return h;
}

static uint32_t hash_map(CljMap *map) {
    if (!map) return 0;
    uint32_t h = 0;
    MAP_FOR_EACH(map, key, value) {
        h = FNV_MIX(h, clj_hash_full(key));
        h = FNV_MIX(h, clj_hash_full(value));
    }
    return h;
}

static uint32_t hash_list(CljList *list) {
    uint32_t h = 0;
    for (CljList *c = list; c; ) {
        h = FNV_MIX(h, clj_hash_full(c->first));
        CljObject *rest = c->rest;
        c = (rest && list_type_matches(TAG(rest))) ? as_list(rest) : NULL;
    }
    return h;
}

static uint32_t hash_string(CljString *str) {
    uint16_t len = str->length;
    if (len <= 16) return fnv1a(str->data);
    // O(1): length + first 8 + last 8 chars
    const char *d = str->data;
    uint32_t h = FNV_MIX(FNV_MIX(FNV1A_OFFSET, len), len >> 8);
    for (int i = 0; i < 8; i++) h = FNV_MIX(h, (uint8_t)d[i]);
    for (int i = len - 8; i < len; i++) h = FNV_MIX(h, (uint8_t)d[i]);
    return h;
}

uint32_t clj_hash_full(ID value) {
    if (!value) return 0;
    if (is_fixnum(value)) return (uint32_t)as_fixnum(value);
    if (is_bool(value)) return is_falsy(value) ? 0 : 1;
    if (is_character(value)) return (uint32_t)as_character(value);

    switch (TAG(value)) {
        case CLJ_STRING: return hash_string((CljString*)value);
        case CLJ_SYMBOL: return hash_symbol((CljSymbol*)value);
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: return hash_vector((CljVector*)value);
        case CLJ_MAP:
        case CLJ_MAP_TRANSIENT: return hash_map((CljMap*)value);
        case CLJ_LIST: return hash_list((CljList*)value);
        case CLJ_INSTANT: {
            CljInstant *inst = (CljInstant*)value;
            uint32_t h = FNV1A_OFFSET;
            h = FNV_MIX(h, (uint32_t)inst->days);
            h = FNV_MIX(h, (uint32_t)inst->ms);
            return h;
        }
        case CLJ_UUID: {
            CljUUID *u = (CljUUID*)value;
            if (u->hash == 0) {
                uint32_t h = FNV1A_OFFSET;
                for (int i = 0; i < 16; i++) {
                    h = FNV_MIX(h, u->bytes[i]);
                }
                u->hash = h;
            }
            return u->hash;
        }
        default: return (uint32_t)(uintptr_t)value;
    }
}

