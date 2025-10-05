#include "map.h"
#include "common.h"
#include "kv_macros.h"
#include "runtime.h"
#include "vector.h"
#include <stdlib.h>
#include <stdbool.h>

// Empty-map singleton: CLJ_MAP object with rc=0 and no backing store
// (data=NULL)
static CljObject clj_empty_map_singleton;
static void init_empty_map_singleton_once(void) {
  static bool initialized = false;
  if (initialized)
    return;
  clj_empty_map_singleton.type = CLJ_MAP;
  clj_empty_map_singleton.rc = 0;
  clj_empty_map_singleton.as.data = NULL;
    initialized = true;
}

/** @brief Create a new map with specified capacity */
CljObject *make_map(int capacity) {
  if (capacity <= 0) {
    init_empty_map_singleton_once();
    return &clj_empty_map_singleton;
  }
  CljMap *map = ALLOC(CljMap, 1);
  if (!map)
    return NULL;
  map->base.type = CLJ_MAP;
  map->base.rc = 1;
  map->count = 0;
  map->capacity = capacity;
  map->data = (CljObject **)calloc((size_t)capacity * 2, sizeof(CljObject *));
  CljObject *obj = ALLOC(CljObject, 1);
  if (!obj) {
    free(map);
    return NULL;
  }
  obj->type = CLJ_MAP;
  obj->rc = 1;
  obj->as.data = (void *)map;
  return obj;
}

/** @brief Get value from map by key */
CljObject *map_get(CljObject *map, CljObject *key) {
  if (!map || map->type != CLJ_MAP || !key)
    return NULL;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return NULL;
  for (int i = 0; i < map_data->count; i++) {
    if (clj_equal(KV_KEY(map_data->data, i), key)) {
      return KV_VALUE(map_data->data, i);
    }
  }
  return NULL;
}

/** @brief Associate key-value pair in map */
void map_assoc(CljObject *map, CljObject *key, CljObject *value) {
  if (!map || map->type != CLJ_MAP || !key)
    return;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *k = map_data->data[2 * i];
    if (k && clj_equal(k, key)) {
      CljObject *old_value = map_data->data[2 * i + 1];
      if (old_value)
        release(old_value);
      map_data->data[2 * i + 1] = value ? (retain(value), value) : NULL;
      return;
    }
  }
  if (map_data->count >= map_data->capacity) {
    int new_capacity = map_data->capacity * 2;
    if (new_capacity < 4)
      new_capacity = 4;
    CljObject **new_data =
        (CljObject **)calloc((size_t)new_capacity * 2, sizeof(CljObject *));
    if (!new_data)
      return;
    for (int i = 0; i < map_data->count; i++) {
      new_data[i * 2] = map_data->data[2 * i];
      new_data[i * 2 + 1] = map_data->data[2 * i + 1];
    }
    free(map_data->data);
    map_data->data = new_data;
    map_data->capacity = new_capacity;
  }
  int idx = map_data->count;
  map_data->data[2 * idx] = key ? (retain(key), key) : NULL;
  map_data->data[2 * idx + 1] = value ? (retain(value), value) : NULL;
  map_data->count++;
}

CljObject *map_keys(CljObject *map) {
  if (!map || map->type != CLJ_MAP)
    return NULL;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return NULL;
  CljObject *keys = make_vector(map_data->count, 0);
  CljPersistentVector *keys_vec = as_vector(keys);
  if (!keys_vec)
    return NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *key = KV_KEY(map_data->data, i);
    if (key) {
      keys_vec->data[i] = key;
      retain(key);
      keys_vec->count++;
    }
  }
  return keys;
}

CljObject *map_vals(CljObject *map) {
  if (!map || map->type != CLJ_MAP)
    return NULL;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return NULL;
  CljObject *vals = make_vector(map_data->count, 0);
  CljPersistentVector *vals_vec = as_vector(vals);
  if (!vals_vec)
    return NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *val = KV_VALUE(map_data->data, i);
    if (val) {
      vals_vec->data[i] = val;
      retain(val);
      vals_vec->count++;
    }
  }
  return vals;
}

int map_count(CljObject *map) {
  if (!map || map->type != CLJ_MAP)
    return 0;
  CljMap *map_data = as_map(map);
  return map_data ? map_data->count : 0;
}

void map_put(CljObject *map, CljObject *key, CljObject *value) {
  if (!map || map->type != CLJ_MAP || !key)
    return;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return;
  int newcap = map_data->capacity * 2;
  if (newcap < 4)
    newcap = 4;
  map_data->data =
      (CljObject **)realloc(map_data->data, sizeof(CljObject *) * newcap * 2);
  map_data->capacity = newcap;
  map_data->data[map_data->count * 2] = key;
  map_data->data[map_data->count * 2 + 1] = value;
  map_data->count++;
  retain(key);
  retain(value);
}

void map_foreach(CljObject *map, void (*func)(CljObject *, CljObject *)) {
  if (!map || map->type != CLJ_MAP || !func)
    return;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return;
  KV_FOREACH(map_data->data, map_data->count, key, value,
             { func(key, value); });
}

int map_contains(CljObject *map, CljObject *key) {
  if (!map || map->type != CLJ_MAP || !key)
    return 0;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return 0;
  return KV_CONTAINS(map_data->data, map_data->count, key);
}

void map_remove(CljObject *map, CljObject *key) {
  if (!map || map->type != CLJ_MAP || !key)
    return;
  CljMap *map_data = as_map(map);
  if (!map_data)
    return;
  int index = KV_FIND_INDEX(map_data->data, map_data->count, key);
  if (index >= 0) {
    CljObject *old_key = KV_KEY(map_data->data, index);
    CljObject *old_value = KV_VALUE(map_data->data, index);
    if (old_key)
      release(old_key);
    if (old_value)
      release(old_value);
    for (int j = index; j < map_data->count - 1; j++) {
      KV_SET_PAIR(map_data->data, j, KV_KEY(map_data->data, j + 1),
                  KV_VALUE(map_data->data, j + 1));
    }
    map_data->count--;
  }
}

CljObject* map_from_stack(CljObject **pairs, int pair_count) {
    if (pair_count == 0) {
        return make_map(0);
    }
    CljObject *map = make_map(pair_count * 2);
    CljMap *map_data = as_map(map);
    if (!map_data) return NULL;
    for (int i = 0; i < pair_count; i++) {
        map_data->data[i * 2] = pairs[i * 2];
        map_data->data[i * 2 + 1] = pairs[i * 2 + 1];
        if (pairs[i * 2]) retain(pairs[i * 2]);
        if (pairs[i * 2 + 1]) retain(pairs[i * 2 + 1]);
    }
    map_data->count = pair_count;
    return map;
}
