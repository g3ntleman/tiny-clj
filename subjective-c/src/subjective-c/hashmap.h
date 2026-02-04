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

/** @brief Create hashmap with specified capacity
 * @param capacity Initial capacity (must be power of 2)
 * @return New hashmap
 */
CljHashMap* make_hashmap(unsigned int capacity);

/** @brief Get value with custom not-found sentinel
 * @param map Hashmap to query
 * @param key Key to lookup
 * @param not_found Value to return if not found
 * @return Value or not_found
 */
ID hashmap_get_sentinel(CljHashMap *map, ID key, ID not_found);

static inline ID hashmap_get(CljHashMap *map, ID key) {
    return hashmap_get_sentinel(map, key, NOT_FOUND);
}

/** @brief Check if hashmap contains key
 * @param map Hashmap to query
 * @param key Key to check
 * @return Non-zero if key exists
 */
int hashmap_contains(CljHashMap *map, ID key);

/** @brief Associate key-value (COW, returns new/same map)
 * @param map Source hashmap
 * @param key Key to associate
 * @param value Value to associate
 * @return New or same hashmap (AUTORELEASE'd)
 */
CljHashMap* hashmap_assoc(CljHashMap *map, ID key, ID value);

/** @brief Remove key (COW, returns new/same map)
 * @param map Source hashmap
 * @param key Key to remove
 * @return New or same hashmap (AUTORELEASE'd)
 */
CljHashMap* hashmap_remove(CljHashMap *map, ID key);

/** @brief Associate key-value in-place (updates slot)
 * @param map_slot Pointer to hashmap slot
 * @param key Key to associate
 * @param value Value to associate
 */
void hashmap_assoc_inplace(CljHashMap **map_slot, ID key, ID value);

/** @brief Remove key in-place (updates slot)
 * @param map_slot Pointer to hashmap slot
 * @param key Key to remove
 */
void hashmap_remove_inplace(CljHashMap **map_slot, ID key);

/** @brief Get number of entries
 * @param map Hashmap to count
 * @return Number of entries
 */
unsigned int hashmap_count(CljHashMap *map);

/** @brief Register hashmap release function
 */
void hashmap_register_release_fn(void);

// Iteration (skips EMPTY and TOMBSTONE)
#define HASHMAP_FOR_EACH(map, key_var, value_var) \
    for (unsigned int _hm_i = 0; (map) && _hm_i < (map)->capacity; _hm_i++) \
        if ((key_var = KV_KEY((map)->data, _hm_i)) != HASHMAP_EMPTY && key_var != HASHMAP_TOMBSTONE) \
            if ((value_var = KV_VALUE((map)->data, _hm_i)), 1)

#endif // SUBJECTIVE_C_HASHMAP_H

