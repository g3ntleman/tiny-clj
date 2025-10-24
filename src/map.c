#include "map.h"
#include "kv_macros.h"
#include "object.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include <stdlib.h>
#include <stdbool.h>

// Empty-map singleton: CLJ_MAP with rc=0, statically initialized
static struct {
    CljMap map;
} clj_empty_map_singleton_data = {
    .map = {
        .base = { .type = CLJ_MAP, .rc = 0 },
        .count = 0,
        .capacity = 0,
        .data = NULL
    }
};
static CljMap *clj_empty_map_singleton = &clj_empty_map_singleton_data.map;


// === CljValue API (Phase 1: Parallel) ===

/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljObject* make_map(int capacity) {
  if (capacity <= 0) {
    return (CljObject*)clj_empty_map_singleton;
  }
  CljMap *map = ALLOC(CljMap, 1);
  if (!map)
    throw_oom(CLJ_MAP);
  map->base.type = CLJ_MAP;
  map->base.rc = 1;
  map->count = 0;
  map->capacity = capacity;
  map->data = (CljObject **)calloc((size_t)capacity * 2, sizeof(CljObject *));
  if (!map->data) {
    free(map);
    throw_oom(CLJ_MAP);
  }
  return (CljObject*)map;
}

/** Get value for key or NULL if absent (structural key equality). */
CljValue map_get(CljValue map, CljValue key) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  if (!map_obj || map_obj->type != CLJ_MAP || !key_obj)
    return (CljValue)NULL;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return (CljValue)NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *stored_key = KV_KEY(map_data->data, i);
    // Fast path: pointer comparison first (for interned symbols)
    if (stored_key == key_obj) {
      return (CljValue)KV_VALUE(map_data->data, i);
    }
    // Fallback: structural comparison for non-interned objects
    if (clj_equal(stored_key, key_obj)) {
      return (CljValue)KV_VALUE(map_data->data, i);
    }
  }
  return (CljValue)NULL;
}

/** Associate key->value (replace if key exists; retains value). */
void map_assoc(CljValue map, CljValue key, CljValue value) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  if (!map_obj || map_obj->type != CLJ_MAP || !key_obj) {
    return;
  }
  CljMap *map_data = as_map(map_obj);
  if (!map_data) {
    return;
  }
  for (int i = 0; i < map_data->count; i++) {
    CljObject *k = map_data->data[2 * i];
    if (k && clj_equal(k, key_obj)) {
      CljObject *old_value = map_data->data[2 * i + 1];
      if (old_value)
        RELEASE(old_value);
      map_data->data[2 * i + 1] = value_obj ? (RETAIN(value_obj), value_obj) : NULL;
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
  map_data->data[2 * idx] = key_obj ? (RETAIN(key_obj), key_obj) : NULL;
  map_data->data[2 * idx + 1] = value_obj ? (RETAIN(value_obj), value_obj) : NULL;
  map_data->count++;
}

/** Return a vector of keys (retained). */
CljValue map_keys(CljValue map) {
  CljObject *map_obj = (CljObject*)map;
  if (!map_obj || map_obj->type != CLJ_MAP)
    return (CljValue)NULL;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return (CljValue)NULL;
  CljValue keys_val = make_vector(map_data->count, 0);
  CljObject *keys = (CljObject*)keys_val;
  CljPersistentVector *keys_vec = as_vector(keys);
  if (!keys_vec)
    return (CljValue)NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *key = KV_KEY(map_data->data, i);
    if ((keys_vec->data[i] = RETAIN(key))) {
      keys_vec->count++;
    }
  }
  return keys_val;
}

/** Return a vector of values (retained). */
CljValue map_vals(CljValue map) {
  CljObject *map_obj = (CljObject*)map;
  if (!map_obj || map_obj->type != CLJ_MAP)
    return (CljValue)NULL;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return (CljValue)NULL;
  CljValue vals_val = make_vector(map_data->count, 0);
  CljObject *vals = (CljObject*)vals_val;
  CljPersistentVector *vals_vec = as_vector(vals);
  if (!vals_vec)
    return (CljValue)NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *val = KV_VALUE(map_data->data, i);
    if ((vals_vec->data[i] = RETAIN(val))) {
      vals_vec->count++;
    }
  }
  return vals_val;
}

/** Return number of key/value pairs. */
int map_count(CljValue map) {
  CljObject *map_obj = (CljObject*)map;
  if (!map_obj) return 0;
  
  // Check if it's a heap object
  if (!is_heap_object(map)) {
    return 0;  // Immediates are not maps
  }
  
  if (map_obj->type != CLJ_MAP)
    return 0;
  CljMap *map_data = as_map(map_obj);
  return map_data ? map_data->count : 0;
}

/** Append key/value without structural duplicate check (retains both). */
void map_put(CljValue map, CljValue key, CljValue value) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  if (!map_obj || map_obj->type != CLJ_MAP || !key_obj)
    return;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return;
  int newcap = map_data->capacity * 2;
  if (newcap < 4)
    newcap = 4;
  map_data->data =
      (CljObject **)realloc(map_data->data, sizeof(CljObject *) * newcap * 2);
  map_data->capacity = newcap;
  map_data->data[map_data->count * 2] = key_obj;
  map_data->data[map_data->count * 2 + 1] = value_obj;
  map_data->count++;
  RETAIN(key_obj);
  RETAIN(value_obj);
}

/** Iterate over all key/value pairs calling func(key,value). */
void map_foreach(CljValue map, void (*func)(CljValue, CljValue)) {
  CljObject *map_obj = (CljObject*)map;
  if (!map_obj || map_obj->type != CLJ_MAP || !func)
    return;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return;
  KV_FOREACH(map_data->data, map_data->count, key, value,
             { func((CljValue)key, (CljValue)value); });
}

