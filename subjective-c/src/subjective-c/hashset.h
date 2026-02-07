#ifndef SUBJECTIVE_C_HASHSET_H
#define SUBJECTIVE_C_HASHSET_H

#include "object.h"
#include <stdbool.h>

// Sentinel singletons (SINGLETON_RC makes RETAIN/RELEASE safe no-ops)
extern CljObject g_hashset_empty_sentinel;
extern CljObject g_hashset_tombstone_sentinel;
#define HASHSET_EMPTY (&g_hashset_empty_sentinel)
#define HASHSET_TOMBSTONE (&g_hashset_tombstone_sentinel)

typedef struct {
    CljObject base;
    unsigned int count;     // Active entries (never negative)
    unsigned int capacity;  // Array size (must be 2^n for mask, never negative)
    size_t tombstones;      // Deleted slots (for Linear Probing)
    CljObject *data[];      // Embedded array: [key0, key1, ...]
                            // Size: capacity * sizeof(CljObject*)
} CljHashSet;

/** @brief Create hashset with specified capacity
 * @param capacity Initial capacity (must be power of 2)
 * @return New hashset
 */
CljHashSet* make_hashset(unsigned int capacity);

/** @brief Get key with custom not-found sentinel
 * @param set Hashset to query
 * @param key Key to lookup
 * @param not_found Value to return if not found
 * @return Stored key or not_found
 */
ID hashset_get_sentinel(CljHashSet *set, ID key, ID not_found);

static inline ID hashset_get(CljHashSet *set, ID key) {
    return hashset_get_sentinel(set, key, NOT_FOUND);
}

/** @brief Check if hashset contains key
 * @param set Hashset to query
 * @param key Key to check
 * @return Non-zero if key exists
 */
int hashset_contains(CljHashSet *set, ID key);

/** @brief Add key (COW, returns new/same set)
 * @param set Source hashset
 * @param key Key to add
 * @return New or same hashset (AUTORELEASE'd)
 */
CljHashSet* hashset_add(CljHashSet *set, ID key);

/** @brief Remove key (COW, returns new/same set)
 * @param set Source hashset
 * @param key Key to remove
 * @return New or same hashset (AUTORELEASE'd)
 */
CljHashSet* hashset_remove(CljHashSet *set, ID key);

/** @brief Add key in-place (updates slot)
 * @param set_slot Pointer to hashset slot
 * @param key Key to add
 */
void hashset_add_inplace(CljHashSet **set_slot, ID key);

/** @brief Remove key in-place (updates slot)
 * @param set_slot Pointer to hashset slot
 * @param key Key to remove
 */
void hashset_remove_inplace(CljHashSet **set_slot, ID key);

/** @brief Get number of entries
 * @param set Hashset to count
 * @return Number of entries
 */
unsigned int hashset_count(CljHashSet *set);

/** @brief Register hashset release function
 */
void hashset_register_release_fn(void);

// Iteration (skips EMPTY and TOMBSTONE)
#define HASHSET_FOR_EACH(set, key_var) \
    for (unsigned int _hs_i = 0; (set) && _hs_i < (set)->capacity; _hs_i++) \
        if ((key_var = (set)->data[_hs_i]) != HASHSET_EMPTY && key_var != HASHSET_TOMBSTONE)

#endif // SUBJECTIVE_C_HASHSET_H
