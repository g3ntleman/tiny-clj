#include "callbacks.h"
#include "value.h"
#include "symbol.h"
#include "strings.h"
#include "vector.h"
#include "map.h"
#include "byte_array.h"
#include "hashmap.h"
#include "hashset.h"
#include "kv_macros.h"
#include "exception.h"
#include <string.h>
#include "mini_format.h"

// FNV-1a hash for strings
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) { 
        h ^= (uint8_t)*s; 
        h *= 16777619u; 
    }
    return h;
}

static uint32_t fnv1a_bytes(const uint8_t *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

// Combine count and element hash for container types
static inline uint32_t hash_container(uint32_t count, uint32_t elem_hash) {
    uint32_t h = 2166136261u;
    h ^= count;
    h *= 16777619u;
    h ^= elem_hash;
    h *= 16777619u;
    return h;
}

// Default hash function for all subjective-c types
uint32_t clj_hash_default(ID value) {
    // Fast path: nil
    if (!value) return 0;
    
    // Fast path: fixnum (most common case)
    if (is_fixnum(value)) return (uint32_t)as_fixnum(value);
    
    // Immediate types (char, bool) - use value directly
    if ((uintptr_t)value & 1) return (uint32_t)(uintptr_t)value;
    
    // Heap object - dispatch by type
    CljType type = TAG(value);
    
    switch (type) {
        case CLJ_STRING: {
            const char *data = string_data(value);
            uint16_t len = string_length(value);
            return fnv1a_bytes((const uint8_t*)data, (size_t)len);
        }
        
        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT: {
            CljPersistentVector *vec =
                (type == CLJ_VECTOR_TRANSIENT)
                    ? vector_persistent(as_transient_vector(value))
                    : as_persistent_vector(value);
            unsigned int count = vector_count(vec);
            uint32_t elem_hash = count > 0 ? clj_hash_default(vector_nth(vec, 0)) : 0;
            return hash_container(count, elem_hash);
        }
        
        case CLJ_MAP_PERSISTENT:
        case CLJ_MAP_TRANSIENT: {
            CljPersistentMap *map = map_backing(value);
            int count = map ? map_count(map) : 0;
            uint32_t key_hash = 0;
            if (map && count > 0) {
                for (int i = 0; i < map->capacity; i++) {
                    ID key = KV_KEY(map->data, i);
                    if (key) {
                        key_hash = clj_hash_default(key);
                        break;
                    }
                }
            }
            return hash_container((uint32_t)count, key_hash);
        }
        
        case CLJ_HASHMAP: {
            CljHashMap *hm = (CljHashMap*)value;
            unsigned int count = hashmap_count(hm);
            uint32_t key_hash = 0;
            if (count > 0) {
                for (unsigned int i = 0; i < hm->capacity; i++) {
                    ID key = KV_KEY(hm->data, i);
                    if (key != HASHMAP_EMPTY && key != HASHMAP_TOMBSTONE) {
                        key_hash = clj_hash_default(key);
                        break;
                    }
                }
            }
            return hash_container(count, key_hash);
        }

        case CLJ_HASHSET: {
            CljHashSet *hs = (CljHashSet*)value;
            unsigned int count = hashset_count(hs);
            uint32_t key_hash = 0;
            if (count > 0) {
                for (unsigned int i = 0; i < hs->capacity; i++) {
                    ID key = hs->data[i];
                    if (key != HASHSET_EMPTY && key != HASHSET_TOMBSTONE) {
                        key_hash = clj_hash_default(key);
                        break;
                    }
                }
            }
            return hash_container(count, key_hash);
        }
        
        case CLJ_BYTE_ARRAY: {
            CljByteArray *arr = (CljByteArray*)value;
            uint32_t first_byte = arr->length > 0 ? arr->data[0] : 0;
            return hash_container((uint32_t)arr->length, first_byte);
        }

        case CLJ_SYMBOL: {
            // Symbols are equal by (ns_name, cname) content, not identity.
            // This keeps map/hashmap semantics correct even if symbols are not perfectly interned.
            const CljSymbol *s = (const CljSymbol*)value;
            const char *name = (s && s->cname) ? s->cname : "";
            const char *ns = NULL;
            if (s && s->ns_name) {
                const CljSymbol *nss = (const CljSymbol*)s->ns_name;
                if (nss && nss->cname && nss->cname[0] != '\0') ns = nss->cname;
            }
            if (!ns) {
                return fnv1a(name);
            }
            // Hash "ns/name" without allocating.
            uint32_t h = fnv1a(ns);
            h ^= (uint8_t)'/'; h *= 16777619u;
            for (const char *p = name; *p; p++) {
                h ^= (uint8_t)*p;
                h *= 16777619u;
            }
            return h;
        }
        
        default:
            // Identity-based types: use pointer
            return (uint32_t)(uintptr_t)value;
    }
}

// Default equal function for all subjective-c types
bool clj_equal_default(ID a, ID b) {
    // Fast path: identical pointers
    if (a == b) return true;
    
    // Reject if either is nil or immediate
    uintptr_t pa = (uintptr_t)a;
    uintptr_t pb = (uintptr_t)b;
    if ((pa == 0) | (pb == 0) | (pa & 1) | (pb & 1)) return false;
    
    // Both are heap objects - compare types
    CljType tag = TAG(a);
    if (tag != TAG(b)) return false;
    
    switch (tag) {
        case CLJ_STRING: {
            uint16_t len_a = string_length(a);
            uint16_t len_b = string_length(b);
            if (len_a != len_b) return false;
            if (len_a == 0) return true;
            const char *data_a = string_data(a);
            const char *data_b = string_data(b);
            return memcmp(data_a, data_b, len_a) == 0;
        }
        
        case CLJ_VECTOR_PERSISTENT:
        case CLJ_VECTOR_TRANSIENT: {
            CljPersistentVector *vec_a =
                (tag == CLJ_VECTOR_TRANSIENT)
                    ? vector_persistent(as_transient_vector(a))
                    : as_persistent_vector(a);
            CljPersistentVector *vec_b =
                (tag == CLJ_VECTOR_TRANSIENT)
                    ? vector_persistent(as_transient_vector(b))
                    : as_persistent_vector(b);
            unsigned int count_a = vector_count(vec_a);
            if (count_a != vector_count(vec_b)) return false;
            for (unsigned int i = 0; i < count_a; i++) {
                if (!clj_equal_default(vector_nth(vec_a, i), vector_nth(vec_b, i))) {
                    return false;
                }
            }
            return true;
        }
        
        case CLJ_MAP_PERSISTENT:
        case CLJ_MAP_TRANSIENT: {
            CljPersistentMap *map_a = map_backing(a);
            CljPersistentMap *map_b = map_backing(b);
            if (!map_a || !map_b) return false;
            if (map_count(map_a) != map_count(map_b)) return false;
            for (int i = 0; i < map_a->capacity; i++) {
                ID key = KV_KEY(map_a->data, i);
                if (key) {
                    ID val_a = KV_VALUE(map_a->data, i);
                    ID val_b = map_get(map_b, key);
                    if (val_b == NOT_FOUND) return false;
                    if (!clj_equal_default(val_a, val_b)) return false;
                }
            }
            return true;
        }
        
        case CLJ_BYTE_ARRAY: {
            CljByteArray *arr_a = (CljByteArray*)a;
            CljByteArray *arr_b = (CljByteArray*)b;
            if (arr_a->length != arr_b->length) return false;
            return memcmp(arr_a->data, arr_b->data, arr_a->length) == 0;
        }
        
        case CLJ_HASHMAP: {
            CljHashMap *hm_a = (CljHashMap*)a;
            CljHashMap *hm_b = (CljHashMap*)b;
            if (hashmap_count(hm_a) != hashmap_count(hm_b)) return false;
            ID key;
            ID val_a;
            HASHMAP_FOR_EACH(hm_a, key, val_a) {
                ID val_b = hashmap_get(hm_b, key);
                if (val_b == NOT_FOUND) return false;
                if (!clj_equal(val_a, val_b)) return false;
            }
            return true;
        }

        case CLJ_HASHSET: {
            CljHashSet *hs_a = (CljHashSet*)a;
            CljHashSet *hs_b = (CljHashSet*)b;
            if (hashset_count(hs_a) != hashset_count(hs_b)) return false;
            ID key;
            HASHSET_FOR_EACH(hs_a, key) {
                if (!hashset_contains(hs_b, key)) return false;
            }
            return true;
        }

        case CLJ_SYMBOL: {
            const CljSymbol *sa = (const CljSymbol*)a;
            const CljSymbol *sb = (const CljSymbol*)b;
            if (!sa || !sb) return false;
            if (sa->cname == sb->cname) {
                // Fast path: names share pointer (common for interned symbols)
            } else {
                if (!sa->cname || !sb->cname) return false;
                if (strcmp(sa->cname, sb->cname) != 0) return false;
            }

            const CljSymbol *nsa = (const CljSymbol*)sa->ns_name;
            const CljSymbol *nsb = (const CljSymbol*)sb->ns_name;
            if (nsa == nsb) return true; // includes both NULL
            if (!nsa || !nsb) return false;
            if (!nsa->cname || !nsb->cname) return false;
            return strcmp(nsa->cname, nsb->cname) == 0;
        }
        
        case CLJ_EXCEPTION:
            return false;  // Identity only
        
        default:
            CLJ_ASSERT(false && "clj_equal_default: unknown type");
            return false;
    }
}

// Default to_string: minimal for subjective-c
CljString* clj_to_string_default(ID value) {
    if (!value) return make_string("nil");
    
    if (is_fixnum(value)) {
        char buf[32];
        mini_snprintf(buf, sizeof(buf), "%d", as_fixnum(value));
        return make_string(buf);
    }
    
    if (is_immediate(value)) {
        if (value == clj_true) return make_string("true");
        if (value == clj_false) return make_string("false");
        return make_string("#<immediate>");
    }
    
    CljType type = TAG(value);
    switch (type) {
        case CLJ_STRING: {
            uint16_t len = string_length(value);
            const char *data = string_data(value);
            CljString *s = make_string_buffer(len);
            if (!s) return NULL;
            memcpy(s->data, data, len);
            s->data[len] = '\0';
            return s;
        }
        case CLJ_VECTOR_PERSISTENT:
            return make_string("[...]");
        case CLJ_MAP_PERSISTENT:
        case CLJ_HASHMAP:
            return make_string("{...}");
        case CLJ_EXCEPTION:
            return make_string("#<exception>");
        case CLJ_BYTE_ARRAY:
            return make_string("#<byte-array>");
        default:
            return make_string("#<unknown>");
    }
}

// Global callback struct - initialized with defaults
CljCallbacks g_clj_callbacks = {
    .hash = clj_hash_default,
    .equal = clj_equal_default,
    .to_string = clj_to_string_default
};

// Wrapper functions
uint32_t clj_hash(ID value) {
    return g_clj_callbacks.hash(value);
}

bool clj_equal(ID a, ID b) {
    return g_clj_callbacks.equal(a, b);
}

CljString* clj_to_string(ID value) {
    return g_clj_callbacks.to_string(value);
}

// Setter for all callbacks
void clj_set_callbacks(CljCallbacks callbacks) {
    if (callbacks.hash) g_clj_callbacks.hash = callbacks.hash;
    if (callbacks.equal) g_clj_callbacks.equal = callbacks.equal;
    if (callbacks.to_string) g_clj_callbacks.to_string = callbacks.to_string;
}

// Legacy individual setters
void clj_set_hash_fn(CljHashFn fn) {
    if (fn) g_clj_callbacks.hash = fn;
}

void clj_set_equal_fn(CljEqualFn fn) {
    if (fn) g_clj_callbacks.equal = fn;
}

void clj_set_to_string_fn(CljToStringFn fn) {
    if (fn) g_clj_callbacks.to_string = fn;
}
