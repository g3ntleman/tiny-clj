#include "map.h"
#include "kv_macros.h"
#include "object.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include "exception.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

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
ID map_get(CljMap *map, ID key) {
  if (!map || !key)
    return NULL;
  CljObject *key_obj = (CljObject*)key;
  if (!key_obj)
    return NULL;
  CljMap *map_data = map;
  if (!map_data)
    return NULL;
  
  for (int i = 0; i < map_data->count; i++) {
    CljObject *stored_key = KV_KEY(map_data->data, i);
    // Fast path: pointer comparison first (for interned symbols)
    if (stored_key == key_obj) {
      return KV_VALUE(map_data->data, i);
    }
    // Fallback: structural comparison for non-interned objects
    if (stored_key && clj_equal(stored_key, key_obj)) {
      // DEBUG: Throw exception if structural equality but not pointer equality
      // This indicates that symbols are not correctly interned
      if (is_type(stored_key, CLJ_SYMBOL) && is_type(key_obj, CLJ_SYMBOL)) {
        CljSymbol *stored_sym = as_symbol(stored_key);
        throw_exception_formatted("SymbolInterningError", __FILE__, __LINE__, 0,
            "Symbol '%s' found by structural equality but not pointer equality. "
            "This indicates that symbols are not correctly interned. "
            "Stored symbol: %p, Key symbol: %p",
            stored_sym && stored_sym->name ? stored_sym->name : "unknown",
            stored_key, key_obj);
      }
      return KV_VALUE(map_data->data, i);
    }
  }
  
  return NULL;
}


/** Associate key->value - always returns a new map (COW disabled).
 * To re-enable COW: Change #if 0 to #if 1 below to enable RC=1 hot-path.
 */
CljMap* map_assoc(CljMap* map, ID key, ID value) {
  if (!map || !is_type((CljObject*)map, CLJ_MAP)) {
    return map;  // Return original map on error
  }
  
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  
  if (!key_obj) {
    return map;  // Return original map on error
  }
  
  CljMap *map_data = map;
  
#if 0
  // COW HOT-PATH: RC=1 → in-place mutation (disabled by default)
  // To re-enable COW: Change #if 0 to #if 1 above
  if (map_data->base.rc == 1) {
    // Check if key exists - update value (linear search necessary)
    // OPTIMIZATION: Fast path for pointer equality (interned symbols/keywords)
    for (int i = 0; i < map_data->count; i++) {
      CljObject *k = KV_KEY(map_data->data, i);
      // Fast path: pointer comparison first (for interned symbols/keywords)
      if (k == key_obj) {
        // Key found: update in-place (no branches after this)
        ASSIGN(KV_VALUE(map_data->data, i), value_obj);
        return map;  // Return SAME map
      }
      // Fallback: structural comparison for non-interned objects
      if (k && clj_equal(k, key_obj)) {
        // Key found: update in-place (no branches after this)
        ASSIGN(KV_VALUE(map_data->data, i), value_obj);
        return map;  // Return SAME map
      }
    }
    
    // Key not found: add new entry if capacity allows
    if (map_data->count < map_data->capacity) {
      // Direct in-place mutation (no branches after this)
      int idx = map_data->count;
      ASSIGN(KV_KEY(map_data->data, idx), key_obj);
      ASSIGN(KV_VALUE(map_data->data, idx), value_obj);
      map_data->count++;
      return map;  // Return SAME map
    }
    
    // Out of capacity - need to grow (fall through to COW path)
  }
#endif
  
  // Always create a new map (COW disabled)
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
    CljObject *k = KV_KEY(map_data->data, i);
    // Fast path: pointer comparison first (for interned symbols/keywords)
    if (k == key_obj) {
      // Key found - update value
      ASSIGN(KV_KEY(new_map->data, new_idx), KV_KEY(map_data->data, i));
      ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
      key_found = true;
      new_idx++;
    } else if (k && clj_equal(k, key_obj)) {
      // Key found - update value (structural comparison)
      ASSIGN(KV_KEY(new_map->data, new_idx), KV_KEY(map_data->data, i));
      ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
      key_found = true;
      new_idx++;
    } else {
      // Copy existing entry
      ASSIGN(KV_KEY(new_map->data, new_idx), KV_KEY(map_data->data, i));
      ASSIGN(KV_VALUE(new_map->data, new_idx), KV_VALUE(map_data->data, i));
      new_idx++;
    }
  }
  
  // Add new key if not found
  if (!key_found && new_idx < new_map->capacity) {
    ASSIGN(KV_KEY(new_map->data, new_idx), key_obj);
    ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
    new_idx++;
  }
  
  // Set final count
  new_map->count = new_idx;
  
  return new_map;  // Return NEW map
}