/** Return 1 if key exists (pointer equality fast-path). */
int map_contains(CljValue map, CljValue key) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  if (!map_obj || map_obj->type != CLJ_MAP || !key_obj)
    return 0;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return 0;
  return KV_CONTAINS(map_data->data, map_data->count, key_obj);
}

/** Remove key if present (releases removed references). */
void map_remove(CljValue map, CljValue key) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  if (!map_obj || map_obj->type != CLJ_MAP || !key_obj)
    return;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return;
  int index = KV_FIND_INDEX(map_data->data, map_data->count, key_obj);
  if (index >= 0) {
    CljObject *old_key = KV_KEY(map_data->data, index);
    CljObject *old_value = KV_VALUE(map_data->data, index);
    if (old_key)
      RELEASE(old_key);
    if (old_value)
      RELEASE(old_value);
    for (int j = index; j < map_data->count - 1; j++) {
      KV_SET_PAIR(map_data->data, j, KV_KEY(map_data->data, j + 1),
                  KV_VALUE(map_data->data, j + 1));
    }
    map_data->count--;
  }
}

CljValue map_from_stack(CljValue *pairs, int pair_count) {
    if (pair_count == 0) {
        return (CljValue)make_map(0);
    }
    CljMap *map = (CljMap*)make_map(pair_count * 2);
    CljMap *map_data = as_map((CljObject*)map);
    if (!map_data) return (CljValue)NULL;
      for (int i = 0; i < pair_count; i++) {
          CljObject *key = (CljObject*)pairs[i * 2];
          CljObject *value = (CljObject*)pairs[i * 2 + 1];
          map_data->data[i * 2] = key;
          map_data->data[i * 2 + 1] = value;
          if (key) RETAIN(key);
          if (value) RETAIN(value);
      }
      map_data->count = pair_count;
    return (CljValue)map;
}

// === Transient API (Phase 2) ===

/** Convert persistent map to transient. */
CljValue transient_map(CljValue map) {
    if (!map || map->type != CLJ_MAP) {
        return NULL;
    }
    
    CljMap *m = as_map((CljObject*)map);
    if (!m) return NULL;
    
    // Create copy with transient type
    CljMap *tmap = ALLOC(CljMap, 1);
    if (!tmap) return NULL;
    
    tmap->base.type = CLJ_TRANSIENT_MAP;
    tmap->base.rc = 1;
    tmap->count = m->count;
    tmap->capacity = m->capacity;
    
    if (m->capacity > 0) {
        tmap->data = (CljObject**)calloc((size_t)m->capacity * 2, sizeof(CljObject*));
        if (!tmap->data) {
            free(tmap);
            return NULL;
        }
        for (int i = 0; i < m->count * 2; i++) {
            if (m->data[i]) {
                tmap->data[i] = m->data[i];
                RETAIN(m->data[i]);
            }
        }
    } else {
        tmap->data = NULL;
    }
    
    return (CljValue)tmap;
}

/** Associate key->value in transient map (guaranteed in-place). */
CljValue conj_map(CljValue tmap, CljValue key, CljValue value) {
    if (!tmap || tmap->type != CLJ_TRANSIENT_MAP || !key || !value) {
        return NULL;
    }
    
    CljMap *m = as_map((CljObject*)tmap);
    if (!m) return NULL;
    
    // Check if key already exists
    for (int i = 0; i < m->count; i++) {
        if (m->data[i * 2] == (CljObject*)key) {
            // Replace existing value
            if (m->data[i * 2 + 1]) {
                RELEASE(m->data[i * 2 + 1]);
            }
            m->data[i * 2 + 1] = RETAIN((CljObject*)value);
            return tmap;
        }
    }
    
    // Add new key-value pair
    if (m->count >= m->capacity) {
        int newcap = m->capacity ? m->capacity * 2 : 4;
        void *p = realloc(m->data, (size_t)newcap * 2 * sizeof(CljObject*));
        if (!p) return NULL;
        m->data = (CljObject**)p;
        for (int i = m->capacity * 2; i < newcap * 2; ++i)
            m->data[i] = NULL;
        m->capacity = newcap;
    }
    
    m->data[m->count * 2] = (CljObject*)key;
    m->data[m->count * 2 + 1] = (CljObject*)value;
    RETAIN((CljObject*)key);
    RETAIN((CljObject*)value);
    m->count++;
    
    return tmap; // In-place mutation
}

/** Convert transient map back to persistent. */
CljValue persistent_map(CljValue tmap) {
    if (!tmap || tmap->type != CLJ_TRANSIENT_MAP) {
        return NULL;
    }
    
    CljMap *m = as_map((CljObject*)tmap);
    if (!m) return NULL;
    
    // Clojure semantics: Create NEW persistent collection
    CljObject *new_map = make_map(m->capacity);  // New instance
    CljMap *new_m = as_map((CljObject*)new_map);
    if (!new_m) return NULL;
    
    // Copy all key-value pairs
    for (int i = 0; i < m->count; i++) {
        if (m->data[i * 2] && m->data[i * 2 + 1]) {
            new_m->data[i * 2] = m->data[i * 2];
            new_m->data[i * 2 + 1] = m->data[i * 2 + 1];
            RETAIN(m->data[i * 2]);
            RETAIN(m->data[i * 2 + 1]);
        }
    }
    new_m->count = m->count;
    
    // Original transient becomes "invalidated" (can be implemented later)
    // m->base.type = CLJ_INVALID;  // TODO: Implement invalidation
    
    return new_map; // NEW persistent collection
}

