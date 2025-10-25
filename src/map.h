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
/** Get value for key or NULL (structural key equality). */
/** Associate key->value (replaces existing; retains value). */
void map_assoc(CljObject *map, CljObject *key, CljObject *value);
/** Vector of keys (retained elements). */
CljObject* map_keys(CljObject *map);
/** Vector of values (retained elements). */
CljObject* map_vals(CljObject *map);
/** Number of entries. */
int map_count(CljObject *map);

// Optimized map operations with pointer comparisons
/** Append key/value without duplicate check (retains both). */
void map_put(CljObject *map, CljObject *key, CljObject *value);
/** Iterate pairs and call func(key,value) for each. */
void map_foreach(CljObject *map, void (*func)(CljObject*, CljObject*));
/** Returns 1 if key exists (may use pointer fast-path). */
int map_contains(CljObject *map, CljObject *key);
/** Remove key if present; releases removed items. */
void map_remove(CljObject *map, CljObject *key);
/** Create map from stack of key/value pairs. */
CljObject* map_from_stack(CljObject **pairs, int pair_count);

// === CljValue API (Phase 1: Parallel) ===
/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljObject* make_map(int capacity);
/** Get value for key or NULL if absent (structural key equality). */
CljValue map_get(CljValue map, CljValue key);
/** Associate key->value (replace if key exists; retains value). */
void map_assoc(CljValue map, CljValue key, CljValue value);
/** Associate key->value with Copy-on-Write - returns same or new map depending on RC. */
CljValue map_assoc_cow(CljValue map, CljValue key, CljValue value);
/** Return a vector of keys (retained). */
CljValue map_keys(CljValue map);
/** Return a vector of values (retained). */
CljValue map_vals(CljValue map);
/** Return number of key/value pairs. */
int map_count(CljValue map);
/** Append key/value without structural duplicate check (retains both). */
void map_put(CljValue map, CljValue key, CljValue value);
/** Iterate over all key/value pairs calling func(key,value). */
void map_foreach(CljValue map, void (*func)(CljValue, CljValue));
/** Return 1 if key exists (pointer equality fast-path). */
int map_contains(CljValue map, CljValue key);
/** Remove key if present (releases removed references). */
void map_remove(CljValue map, CljValue key);
CljValue map_from_stack(CljValue *pairs, int pair_count);

// === Transient API (Phase 2) ===
/** Convert persistent map to transient. */
CljValue transient_map(CljValue map);
/** Associate key->value in transient map (guaranteed in-place). */
CljValue conj_map(CljValue tmap, CljValue key, CljValue value);
/** Convert transient map back to persistent. */
CljValue persistent_map(CljValue tmap);

#endif