/** Return a vector of keys (retained). */
ID map_keys(CljMap *map) {
  if (!map)
    return NULL;
  CljMap *map_data = map;
  if (!map_data)
    return NULL;
  CljVector keys_vec = make_vector(map_data->count, 0);
  if (!keys_vec)
    return NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *key = KV_KEY(map_data->data, i);
    if ((keys_vec->data[i] = RETAIN(key))) {
      keys_vec->count++;
    }
  }
  return (ID)keys_vec;
}

/** Return a vector of values (retained). */
ID map_vals(CljMap *map) {
  if (!map)
    return NULL;
  CljMap *map_data = map;
  if (!map_data)
    return NULL;
  CljVector vals_vec = make_vector(map_data->count, 0);
  if (!vals_vec)
    return NULL;
  for (int i = 0; i < map_data->count; i++) {
    CljObject *val = KV_VALUE(map_data->data, i);
    if ((vals_vec->data[i] = RETAIN(val))) {
      vals_vec->count++;
    }
  }
  return (ID)vals_vec;
}

/** Return number of key/value pairs. */
int map_count(CljMap *map) {
  if (!map) return 0;
  return map->count;
}

/** Append key/value without structural duplicate check (retains both). */
void map_put(CljMap *map, ID key, ID value) {
  if (!map || !key)
    return;
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  if (!key_obj)
    return;
  CljMap *map_data = map;
  if (!map_data)
    return;
  // Note: map_put() cannot grow embedded arrays - use map_assoc() instead
  // This function is deprecated for embedded array approach
  KV_ASSIGN_PAIR(map_data->data, map_data->count, key_obj, value_obj);
  map_data->count++;
  RETAIN(key_obj);
  RETAIN(value_obj);
}

/** Iterate over all key/value pairs calling func(key,value). */
void map_foreach(CljMap *map, void (*func)(ID, ID)) {
  if (!map || !func)
    return;
  CljMap *map_data = map;
  if (!map_data)
    return;
  KV_FOREACH(map_data->data, map_data->count, key, value,
             { func((ID)key, (ID)value); });
}

/** Return 1 if key exists (pointer equality fast-path, fallback to structural equality).
 * 
 * NOTE: This function is consistent with map_get() which also uses structural equality
 * as a fallback. If all symbols are correctly interned, pointer equality should be
 * sufficient. The structural equality fallback is for edge cases where symbols might
 * not be interned (e.g., during parsing or in test code).
 */
int map_contains(CljMap *map, ID key) {
  if (!map || !key)
    return 0;
  CljObject *key_obj = (CljObject*)key;
  if (!key_obj)
    return 0;
  CljMap *map_data = map;
  if (!map_data)
    return 0;
  
  // Use same logic as map_get: pointer equality first, then structural equality
  // This ensures consistency between map_contains and map_get
  for (int i = 0; i < map_data->count; i++) {
    CljObject *stored_key = KV_KEY(map_data->data, i);
    // Fast path: pointer comparison first (for interned symbols)
    if (stored_key == key_obj) {
      return 1;
    }
    // Fallback: structural comparison for non-interned objects
    // This is consistent with map_get() behavior
    if (stored_key && clj_equal(stored_key, key_obj)) {
      // DEBUG: Throw exception if structural equality but not pointer equality
      // This indicates that symbols are not correctly interned
      if (is_type(stored_key, CLJ_SYMBOL) && is_type(key_obj, CLJ_SYMBOL)) {
        CljSymbol *stored_sym = as_symbol(stored_key);
        throw_exception_formatted("SymbolInterningError", __FILE__, __LINE__, 0,
            "Symbol '%s' found by structural equality but not pointer equality. "
            "This indicates that symbols are not correctly interned. "
            "Stored symbol: %p, Key symbol: %p",
            stored_sym && stored_sym->name ? stored_sym->name : "unknown",
            stored_key, key_obj);
      }
      return 1;
    }
  }
  
  return 0;
}

/** Remove key if present - always returns a new map (COW disabled).
 * Returns the original map if key is not found.
 */
