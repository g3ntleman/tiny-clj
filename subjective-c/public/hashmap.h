#ifndef SUBJECTIVE_C_HASHMAP_H
#define SUBJECTIVE_C_HASHMAP_H

#include "object.h"
#include "kv_macros.h"  // For KV_KEY, KV_VALUE, KV_ASSIGN_PAIR
#include <stdbool.h>

// Sentinel singletons (SINGLETON_RC makes RETAIN/RELEASE safe no-ops)
extern CljObject g_hashmap_empty_sentinel;
extern CljObject g_hashmap_tombstone_sentinel;
#define HASHMAP_EMPTY (&g_hashmap_empty_sentinel)
#define HASHMAP_TOMBSTONE (&g_hashmap_tombstone_sentinel)

typedef struct {
    CljObject base;
    unsigned int count;     // Active entries (never negative)
    unsigned int capacity; // Array size (must be 2^n for mask, never negative)
    size_t tombstones;      // Deleted slots (for Linear Probing)
    CljObject *data[];      // Embedded array: [key0, value0, key1, value1, ...]
                            // Size: capacity * 2 * sizeof(CljObject*)
                            // Access via KV_KEY() and KV_VALUE() macros
} CljHashMap;

// Factory
CljHashMap* make_hashmap(unsigned int capacity);

// Lookup - O(1) amortized with Linear Probing
ID hashmap_get(CljHashMap *map, ID key, ID not_found);
int hashmap_contains(CljHashMap *map, ID key);  // Returns int (like map_contains)

// Modification (COW) - returns new or same map
CljHashMap* hashmap_assoc(CljHashMap *map, ID key, ID value);  // COW (like map_assoc)
CljHashMap* hashmap_remove(CljHashMap *map, ID key);

// Utility
unsigned int hashmap_count(CljHashMap *map);  // Returns unsigned int (never negative)

// Memory management registration
void hashmap_register_release_fn(void);

// Iteration (skips EMPTY and TOMBSTONE)
#define HASHMAP_FOR_EACH(map, key_var, value_var) \
    for (unsigned int _hm_i = 0; (map) && _hm_i < (map)->capacity; _hm_i++) \
        if ((key_var = KV_KEY((map)->data, _hm_i)) != HASHMAP_EMPTY && key_var != HASHMAP_TOMBSTONE) \
            if ((value_var = KV_VALUE((map)->data, _hm_i)), 1)

#endif // SUBJECTIVE_C_HASHMAP_H

