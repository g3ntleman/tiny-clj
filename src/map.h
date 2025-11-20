#ifndef TINY_CLJ_MAP_H
#define TINY_CLJ_MAP_H

#include "object.h"
#include "common.h"  // For CLJ_ASSERT
// Forward declaration for IS_IMMEDIATE macro (defined in value.h)
// We can't include value.h here due to circular dependency: map.h -> value.h -> namespace.h -> map.h
// map.c includes value.h where IS_IMMEDIATE is actually needed

// CljMap struct definition
typedef struct {
    CljObject base;
    int count;
    int capacity;
    CljObject *data[];
} CljMap;

// Type-safe casting
static inline CljMap* as_map(ID obj) {
    // Happy path: obj is not NULL and has correct type
    if (obj) {
        int tag = TAG(obj);
        if (tag == CLJ_MAP || tag == CLJ_MAP_TRANSIENT) {
            return (CljMap*)obj;  // Direct return, no jumps
        }
    }
    CLJ_ASSERT(0 && "Expected Map type");
    return NULL;  // Never reached in DEBUG, but needed for Release builds
}

// Map operations (optimized with pointer fast paths)
/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljMap* make_map(int capacity);
/** Return the empty map singleton (inline for performance). */
static inline CljMap* map_empty(void) {
    extern CljMap *map_empty_singleton;
    return map_empty_singleton;
}
/** Get value for key or not_found if absent (structural key equality).
 * If not_found is NULL, returns NULL when key is absent (backward compatible).
 * If not_found is provided, returns not_found when key is absent.
 * Returns the value (which may be NULL/nil) when key exists.
 */
ID map_get(CljMap *map, ID key, ID not_found);

/** Associate key->value with Copy-on-Write - returns same or new map depending on RC. */
CljMap* map_assoc(CljMap* map, ID key, ID value);
/** Return a vector of keys (retained). */
ID map_keys(CljMap *map);
/** Return a vector of values (retained). */
ID map_vals(CljMap *map);
/** Return number of key/value pairs. */
int map_count(CljMap *map);
/** Append key/value without structural duplicate check (retains both). */
void map_put(CljMap *map, ID key, ID value);
/** Iterate over all key/value pairs calling func(key,value). */
void map_foreach(CljMap *map, void (*func)(ID, ID));
/** Return 1 if key exists (pointer equality fast-path). */
int map_contains(CljMap *map, ID key);
/** Remove key if present - always returns a new map (COW disabled).
 * Returns the original map if key is not found.
 */
CljMap* map_remove(CljMap *map, ID key);
/** Create a transient map from variable number of key-value pairs.
 * @param count Number of key-value pairs
 * @param ... Alternating key and value arguments (ID type)
 * @return Transient map with all key-value pairs, or NULL on error
 * @note Example: make_transient_map_from_kv(3, key1, val1, key2, val2, key3, val3)
 */
CljMap* make_transient_map_from_kv(unsigned int count, ...);
/** Construct map from stack of key/value pairs (rc=1). */
CljMap* make_map_from_stack(CljObject **pairs, int pair_count);
/** Copy map and add/update key-value pairs in one operation (optimized for embedded).
 * Creates a new map with all parent bindings and new additions in a single heap allocation.
 */
CljMap* map_copy_with_additions(CljMap *parent_map, CljObject **additions, int addition_count);

// === Transient API (Phase 2) ===
/** Convert persistent map to transient. */
CljMap* map_transient(CljMap *map);
/** Associate key->value in transient map (guaranteed in-place). */
CljMap* map_conj(CljMap *tmap, ID key, ID value);
/** Convert transient map back to persistent. */
CljMap* map_persistent(CljMap *tmap);

/* Verwendung: MAP_FOR_EACH(map, key_var, value_var) { ... } */
#define MAP_FOR_EACH(map, key_var, value_var) \
    for (int _i = 0, _cnt = map_count(map); (map) && _i < _cnt; ++_i) \
        for (CljObject **_data_ptr = (map)->data; _data_ptr; _data_ptr = NULL) \
            for (CljObject *key_var __attribute__((unused)) = _data_ptr[2*_i], *value_var __attribute__((unused)) = _data_ptr[2*_i+1]; _data_ptr; _data_ptr = NULL)

#endif