CljMap* map_remove(CljMap *map, ID key) {
  if (!map || !key)
    return map;  // Return original map on error
  CljObject *key_obj = (CljObject*)key;
  if (!key_obj)
    return map;  // Return original map on error
  
  CljMap *map_data = map;
  if (!map_data)
    return map;  // Return original map on error
  
  int index = KV_FIND_INDEX(map_data->data, map_data->count, key_obj);
  if (index < 0) {
    return map;  // Key not found - return original map
  }
  
  // Key found - create new map without this key
  // Allocate new map with same capacity
  size_t struct_size = sizeof(CljMap);
  size_t data_size = (size_t)map_data->capacity * 2 * sizeof(CljObject*);
  CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
  if (!new_map) {
    return map;  // Return original map on OOM
  }
  
  new_map->base.type = CLJ_MAP;
  new_map->base.rc = 1;
  new_map->count = 0;
  new_map->capacity = map_data->capacity;
  
  // Initialize new data array
  for (int i = 0; i < map_data->capacity * 2; i++) {
    new_map->data[i] = NULL;
  }
  
  // Copy all entries except the one at index
  for (int i = 0; i < map_data->count; i++) {
    if (i != index) {
      // Copy entry to new map
      ASSIGN(KV_KEY(new_map->data, new_map->count), KV_KEY(map_data->data, i));
      ASSIGN(KV_VALUE(new_map->data, new_map->count), KV_VALUE(map_data->data, i));
      new_map->count++;
    }
  }
  
  return new_map;  // Return NEW map
}

CljMap* make_map_from_stack(CljObject **pairs, int pair_count) {
    if (pair_count == 0) {
        return make_map(0);
    }
    // Create map with extra capacity to allow adding new keys
    // Capacity should be at least pair_count + some headroom for growth
    int capacity = pair_count * 2;
    if (capacity < 4) capacity = 4;  // Minimum capacity
    CljMap *map = make_map(capacity);
    if (!map) return NULL;
    for (int i = 0; i < pair_count; i++) {
        CljObject *key = KV_KEY(pairs, i);
        CljObject *value = KV_VALUE(pairs, i);
        KV_ASSIGN_PAIR(map->data, i, key, value);
        if (key) RETAIN(key);
        if (value) RETAIN(value);
    }
    map->count = pair_count;
    return map;
}

/** Copy a map with optional type change (DRY helper function).
 * Creates a new map with the same capacity and entries as the source map.
 * 
 * @param src Source map to copy from (must not be NULL)
 * @param new_type Type for the new map (CLJ_MAP or CLJ_TRANSIENT_MAP)
 * @return New map with copied entries, or NULL on error
 */
static CljMap* map_copy(CljMap *src, CljType new_type) {
    if (!src) return NULL;
    
    // Allocate new map with embedded data array
    size_t struct_size = sizeof(CljMap);
    size_t data_size = (size_t)src->capacity * 2 * sizeof(CljObject*);
    CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
    if (!new_map) {
        return NULL;  // OOM
    }
    
    // Initialize new map
    new_map->base.type = new_type;
    new_map->base.rc = 1;
    new_map->count = src->count;
    new_map->capacity = src->capacity;
    
    // Initialize data array
    for (int i = 0; i < src->capacity * 2; i++) {
        new_map->data[i] = NULL;
    }
    
    // Copy all entries with RETAIN
    for (int i = 0; i < src->count * 2; i++) {
        if (src->data[i]) {
            new_map->data[i] = src->data[i];
            RETAIN(src->data[i]);
        }
    }
    
    return new_map;
}

/** Copy map and add/update key-value pairs using transient map (optimized for embedded).
 * This function creates a transient map, adds all bindings in-place, then makes it immutable.
 * This avoids multiple heap allocations and is very efficient for embedded platforms.
 * 
 * @param parent_map Source map to copy from (can be NULL)
 * @param additions Array of key-value pairs to add/update (key at index i*2, value at i*2+1)
 * @param addition_count Number of key-value pairs in additions array
 * @return New immutable map with parent bindings and additions, or NULL on error
 */
