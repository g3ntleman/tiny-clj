#include "hashmap.h"
#include "public/callbacks.h"   // For clj_hash() and clj_equal() - must be early
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
    
    CljHashMap *map = (CljHashMap*)malloc(total_size);
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
        ID stored_key = (ID)KV_KEY(map->data, idx);
        
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

ID hashmap_get(CljHashMap *map, ID key, ID not_found) {
    if (!map) return not_found;
    // key can be NULL (nil is a valid key)
    unsigned int idx = find_slot(map, key);
    ID stored_key = (ID)KV_KEY(map->data, idx);
    if (stored_key != HASHMAP_EMPTY && stored_key != HASHMAP_TOMBSTONE) {
        return KV_VALUE(map->data, idx);
    }
    return not_found;
}

int hashmap_contains(CljHashMap *map, ID key) {
    if (!map) return 0;
    // key can be NULL (nil is a valid key)
    unsigned int idx = find_slot(map, key);
    ID stored_key = (ID)KV_KEY(map->data, idx);
    return (stored_key != HASHMAP_EMPTY && stored_key != HASHMAP_TOMBSTONE) ? 1 : 0;
}

unsigned int hashmap_count(CljHashMap *map) {
    if (!map) return 0;
    return map->count;
}

// Insert key-value pair at given index (common logic for put operations)
// key_str is actually ID (cast for compatibility with existing code)
static void hashmap_insert_at(CljHashMap *map, unsigned int idx, CljString *key_str, ID value) {
    // Reuse tombstone slot if present
    if (KV_KEY(map->data, idx) == HASHMAP_TOMBSTONE) {
        map->tombstones--;
    }
    
    // key_str is already retained by caller
    ASSIGN(KV_KEY(map->data, idx), (ID)key_str);
    ASSIGN(KV_VALUE(map->data, idx), value);
    map->count++;
}

// Direct insert without rehashing check (used for rehashing)
static void hashmap_put_direct(CljHashMap *map, ID key, ID value) {
    unsigned int idx = find_slot(map, key);
    
    // Key should not exist (we're copying from old map)
    CLJ_ASSERT(KV_KEY(map->data, idx) == HASHMAP_EMPTY || KV_KEY(map->data, idx) == HASHMAP_TOMBSTONE);
    
    // Key is already an ID, retain it
    RETAIN(key);
    hashmap_insert_at(map, idx, (CljString*)key, value);  // Cast for compatibility, but key is ID
}

// Rehashing: Copy all entries into a new map with larger capacity
// ADVANTAGE: Clean copy without tombstones
static CljHashMap* hashmap_rehash(CljHashMap *map, unsigned int new_capacity) {
    CljHashMap *new_map = make_hashmap(new_capacity);
    
    // Insert all entries directly (without rehashing check, without tombstones)
    for (unsigned int i = 0; i < map->capacity; i++) {
        ID stored = (ID)KV_KEY(map->data, i);
        if (stored != HASHMAP_EMPTY && stored != HASHMAP_TOMBSTONE) {
            ID k = stored;
            ID v = KV_VALUE(map->data, i);
            // RETAIN key and value because we're copying them to new map
            RETAIN(k);
            RETAIN(v);
            hashmap_put_direct(new_map, k, v);
        }
    }
    
    // Free old map if RC=1
    if (map->base.rc == 1) {
        for (unsigned int i = 0; i < map->capacity; i++) {
            ID slot = (ID)KV_KEY(map->data, i);
            if (slot != HASHMAP_EMPTY && slot != HASHMAP_TOMBSTONE) {
                RELEASE(KV_KEY(map->data, i));
                RELEASE(KV_VALUE(map->data, i));
            }
        }
        free(map);  // Embedded array is automatically freed
    } else {
        // RC > 1: Old map remains (COW)
        // But we don't need to free references, as they're still in use
    }
    
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
// ADVANTAGE: Clean copy without tombstones
// - Better performance: Fewer slots in Linear Probing
// - Less memory: Tombstones take up space
// - Better load factor: (count + tombstones) → count
// - Less clustering: Tombstones can cause longer probe sequences
// - Automatic rehashing: New map can have optimal capacity
static CljHashMap* hashmap_copy(CljHashMap *map) {
    CljHashMap *copy = make_hashmap(map->count);  // Optimal capacity based on count
    copy->count = 0;  // Will be incremented during insertion
    copy->tombstones = 0;  // Clean, no tombstones
    
    // Re-insert all entries (automatic rehashing)
    // Uses KV_KEY() and KV_VALUE() macros
    for (unsigned int i = 0; i < map->capacity; i++) {
        ID stored = (ID)KV_KEY(map->data, i);
        if (stored != HASHMAP_EMPTY && stored != HASHMAP_TOMBSTONE) {
            ID k = stored;
            ID v = KV_VALUE(map->data, i);
            // RETAIN key and value because we're copying them to new map
            RETAIN(k);
            RETAIN(v);
            hashmap_put_direct(copy, k, v);
        }
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
    unsigned int first_tombstone = UINT_MAX;
    
    do {
        ID stored_key = (ID)KV_KEY(map->data, idx);
        
        if (stored_key == HASHMAP_EMPTY) {
            // Empty slot found
            break;
        }
        
        if (stored_key == HASHMAP_TOMBSTONE) {
            // Tombstone found - remember for reuse
            if (first_tombstone == UINT_MAX) first_tombstone = idx;
        } else {
            if (clj_equal(stored_key, key)) {
                // Key exists - update
                ASSIGN(KV_VALUE(map->data, idx), value);
                return map;
            }
        }
        // Linear Probing: next slot
        idx = (idx + 1) & mask;
    } while (idx != start_idx);
    
    // New entry: reuse tombstone or use empty slot
    unsigned int insert_idx = (first_tombstone != UINT_MAX) ? first_tombstone : idx;
    if (first_tombstone != UINT_MAX) map->tombstones--;
    
    // Retain key (it's already an ID)
    RETAIN(key);
    hashmap_insert_at(map, insert_idx, (CljString*)key, value);  // Cast for compatibility
    
    return map;
}

// Remove with COW, Linear Probing over tombstones
CljHashMap* hashmap_remove(CljHashMap *map, ID key) {
    if (!map) return map;
    // key can be NULL (nil is a valid key)
    
    // Linear Probing: Find slot
    unsigned int idx = find_slot(map, key);
    ID stored = (ID)KV_KEY(map->data, idx);
    if (stored == HASHMAP_EMPTY || stored == HASHMAP_TOMBSTONE) {
        return map;  // Not found
    }
    
    // COW: RC>1 → Clean copy without this key (no tombstones!)
    // Note: We do NOT release the original map here - the caller is responsible
    if (map->base.rc > 1) {
        // ADVANTAGE: New map without tombstones, optimal capacity
        CljHashMap *copy = make_hashmap(map->count > 1 ? map->count - 1 : 0);  // count-1 since one key is removed
        for (unsigned int i = 0; i < map->capacity; i++) {
            ID stored_i = (ID)KV_KEY(map->data, i);
            if (stored_i != HASHMAP_EMPTY && stored_i != HASHMAP_TOMBSTONE && i != idx) {
                ID k = stored_i;
                ID v = KV_VALUE(map->data, i);
                // RETAIN key and value because we're copying them to new map
                RETAIN(k);
                RETAIN(v);
                hashmap_put_direct(copy, k, v);
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

// Memory management registration (no-op - destructor is in memory.c release_object_default)
void hashmap_register_release_fn(void) {
    // HashMap destructor is already implemented in memory.c release_object_default()
    // This function exists for API consistency with other types
}

