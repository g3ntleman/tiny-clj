#include "map.h"
#include "kv_macros.h"
#include "object.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include <stdlib.h>
#include <stdbool.h>

// Empty-map singleton: CLJ_MAP with rc=0, statically initialized
// Note: Cannot use flexible array member in static initialization
static struct {
    CljObject base;
    int count;
    int capacity;
} clj_empty_map_singleton_data = {
    .base = { .type = CLJ_MAP, .rc = 0 },
    .count = 0,
    .capacity = 0
};
static CljMap *clj_empty_map_singleton = (CljMap*)&clj_empty_map_singleton_data;


// === CljValue API (Phase 1: Parallel) ===

/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljMap* make_map(int capacity) {
  if (capacity <= 0) {
    return clj_empty_map_singleton;
  }
  
  // Allocate struct + data array in ONE malloc
  size_t struct_size = sizeof(CljMap);
  size_t data_size = (size_t)capacity * 2 * sizeof(CljObject*);
  size_t total_size = struct_size + data_size;
  
  CljMap *map = (CljMap*)malloc(total_size);
  if (!map) {
    throw_oom(CLJ_MAP);
  }
  
  map->base.type = CLJ_MAP;
  map->base.rc = 1;
  map->count = 0;
  map->capacity = capacity;
  
  // Initialize embedded array to NULL
  for (int i = 0; i < capacity * 2; i++) {
    map->data[i] = NULL;
  }
  
  return map;
}

/** Get value for key or NULL if absent (structural key equality). */
ID map_get(CljValue map, CljValue key) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  if (!map_obj || !is_type(map_obj, CLJ_MAP) || !key_obj)
    return NULL;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *stored_key = KV_KEY(map_data->data, i);
    // Fast path: pointer comparison first (for interned symbols)
    if (stored_key == key_obj) {
      CljValue result = KV_VALUE(map_data->data, i);
        // printf("DEBUG: map_get found key: %p -> %p\n", key_obj, result);
        // printf("DEBUG: result is_fixnum: %d\n", is_fixnum(result));
        // if (is_fixnum(result)) {
        //     printf("DEBUG: result as_fixnum: %d\n", as_fixnum(result));
        // }
      return result;
    }
    // Fallback: structural comparison for non-interned objects
    if (clj_equal(stored_key, key_obj)) {
      CljValue result = KV_VALUE(map_data->data, i);
      // printf("DEBUG: map_get found key (structural): %p -> %p\n", key_obj, result);
      // printf("DEBUG: result is_fixnum: %d\n", is_fixnum(result));
      // if (is_fixnum(result)) {
      //   printf("DEBUG: result as_fixnum: %d\n", as_fixnum(result));
      // }
      return result;
    }
  }
  return NULL;
}

/** Associate key->value (replace if key exists; retains value). */
void map_assoc(CljValue map, CljValue key, CljValue value) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  if (!map_obj || !is_type(map_obj, CLJ_MAP) || !key_obj) {
    return;
  }
  CljMap *map_data = as_map(map_obj);
  if (!map_data) {
    return;
  }
  
  // Check if key exists
  for (int i = 0; i < map_data->count; i++) {
    CljObject *k = KV_KEY(map_data->data, i);
    if (k && clj_equal(k, key_obj)) {
      ASSIGN(KV_VALUE(map_data->data, i), value_obj);
      return;
    }
  }
  
  // Add new entry (if capacity allows)
  // printf("DEBUG: map_assoc capacity check: count=%d, capacity=%d\n", map_data->count, map_data->capacity);
  if (map_data->count < map_data->capacity) {
    int idx = map_data->count;
    KV_ASSIGN_PAIR(map_data->data, idx,
                key_obj ? (RETAIN(key_obj), key_obj) : NULL,
                value_obj ? (RETAIN(value_obj), value_obj) : NULL);
    map_data->count++;
    // printf("DEBUG: map_assoc added: key=%p, value=%p, count=%d\n", key_obj, value_obj, map_data->count);
    // printf("DEBUG: value is_fixnum: %d\n", is_fixnum((CljValue)value_obj));
    // if (is_fixnum((CljValue)value_obj)) {
    //   printf("DEBUG: value as_fixnum: %d\n", as_fixnum((CljValue)value_obj));
    // }
  } else {
    // printf("DEBUG: map_assoc failed - capacity exceeded: count=%d, capacity=%d\n", map_data->count, map_data->capacity);
  }
  // Note: No growth for in-place map_assoc (use map_assoc_cow for that)
}

