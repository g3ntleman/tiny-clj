#include "hashmap.h"
#include "callbacks.h"   // For clj_hash() and clj_equal() - must be early
#include "kv_macros.h"
#include "object.h"
#include "strings.h"
#include "memory.h"
#include "value.h"  // For IS_IMMEDIATE
#include "exception.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>  // For UINT_MAX

// Sentinel singletons (SINGLETON_RC prevents RETAIN/RELEASE from modifying them)
CljObject g_hashmap_empty_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
CljObject g_hashmap_tombstone_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };

// Next power of 2 (for mask: capacity - 1)
static unsigned int next_power_of_2(unsigned int n) {
    if (n < 8) return 8;
    n--;
    n |= n >> 1; 
    n |= n >> 2; 
    n |= n >> 4;
    n |= n >> 8; 
    n |= n >> 16;
    return n + 1;
}

CljHashMap* make_hashmap(unsigned int capacity) {
    unsigned int cap = next_power_of_2(capacity);
    
    // Allocate struct + embedded data array in ONE malloc (like CljMap)
    size_t struct_size = sizeof(CljHashMap);
    size_t data_size = (size_t)cap * 2 * sizeof(CljObject*);
    size_t total_size = struct_size + data_size;
    
    CljHashMap *map = (CljHashMap*)ALLOC_BYTES(CLJ_HASHMAP, total_size);
    if (!map) {
        throw_oom();
    }
    
    map->base.type = CLJ_HASHMAP;
    map->base.rc = 1;
    map->count = 0;
    map->capacity = cap;
    map->tombstones = 0;
    
    // Initialize embedded array to HASHMAP_EMPTY (NULL is a valid key: nil)
    for (unsigned int i = 0; i < cap * 2; i++) {
        map->data[i] = HASHMAP_EMPTY;
    }
    
    return map;
}

// Linear Probing: Find slot (for get/put)
// Uses KV_KEY() macro for consistent access
// Now uses ID keys with clj_hash() and clj_equal()
// Note: HASHMAP_EMPTY marks empty slots, NULL is a valid key (nil)
static unsigned int find_slot(CljHashMap *map, ID key) {
    unsigned int mask = map->capacity - 1;  // 2^n - 1 for fast modulo
    unsigned int start_idx = clj_hash(key) & mask;   // Start index
    unsigned int idx = start_idx;
    
    // Linear Probing: on collision simply +1
    // IMPORTANT: Check for wraparound to avoid infinite loops
    do {
        ID stored_key = KV_KEY(map->data, idx);
        
        if (stored_key == HASHMAP_EMPTY) {
            return idx;  // Empty slot found
        }
        
        if (stored_key != HASHMAP_TOMBSTONE) {
            // Use clj_equal for comparison (supports all types)
            if (clj_equal(stored_key, key)) return idx;  // Found
        }
        
        // Collision or tombstone: Linear Probing
        idx = (idx + 1) & mask;  // Wraps around at 2^n
    } while (idx != start_idx);  // Stop when we're back at start
    
    // Map is full (all slots occupied or tombstones)
    // This should not happen if we rehash at Load > 0.75
    // But for safety: return start_idx (will cause error in put())
    return start_idx;
}

ID hashmap_get_sentinel(CljHashMap *map, ID key, ID not_found) {
    if (!map) return not_found;
    // key can be NULL (nil is a valid key)
    unsigned int idx = find_slot(map, key);
    ID stored_key = KV_KEY(map->data, idx);
    if (stored_key != HASHMAP_EMPTY && stored_key != HASHMAP_TOMBSTONE) {
        return KV_VALUE(map->data, idx);
    }
    return not_found;
}

int hashmap_contains(CljHashMap *map, ID key) {
    if (!map) return 0;
    // key can be NULL (nil is a valid key)
    unsigned int idx = find_slot(map, key);
    ID stored_key = KV_KEY(map->data, idx);
    return (stored_key != HASHMAP_EMPTY && stored_key != HASHMAP_TOMBSTONE) ? 1 : 0;
}

unsigned int hashmap_count(CljHashMap *map) {
    if (!map) return 0;
    return map->count;
}

// Insert key-value pair at given index
// Caller must handle tombstone accounting before calling
static void hashmap_insert_at(CljHashMap *map, unsigned int idx, ID key, ID value) {
    CLJ_ASSERT(KV_KEY(map->data, idx) != HASHMAP_TOMBSTONE);
    
    ASSIGN(KV_KEY(map->data, idx), key);
    ASSIGN(KV_VALUE(map->data, idx), value);
    map->count++;
}

// Direct insert without rehashing check (used for rehashing/copying)
static void hashmap_put_direct(CljHashMap *map, ID key, ID value) {
    unsigned int idx = find_slot(map, key);
    CLJ_ASSERT(KV_KEY(map->data, idx) == HASHMAP_EMPTY);
    hashmap_insert_at(map, idx, key, value);
}

