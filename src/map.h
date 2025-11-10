#ifndef TINY_CLJ_MAP_H
#define TINY_CLJ_MAP_H

#include "object.h"
#include "value.h"

// CljMap struct definition
typedef struct {
    CljObject base;
    int count;
    int capacity;
    CljObject *data[];
} CljMap;

// Type-safe casting
static inline CljMap* as_map(ID obj) {
    if (!obj || (TAG(obj) != CLJ_MAP && TAG(obj) != CLJ_TRANSIENT_MAP)) {
#ifdef DEBUG
        const char *actual_type = obj ? "Vector" : "NULL";
        fprintf(stderr, "Assertion failed: Expected Map, got %s at %s:%d\n", 
                actual_type, __FILE__, __LINE__);
#endif
        abort();
    }
    return (CljMap*)obj;
}

// Map operations (optimized with pointer fast paths)
/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljMap* make_map(int capacity);
/** Get value for key or NULL if absent (structural key equality). */
ID map_get(CljMap *map, ID key);
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

#endif

