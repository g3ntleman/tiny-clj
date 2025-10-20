#ifndef TINY_CLJ_MAP_H
#define TINY_CLJ_MAP_H

#include "object.h"
#include "value.h"

// === Legacy API (deprecated - use CljValue API) ===
/** @deprecated Use make_map() instead. Create a map with capacity; capacity<=0 returns empty-map singleton. */
CljMap* make_map_old(int capacity);
/** @deprecated Use map_get() instead. Get value for key or NULL if absent (structural key equality). */
CljObject* map_get(CljObject *map, CljObject *key);
/** @deprecated Use map_assoc() instead. Associate key->value (replace if key exists; retains value). */
void map_assoc(CljObject *map, CljObject *key, CljObject *value);
/** @deprecated Use map_keys_v() instead. Return a vector of keys (retained). */
CljObject* map_keys(CljObject *map);
/** @deprecated Use map_vals_v() instead. Return a vector of values (retained). */
CljObject* map_vals(CljObject *map);
/** @deprecated Use map_count_v() instead. Return number of key/value pairs. */
int map_count(CljObject *map);
/** @deprecated Use map_put_v() instead. Append key/value without structural duplicate check (retains both). */
void map_put(CljObject *map, CljObject *key, CljObject *value);
/** @deprecated Use map_foreach_v() instead. Iterate over all key/value pairs calling func(key,value). */
void map_foreach(CljObject *map, void (*func)(CljObject*, CljObject*));
/** @deprecated Use map_contains_v() instead. Return 1 if key exists (pointer equality fast-path). */
int map_contains(CljObject *map, CljObject *key);
/** @deprecated Use map_remove_v() instead. Remove key if present (releases removed references). */
void map_remove(CljObject *map, CljObject *key);
/** @deprecated Use map_from_stack() instead. */
CljObject* map_from_stack(CljObject **pairs, int pair_count);

// === CljValue API (Phase 1: Parallel) ===
/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljValue make_map(int capacity);
/** Get value for key or NULL if absent (structural key equality). */
CljValue map_get(CljValue map, CljValue key);
/** Associate key->value (replace if key exists; retains value). */
void map_assoc(CljValue map, CljValue key, CljValue value);
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

