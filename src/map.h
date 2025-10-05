#ifndef TINY_CLJ_MAP_H
#define TINY_CLJ_MAP_H

#include "CljObject.h"

/** Create a map with capacity; capacity<=0 returns empty-map singleton. */
CljObject* make_map(int capacity);
/** Get value for key or NULL if absent (structural key equality). */
CljObject* map_get(CljObject *map, CljObject *key);
/** Associate key->value (replace if key exists; retains value). */
void map_assoc(CljObject *map, CljObject *key, CljObject *value);
/** Return a vector of keys (retained). */
CljObject* map_keys(CljObject *map);
/** Return a vector of values (retained). */
CljObject* map_vals(CljObject *map);
/** Return number of key/value pairs. */
int map_count(CljObject *map);
/** Append key/value without structural duplicate check (retains both). */
void map_put(CljObject *map, CljObject *key, CljObject *value);
/** Iterate over all key/value pairs calling func(key,value). */
void map_foreach(CljObject *map, void (*func)(CljObject*, CljObject*));
/** Return 1 if key exists (pointer equality fast-path). */
int map_contains(CljObject *map, CljObject *key);
/** Remove key if present (releases removed references). */
void map_remove(CljObject *map, CljObject *key);

#endif

