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
    if (!is_type((CljObject*)obj, CLJ_MAP) && !is_type((CljObject*)obj, CLJ_TRANSIENT_MAP)) {
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
ID map_get(ID map, ID key);
/** Associate key->value with Copy-on-Write - returns same or new map depending on RC. */
ID map_assoc(ID map, ID key, ID value);
/** Return a vector of keys (retained). */
ID map_keys(ID map);
/** Return a vector of values (retained). */
ID map_vals(ID map);
/** Return number of key/value pairs. */
int map_count(ID map);
/** Append key/value without structural duplicate check (retains both). */
void map_put(ID map, ID key, ID value);
/** Iterate over all key/value pairs calling func(key,value). */
void map_foreach(ID map, void (*func)(ID, ID));
/** Return 1 if key exists (pointer equality fast-path). */
int map_contains(ID map, ID key);
/** Remove key if present (releases removed references). */
void map_remove(ID map, ID key);
/** Construct map from stack of key/value pairs (rc=1). */
CljMap* make_map_from_stack(CljObject **pairs, int pair_count);

// === Transient API (Phase 2) ===
/** Convert persistent map to transient. */
CljValue transient_map(CljValue map);
/** Associate key->value in transient map (guaranteed in-place). */
CljValue conj_map(CljValue tmap, CljValue key, CljValue value);
/** Convert transient map back to persistent. */
CljValue persistent_map(CljValue tmap);

#endif