CljMap* map_copy_with_additions(CljMap *parent_map, CljObject **additions, int addition_count) {
    // Calculate total capacity needed
    int parent_count = parent_map ? parent_map->count : 0;
    int total_capacity = parent_count + addition_count + 4;  // Extra headroom
    if (total_capacity < 4) total_capacity = 4;
    
    // Allocate transient map with embedded data array (single heap allocation)
    size_t struct_size = sizeof(CljMap);
    size_t data_size = (size_t)total_capacity * 2 * sizeof(CljObject*);
    CljMap *tmap = (CljMap*)malloc(struct_size + data_size);
    if (!tmap) {
        return NULL;  // OOM
    }
    
    // Initialize as transient map (allows in-place mutation)
    tmap->base.type = CLJ_TRANSIENT_MAP;
    tmap->base.rc = 1;
    tmap->count = 0;
    tmap->capacity = total_capacity;
    
    // Initialize data array
    for (int i = 0; i < total_capacity * 2; i++) {
        tmap->data[i] = NULL;
    }
    
    // First, copy all parent bindings in-place
    if (parent_map) {
        for (int i = 0; i < parent_map->count; i++) {
            CljObject *key = KV_KEY(parent_map->data, i);
            CljObject *value = KV_VALUE(parent_map->data, i);
            if (key) {
                // Use conj_map for in-place addition (no heap allocation)
                conj_map(tmap, (ID)key, (ID)value);
            }
        }
    }
    
    // Then, add/update with additions in-place (later additions override earlier ones)
    for (int i = 0; i < addition_count; i++) {
        CljObject *key = additions[i * 2];
        CljObject *value = additions[i * 2 + 1];
        if (key) {
                // Use conj_map for in-place addition/update (no heap allocation)
                conj_map(tmap, (ID)key, (ID)value);
        }
    }
    
    // Make immutable by simply changing the type (no copy needed!)
    tmap->base.type = CLJ_MAP;
    
    return tmap;
}

// === Transient API (Phase 2) ===

/** Convert persistent map to transient. */
CljMap* map_transient(CljMap *map) {
    if (!map) return NULL;
    CljObject *obj = (CljObject*)map;
    if (obj->type != CLJ_MAP) {
        return NULL;
    }
    
    // Use map_copy helper (DRY)
    CljMap *tmap = map_copy(map, CLJ_TRANSIENT_MAP);
    return tmap;
}

/** Associate key->value in transient map (guaranteed in-place). */
CljMap* conj_map(CljMap *tmap, ID key, ID value) {
    // CRITICAL: value can be NULL (nil), which is a valid value in Clojure
    // Only check tmap and key, not value
    if (!tmap || !key) return NULL;
    CljObject *obj = (CljObject*)tmap;
    if (obj->type != CLJ_TRANSIENT_MAP) {
        return NULL;
    }
    
    CljMap *m = tmap;
    if (!m) return NULL;
    
    // Check if key already exists (pointer equality first, then structural)
    CljObject *key_obj = (CljObject*)key;
    for (int i = 0; i < m->count; i++) {
        CljObject *existing_key = m->data[i * 2];
        // Fast path: pointer comparison first (for interned symbols/keywords)
        if (existing_key == key_obj) {
            // Replace existing value
            // CRITICAL: value can be NULL (nil), which is valid
            if (m->data[i * 2 + 1]) {
                RELEASE(m->data[i * 2 + 1]);
            }
            m->data[i * 2 + 1] = value ? RETAIN((CljObject*)value) : NULL;
            return tmap;
        }
        // Fallback: structural comparison for non-interned objects
        if (existing_key && clj_equal((ID)existing_key, (ID)key_obj)) {
            // Replace existing value
            // CRITICAL: value can be NULL (nil), which is valid
            if (m->data[i * 2 + 1]) {
                RELEASE(m->data[i * 2 + 1]);
            }
            m->data[i * 2 + 1] = value ? RETAIN((CljObject*)value) : NULL;
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
    m->data[m->count * 2 + 1] = value ? (CljObject*)value : NULL;
    RETAIN((CljObject*)key);
    // CRITICAL: value can be NULL (nil), which is valid - don't RETAIN NULL
    if (value) {
        RETAIN((CljObject*)value);
    }
    m->count++;
    
    return tmap; // In-place mutation
}

/** Convert transient map back to persistent. */
CljMap* map_persistent(CljMap *tmap) {
    if (!tmap) return NULL;
    CljObject *obj = (CljObject*)tmap;
    if (obj->type != CLJ_TRANSIENT_MAP) {
        return NULL;
    }
    
    CljMap *m = tmap;
    if (!m) return NULL;
    
    // Use map_copy helper (DRY) - Clojure semantics: Create NEW persistent collection
    CljMap *new_map = map_copy(m, CLJ_MAP);
    
    // Original transient becomes "invalidated" (can be implemented later)
    // m->base.type = CLJ_INVALID;  // TODO: Implement invalidation
    
    return new_map; // NEW persistent collection
}

