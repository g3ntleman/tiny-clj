/*
 * Hash Function Implementation
 *
 * Vollständige Hash-Implementierung für alle Clojure-Typen.
 * Diese Funktion wird in runtime_init() als Callback für subjective-c registriert.
 */

#include "hash.h"
#include "symbol.h"
#include "vector.h"
#include "map.h"
#include "list.h"
#include "strings.h"
#include "value.h"
#include "object.h"
#include "kv_macros.h"
#include "subjective-c/public/callbacks.h"  // For clj_hash() wrapper
#include <string.h>

#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME  16777619u

// FNV-1a Hash (continue from given state, or use FNV1A_OFFSET to start fresh)
static uint32_t fnv1a_continue(uint32_t h, const char *s) {
    for (; *s; s++) { 
        h ^= (uint8_t)*s; 
        h *= FNV1A_PRIME; 
    }
    return h;
}

#define fnv1a(s) fnv1a_continue(FNV1A_OFFSET, (s))

// Hash for Symbol: combines namespace and name
static uint32_t hash_symbol(CljSymbol *sym) {
    if (!sym || !sym->cname) return 0;
    
    // Hash "ns/name" without copying
    if (sym->ns_name && sym->ns_name->cname) {
        uint32_t h = fnv1a(sym->ns_name->cname);
        h ^= '/';
        h *= 16777619u;
        return fnv1a_continue(h, sym->cname);
    } else {
        return fnv1a(sym->cname);
    }
}

// Hash for Vector: combines hashes of all elements
static uint32_t hash_vector(CljVector *vec) {
    if (!vec) return 0;
    uint32_t h = 0;
    int count = vector_count(vec);
    for (int i = 0; i < count; i++) {
        ID elem = vector_nth(vec, i);
        uint32_t elem_hash = clj_hash_full(elem);  // Recursive: clj_hash_full calls g_clj_hash_fn
        // Combine hashes (FNV-1a style)
        h ^= elem_hash;
        h *= 16777619u;
    }
    return h;
}

// Hash for Map: combines hashes of all key-value pairs
static uint32_t hash_map(CljMap *map) {
    if (!map) return 0;
    uint32_t h = 0;
    MAP_FOR_EACH(map, key, value) {
        uint32_t key_hash = clj_hash_full((ID)key);  // Rekursiv
        uint32_t val_hash = clj_hash_full(value);     // Rekursiv
        // Kombiniere Key- und Value-Hash
        h ^= key_hash;
        h *= 16777619u;
        h ^= val_hash;
        h *= 16777619u;
    }
    return h;
}

// Hash for List: combines hashes of all elements
static uint32_t hash_list(CljList *list) {
    if (!list) return 0;
    uint32_t h = 0;
    CljList *curr = list;
    while (curr) {
        uint32_t elem_hash = clj_hash_full((ID)curr->first);  // Rekursiv
        h ^= elem_hash;
        h *= 16777619u;
        CljObject *rest = curr->rest;
        if (!rest || !list_type_matches(TAG(rest))) {
            break;
        }
        curr = as_list(rest);
    }
    return h;
}

// Complete hash implementation for all types
uint32_t clj_hash_full(ID value) {
    if (!value) return 0;  // nil
    
    // Fixnum: Wert direkt
    if (is_fixnum(value)) {
        return (uint32_t)as_fixnum(value);
    }
    
    // Bool: 0 for false, 1 for true
    if (is_bool(value)) {
        return is_falsy(value) ? 0 : 1;
    }
    
    // Character: Codepoint als Hash
    if (is_character(value)) {
        return (uint32_t)as_character(value);
    }
    
    // Heap-Objekte
    CljType type = TAG(value);
    switch (type) {
        case CLJ_STRING: {
            CljString *str = (CljString*)value;
            uint16_t len = str->length;
            // Short strings: hash fully
            if (len <= 16) {
                return fnv1a(str->data);
            }
            // Long strings: O(1) hash using length + first 8 + last 8 chars
            uint32_t h = FNV1A_OFFSET;
            h ^= len;
            h *= FNV1A_PRIME;
            h ^= (len >> 8);
            h *= FNV1A_PRIME;
            const char *data = str->data;
            for (int i = 0; i < 8; i++) {
                h ^= (uint8_t)data[i];
                h *= FNV1A_PRIME;
            }
            for (int i = len - 8; i < len; i++) {
                h ^= (uint8_t)data[i];
                h *= FNV1A_PRIME;
            }
            return h;
        }
        
        case CLJ_SYMBOL: {
            return hash_symbol((CljSymbol*)value);
        }
        
        case CLJ_VECTOR:
        case CLJ_VECTOR_TRANSIENT:
        case CLJ_VECTOR_TRANSIENT_WEAK: {
            return hash_vector((CljVector*)value);
        }
        
        case CLJ_MAP:
        case CLJ_MAP_TRANSIENT: {
            return hash_map((CljMap*)value);
        }
        
        case CLJ_LIST: {
            return hash_list((CljList*)value);
        }
        
        // Referenz-Typen: Pointer als Hash (nicht ideal, aber konsistent)
        case CLJ_FUNC:
        case CLJ_CLOSURE:
        case CLJ_EXCEPTION:
        case CLJ_ATOM:
        case CLJ_NAMESPACE:
        case CLJ_RAW_MEMORY:
        case CLJ_BYTE_ARRAY:
        case CLJ_SEQ:
        case CLJ_AST_NODE:
        case CLJ_CALLSITE_CACHE:
        case CLJ_SYMBOL_TOKEN:
        case CLJ_HASHMAP:
            return (uint32_t)(uintptr_t)value;
        
        default:
            // Unbekannter Typ: Pointer als Hash
            return (uint32_t)(uintptr_t)value;
    }
}