/** Associate key->value with Copy-on-Write - returns same or new map depending on RC. */
CljValue map_assoc_cow(CljValue map, CljValue key, CljValue value) {
  CljObject *map_obj = (CljObject*)map;
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  
  if (!map_obj || !is_type(map_obj, CLJ_MAP) || !key_obj) {
    return map;  // Return original map on error
  }
  
  CljMap *map_data = as_map(map_obj);
  if (!map_data) {
    return map;  // Return original map on error
  }
  
  // OPTIMIZATION: If RC=1, we're the only owner - mutate in-place
  if (map_data->base.rc == 1) {
    // Check if key exists - update value
    for (int i = 0; i < map_data->count; i++) {
      CljObject *k = KV_KEY(map_data->data, i);
      if (k && clj_equal(k, key_obj)) {
        ASSIGN(KV_VALUE(map_data->data, i), value_obj);
        return map;  // Return SAME map
      }
    }
    
    // Add new entry (if capacity allows)
    if (map_data->count < map_data->capacity) {
      int idx = map_data->count;
      map_data->data[2 * idx] = key_obj ? (RETAIN(key_obj), key_obj) : NULL;
      map_data->data[2 * idx + 1] = value_obj ? (RETAIN(value_obj), value_obj) : NULL;
      map_data->count++;
      return map;  // Return SAME map
    }
    
    // Out of capacity - need to grow (fall through to COW path)
  }
  
  // RC>1 or out of capacity: Copy-on-Write with optional growth
  int new_capacity = map_data->capacity;
  if (map_data->count >= map_data->capacity) {
    new_capacity = map_data->capacity * 2;
    if (new_capacity < 4) new_capacity = 4;
  }
  
  // Allocate new map with embedded data array
  size_t struct_size = sizeof(CljMap);
  size_t data_size = (size_t)new_capacity * 2 * sizeof(CljObject*);
  CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
  if (!new_map) {
    return map;  // Return original map on OOM
  }
  
  new_map->base.type = CLJ_MAP;
  new_map->base.rc = 1;
  new_map->count = 0;  // Start with 0, will be set correctly below
  new_map->capacity = new_capacity;
  
  // Initialize new data array
  for (int i = 0; i < new_capacity * 2; i++) {
    new_map->data[i] = NULL;
  }
  
  // Copy existing entries with RETAIN
  bool key_found = false;
  int new_idx = 0;
  
  for (int i = 0; i < map_data->count; i++) {
    if (KV_KEY(map_data->data, i) && clj_equal(KV_KEY(map_data->data, i), key_obj)) {
      // Key found - update value
      ASSIGN(KV_KEY(new_map->data, new_idx), KV_KEY(map_data->data, i));
      ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
      key_found = true;
    } else {
      // Copy existing entry
      ASSIGN(KV_KEY(new_map->data, new_idx), KV_KEY(map_data->data, i));
      ASSIGN(KV_VALUE(new_map->data, new_idx), KV_VALUE(map_data->data, i));
    }
    new_idx++;
  }
  
  // Add new key if not found
  if (!key_found && new_idx < new_map->capacity) {
    ASSIGN(KV_KEY(new_map->data, new_idx), key_obj);
    ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
    new_idx++;
  }
  
  // Set final count
  new_map->count = new_idx;
  
  return (CljValue)new_map;  // Return NEW map
}

/** Return a vector of keys (retained). */
CljValue map_keys(CljValue map) {
  CljObject *map_obj = (CljObject*)map;
  if (!map_obj || !is_type(map_obj, CLJ_MAP))
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
  if (!map_obj || !is_type(map_obj, CLJ_MAP))
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
  if (!map_obj || !is_type(map_obj, CLJ_MAP) || !key_obj)
    return;
  CljMap *map_data = as_map(map_obj);
  if (!map_data)
    return;
  // Note: map_put() cannot grow embedded arrays - use map_assoc_cow() instead
  // This function is deprecated for embedded array approach
  KV_ASSIGN_PAIR(map_data->data, map_data->count, key_obj, value_obj);
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
  if (!map_obj || !is_type(map_obj, CLJ_MAP) || !key_obj)
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
  if (!map_obj || !is_type(map_obj, CLJ_MAP) || !key_obj)
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
      KV_ASSIGN_PAIR(map_data->data, j, KV_KEY(map_data->data, j + 1),
                     KV_VALUE(map_data->data, j + 1));
    }
    map_data->count--;
  }
}

CljObject* make_map_from_stack(CljObject **pairs, int pair_count) {
    if (pair_count == 0) {
        return (CljObject*)make_map(0);
    }
    CljMap *map = (CljMap*)make_map(pair_count * 2);
    CljMap *map_data = as_map((CljObject*)map);
    if (!map_data) return NULL;
    for (int i = 0; i < pair_count; i++) {
        CljObject *key = KV_KEY(pairs, i);
        CljObject *value = KV_VALUE(pairs, i);
        KV_ASSIGN_PAIR(map_data->data, i, key, value);
        if (key) RETAIN(key);
        if (value) RETAIN(value);
    }
    map_data->count = pair_count;
    return (CljObject*)map;
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
    
    // For transient maps, we need to allocate with embedded data array
    if (m->capacity > 0) {
        // Allocate new transient map with embedded data array
        size_t struct_size = sizeof(CljMap);
        size_t data_size = (size_t)m->capacity * 2 * sizeof(CljObject*);
        size_t total_size = struct_size + data_size;
        
        CljMap *new_tmap = (CljMap*)malloc(total_size);
        if (!new_tmap) {
            free(tmap);
            return NULL;
        }
        
        new_tmap->base.type = CLJ_TRANSIENT_MAP;
        new_tmap->base.rc = 1;
        new_tmap->count = m->count;
        new_tmap->capacity = m->capacity;
        
        // Initialize embedded array
        for (int i = 0; i < m->capacity * 2; i++) {
            new_tmap->data[i] = NULL;
        }
        
        // Copy existing entries
        for (int i = 0; i < m->count * 2; i++) {
            if (m->data[i]) {
                new_tmap->data[i] = m->data[i];
                RETAIN(m->data[i]);
            }
        }
        
        // Replace the old tmap with the new one
        free(tmap);
        tmap = new_tmap;
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
        // Cannot grow embedded arrays - transient maps have fixed capacity
        // This is a limitation of the embedded array approach
        return NULL;  // Out of capacity
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
    CljMap *new_map = make_map(m->capacity);  // New instance
    CljMap *new_m = new_map;
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
    
    return (CljValue)new_map; // NEW persistent collection
}