// Rehashing: Copy all entries into a new map with larger capacity
static CljHashMap* hashmap_rehash(CljHashMap *map, unsigned int new_capacity) {
    CljHashMap *new_map = make_hashmap(new_capacity);
    
    ID key;
    ID val;
    HASHMAP_FOR_EACH(map, key, val) {
        hashmap_put_direct(new_map, key, val);
    }

    // Return owned (rc=1). Caller is responsible for adopting and releasing the old map.
    return new_map;
}

// Check if rehashing is needed (Load factor > 0.75)
static bool needs_rehash(CljHashMap *map) {
    if (!map) return false;
    // Load factor = (count + tombstones) / capacity
    // Rehash when > 0.75 (75% occupied)
    size_t total_used = (size_t)map->count + map->tombstones;
    return total_used * 4 > (size_t)map->capacity * 3;  // total_used / capacity > 0.75
}

// Copy hashmap (clean copy without tombstones)
static CljHashMap* hashmap_copy(CljHashMap *map) {
    CljHashMap *copy = make_hashmap(map->count);
    copy->count = 0;
    copy->tombstones = 0;
    
    ID key;
    ID val;
    HASHMAP_FOR_EACH(map, key, val) {
        hashmap_put_direct(copy, key, val);
    }
    return copy;
}

// COW implementation with Linear Probing and tombstone reuse
CljHashMap* hashmap_assoc(CljHashMap *map, ID key, ID value) {
    if (!map) return map;
    // key can be NULL (nil is a valid key)
    
    // Rehash if needed (before COW check)
    if (needs_rehash(map)) {
        map = hashmap_rehash(map, map->capacity * 2);
    }
    
    // COW: RC>1 → create copy
    // Note: We do NOT release the original map here - the caller is responsible
    // for managing the old reference (e.g., via adopt_hashmap helper)
    if (map->base.rc > 1) {
        CljHashMap *copy = hashmap_copy(map);
        map = copy;
    }
    
    // Linear Probing: Find slot (with tombstone reuse)
    unsigned int mask = map->capacity - 1;
    unsigned int idx = clj_hash(key) & mask;
    unsigned int start_idx = idx;
    
    do {
        ID stored_key = KV_KEY(map->data, idx);
        
        if (stored_key == HASHMAP_EMPTY) {
            // Empty slot found - insert here
            break;
        }
        
        if (stored_key != HASHMAP_TOMBSTONE && clj_equal(stored_key, key)) {
            // Key exists - update value
            ASSIGN(KV_VALUE(map->data, idx), value);
            return map;
        }
        
        // Linear Probing: skip tombstones and collisions
        idx = (idx + 1) & mask;
    } while (idx != start_idx);
    
    hashmap_insert_at(map, idx, key, value);
    
    return map;
}

// Remove with COW, Linear Probing over tombstones
CljHashMap* hashmap_remove(CljHashMap *map, ID key) {
    if (!map) return map;
    // key can be NULL (nil is a valid key)
    
    // Linear Probing: Find slot
    unsigned int idx = find_slot(map, key);
    ID stored = KV_KEY(map->data, idx);
    if (stored == HASHMAP_EMPTY || stored == HASHMAP_TOMBSTONE) {
        return map;  // Not found
    }
    
    // COW: RC>1 → Clean copy without this key
    if (map->base.rc > 1) {
        CljHashMap *copy = make_hashmap(map->count > 1 ? map->count - 1 : 0);
        for (unsigned int i = 0; i < map->capacity; i++) {
            ID stored_i = KV_KEY(map->data, i);
            if (stored_i != HASHMAP_EMPTY && stored_i != HASHMAP_TOMBSTONE && i != idx) {
                // hashmap_put_direct -> ASSIGN handles RETAIN
                hashmap_put_direct(copy, stored_i, KV_VALUE(map->data, i));
            }
        }
        return copy;
    }
    
    // RC=1: In-place tombstone (Linear Probing can jump over it)
    RELEASE(KV_KEY(map->data, idx));
    RELEASE(KV_VALUE(map->data, idx));
    KV_SET_KEY(map->data, idx, HASHMAP_TOMBSTONE);
    KV_SET_VALUE(map->data, idx, NULL);
    map->count--;
    map->tombstones++;
    
    return map;
}

void hashmap_assoc_inplace(CljHashMap **map_slot, ID key, ID value) {
    if (!map_slot || !*map_slot) return;
    CljHashMap *current = *map_slot;
    CljHashMap *updated = hashmap_assoc(current, key, value);
    if (updated && updated != current) {
        RELEASE(current);
        *map_slot = updated;
    }
}

void hashmap_remove_inplace(CljHashMap **map_slot, ID key) {
    if (!map_slot || !*map_slot) return;
    CljHashMap *current = *map_slot;
    CljHashMap *updated = hashmap_remove(current, key);
    if (updated && updated != current) {
        RELEASE(current);
        *map_slot = updated;
    }
}

// Memory management registration (no-op - destructor is in memory.c release_object_default)
void hashmap_register_release_fn(void) {
    // HashMap destructor is already implemented in memory.c release_object_default()
    // This function exists for API consistency with other types
}

