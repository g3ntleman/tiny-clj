#ifndef SUBJECTIVE_C_MAP_H
#define SUBJECTIVE_C_MAP_H

#include <stdbool.h>
#include "object.h"
#include "common.h"

typedef struct CljPersistentMap {
    CljObject base;
    int count;
    int capacity;
    CljObject *data[];
} CljPersistentMap;

typedef struct CljTransientMap {
    CljObject base;                 // type == CLJ_MAP_TRANSIENT
    CljPersistentMap *backing;      // always CLJ_MAP_PERSISTENT
} CljTransientMap;

// Type predicate - O(1) check if object is a map
static inline bool is_persistent_map(ID obj) {
    return obj && TAG(obj) == CLJ_MAP_PERSISTENT;
}

static inline bool is_transient_map(ID obj) {
    return obj && TAG(obj) == CLJ_MAP_TRANSIENT;
}

static inline bool is_map(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT;
}

// Type-safe casting
static inline CljPersistentMap* as_persistent_map(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || TAG(obj) == CLJ_MAP_PERSISTENT);
#endif
    return (CljPersistentMap*)obj;
}

static inline CljTransientMap* as_transient_map(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || TAG(obj) == CLJ_MAP_TRANSIENT);
#endif
    return (CljTransientMap*)obj;
}

static inline CljPersistentMap* map_backing(ID obj) {
    if (!obj) return NULL;
    CljType tag = TAG(obj);
    if (tag == CLJ_MAP_PERSISTENT) return (CljPersistentMap*)obj;
    if (tag == CLJ_MAP_TRANSIENT) {
        CljTransientMap *tmap = (CljTransientMap*)obj;
        return tmap ? tmap->backing : NULL;
    }
    return NULL;
}

static inline CljPersistentMap* as_map(ID obj) {
    return map_backing(obj);
}

/** @brief Create new map with specified capacity and retention
 * @param capacity Initial capacity
 * @param retention Element retention mode (STRONG/WEAK)
 * @return New map
 */
CljPersistentMap* make_map_impl(int capacity, ElementRetention retention);
#define MAKE_MAP_GET_MACRO(_1, _2, NAME, ...) NAME
#define make_map1(capacity) make_map_impl((capacity), STRONG)
#define make_map2(capacity, retention) make_map_impl((capacity), (retention))
#define make_map(...) MAKE_MAP_GET_MACRO(__VA_ARGS__, make_map2, make_map1)(__VA_ARGS__)
static inline CljPersistentMap* map_empty(void) {
    extern CljPersistentMap *map_empty_singleton;
    return map_empty_singleton;
}
/** @brief Get value for key with custom not-found value
 * @param map Map to query
 * @param key Key to lookup
 * @param not_found Value to return if key not found
 * @return Value for key or not_found
 */
ID map_get_sentinel(ID map, ID key, ID not_found);

static inline ID map_get(ID map, ID key) {
    return map_get_sentinel(map, key, NOT_FOUND);
}

/** @brief Associate key-value pair (persistent, returns new map)
 * @param map Source map
 * @param key Key to associate
 * @param value Value to associate
 * @return New map with association (AUTORELEASE'd)
 */
CljPersistentMap* map_assoc(CljPersistentMap* map, ID key, ID value);

/** @brief Merge two maps
 * @param a First map
 * @param b Second map
 * @param overwrite If true, b's values override a's
 * @return Merged map (AUTORELEASE'd)
 */
CljPersistentMap* map_merge(CljPersistentMap* a, CljPersistentMap* b, bool overwrite);

/** @brief Get vector of all keys
 * @param map Map to query
 * @return Vector of keys (AUTORELEASE'd)
 */
ID map_keys(ID map);

/** @brief Get vector of all values
 * @param map Map to query
 * @return Vector of values (AUTORELEASE'd)
 */
ID map_vals(ID map);

/** @brief Get number of key-value pairs
 * @param map Map to count
 * @return Number of entries
 */
int map_count(ID map);

/** @brief Put key-value pair in-place (mutates map)
 * @param map Map to modify
 * @param key Key to put
 * @param value Value to put
 */
void map_put(CljPersistentMap *map, ID key, ID value);

/** @brief Iterate over all key-value pairs
 * @param map Map to iterate
 * @param func Callback function (key, value)
 */
void map_foreach(ID map, void (*func)(ID, ID));

/** @brief Check if map contains key
 * @param map Map to query
 * @param key Key to check
 * @return Non-zero if key exists
 */
int map_contains(ID map, ID key);

/** @brief Remove key (persistent, returns new map)
 * @param map Source map
 * @param key Key to remove
 * @return New map without key (AUTORELEASE'd)
 */
CljPersistentMap* map_remove(CljPersistentMap *map, ID key);

// Compatibility helper for legacy call sites.
static inline CljPersistentMap* map_by_associng_kv(CljPersistentMap *map, ID key, ID value) {
    return map_assoc(map, key, value);
}

/** @brief Associate key-value in-place (updates slot)
 * @param map_slot Pointer to map slot
 * @param key Key to associate
 * @param value Value to associate
 */
void map_assoc_inplace(CljPersistentMap **map_slot, ID key, ID value);

/** @brief Remove key in-place (updates slot)
 * @param map_slot Pointer to map slot
 * @param key Key to remove
 */
void map_remove_inplace(CljPersistentMap **map_slot, ID key);

/** @brief Create transient map from varargs key-value pairs
 * @param count Number of key-value pairs
 * @return New transient map
 */
CljTransientMap* make_transient_map_from_kv(unsigned int count, ...);

/** @brief Create map from varargs key-value pairs
 * @param first_key First key (NOT_FOUND terminated)
 * @return New map (AUTORELEASE'd)
 */
CljPersistentMap* make_map_kv(ID first_key, ...);

/** @brief Create map from array of key-value pairs
 * @param pairs Array of alternating keys and values
 * @param pair_count Number of pairs (array length / 2)
 * @return New map
 */
CljPersistentMap* make_map_from_stack(CljObject **pairs, int pair_count);

/** @brief Create map copy with additional entries
 * @param parent_map Base map
 * @param additions Array of alternating keys and values
 * @param addition_count Number of additions (array length / 2)
 * @return New map with additions
 */
CljPersistentMap* map_copy_with_additions(CljPersistentMap *parent_map, CljObject **additions, int addition_count);

/** @brief Create transient wrapper for efficient mutations
 * @param map Source map
 * @return New transient map (AUTORELEASE'd)
 */
CljTransientMap* map_transient(CljPersistentMap *map);

/** @brief Add key-value to transient map (in-place)
 * @param tmap Transient map
 * @param key Key to add
 * @param value Value to add
 */
void map_conj(CljTransientMap *tmap, ID key, ID value);

/** @brief Remove key from transient map (in-place)
 * @param tmap Transient map
 * @param key Key to remove
 */
void map_dissoc(CljTransientMap *tmap, ID key);

/** @brief Convert transient map to persistent
 * @param tmap Transient map
 * @return Persistent map (borrowed reference)
 */
CljPersistentMap* map_persistent(CljTransientMap *tmap);

#define MAP_FOR_EACH(map, key_var, value_var) \
    for (CljPersistentMap *_map = map_backing((ID)(map)); _map; _map = NULL) \
        for (int _i = 0, _cnt = _map->count; _i < _cnt; ++_i) \
            for (CljObject **_data_ptr = _map->data; _data_ptr; _data_ptr = NULL) \
                for (CljObject *key_var __attribute__((unused)) = _data_ptr[2*_i], *value_var __attribute__((unused)) = _data_ptr[2*_i+1]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_MAP_H
