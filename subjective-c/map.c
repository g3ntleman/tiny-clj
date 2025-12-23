#include "map.h"
#include "kv_macros.h"
#include "object.h"
#include "vector.h"
#include "memory.h"
#include "value.h"
#include "exception.h"
#include "types.h"  // For SINGLETON_RC
#include "common.h"  // For CLJ_ASSERT
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

// Empty-map singleton: CLJ_MAP with rc=SINGLETON_RC, statically initialized
// Note: Flexible array member data[] is not used when capacity=0
static CljMap map_empty_singleton_data = {
    .base = { .type = CLJ_MAP, .rc = SINGLETON_RC },
    .count = 0,
    .capacity = 0
    // data[] flexible array member not initialized (not needed for capacity=0)
};
CljMap *map_empty_singleton = &map_empty_singleton_data;

// Global sentinel for special values (NOT_FOUND)
CljObject g_not_found_sentinel = { .type = CLJ_NIL, .flags = 0, .rc = SINGLETON_RC };

// === CljValue API (Phase 1: Parallel) ===

/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljMap* make_map(int capacity) {
  if (capacity <= 0) {
    return map_empty();
  }

  // Allocate struct + data array in ONE malloc
  size_t struct_size = sizeof(CljMap);
  size_t data_size = (size_t)capacity * 2 * sizeof(CljObject*);
  size_t total_size = struct_size + data_size;

  CljMap *map = (CljMap*)malloc(total_size);
  if (!map) {
    throw_oom();
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

ID map_get(CljMap *map, ID key, ID not_found) {
  if (map) {
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    CljObject *key_obj = (CljObject*)key;

    MAP_FOR_EACH(map, stored_key, value) {
      // Happy path: pointer comparison first (for interned symbols and nil keys)
      if (stored_key == key_obj) {
        return value;  // Direct return, no jumps
      }
      // Fallback: structural comparison for non-interned objects
      // OPTIMIZATION: Allow structural equality for symbols to avoid intern_symbol_global in hot path
      // This is acceptable because clj_equal is fast for symbols (string comparison)
      if (stored_key && key_obj && clj_equal(stored_key, key_obj)) {
        return value;
      }
    }
  }

  return not_found;
}



/** Associate key->value with COW: RC=1 → in-place mutation, RC>1 → COW.
 */
static CljMap* map_assoc_impl(CljMap* map, ID key, ID value, bool autorelease_result) {
  if (map && TAG(map) == CLJ_MAP) {
    CljObject *key_obj = (CljObject*)key;
    CljObject *value_obj = (CljObject*)value;
    // Note: key can be NULL (nil) - that's a valid key in Clojure!

#if 1
  // COW HOT-PATH: RC=1 → in-place mutation
  if (map->base.rc == 1) {
    // Check if key exists - update value (linear search necessary)
    // OPTIMIZATION: Fast path for pointer equality (interned symbols/keywords)
    int found_idx = -1;
    MAP_FOR_EACH(map, k, v) {
      (void)v;  // unused
      // Fast path: pointer comparison first (for interned symbols/keywords)
      if (k == key_obj) {
        found_idx = _i;
        break;
      }
      // Fallback: structural comparison for non-interned objects
      // Note: If key_obj is NULL, k must also be NULL to match (already handled above)
      if (k && key_obj && clj_equal(k, key_obj)) {
        found_idx = _i;
        break;
      }
    }
    if (found_idx >= 0) {
      // Key found: update in-place (no branches after this)
      ASSIGN(KV_VALUE(map->data, found_idx), value_obj);
      return map;  // Return SAME map
    }

    // Key not found: add new entry if capacity allows
    if (map->count < map->capacity) {
      // Direct in-place mutation (no branches after this)
      int idx = map->count;
      ASSIGN(KV_KEY(map->data, idx), key_obj);
      ASSIGN(KV_VALUE(map->data, idx), value_obj);
      map->count++;
      return map;  // Return SAME map
    }

    // Out of capacity - need to grow (fall through to COW path)
  }
#endif

  // COW path: RC>1 or capacity insufficient → create new map
  int new_capacity = map->capacity;
  if (map->count >= map->capacity) {
    new_capacity = map->capacity * 2;
    if (new_capacity < 4) new_capacity = 4;
  }

  // Allocate new map with embedded data array
  size_t struct_size = sizeof(CljMap);
  size_t data_size = (size_t)new_capacity * 2 * sizeof(CljObject*);
  CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
  if (!new_map) {
    throw_oom();
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

  MAP_FOR_EACH(map, k, v) {
    // Fast path: pointer comparison first (for interned symbols/keywords)
    if (k == key_obj) {
      // Key found - update value
      ASSIGN(KV_KEY(new_map->data, new_idx), k);
      ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
      key_found = true;
      new_idx++;
    } else if (k && key_obj && clj_equal(k, key_obj)) {
      // Key found - update value (structural comparison)
      // Note: If key_obj is NULL, k must also be NULL to match (already handled above)
      ASSIGN(KV_KEY(new_map->data, new_idx), k);
      ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
      key_found = true;
      new_idx++;
    } else {
      // Copy existing entry
      ASSIGN(KV_KEY(new_map->data, new_idx), k);
      ASSIGN(KV_VALUE(new_map->data, new_idx), v);
      new_idx++;
    }
  }

  // Add new key if not found
  if (!key_found && new_idx < new_map->capacity) {
    ASSIGN(KV_KEY(new_map->data, new_idx), key_obj);
    ASSIGN(KV_VALUE(new_map->data, new_idx), value_obj);
    new_idx++;
  }

  // Set final count - new_idx is the number of entries copied/updated/added
  new_map->count = new_idx;

  // ASSERT removed for performance - was verifying with map_get after every map_assoc

  if (autorelease_result) {
    return AUTORELEASE(new_map);
  }
  return new_map;  // owned (rc=1)
  }

  // Error case: invalid map or wrong type
  return map;  // Return original map on error
}

CljMap* map_assoc(CljMap* map, ID key, ID value) {
  return map_assoc_impl(map, key, value, true);
}

static CljMap* map_assoc_owned(CljMap* map, ID key, ID value) {
  return map_assoc_impl(map, key, value, false);
}

/** Merge two maps with optional overwrite. */
CljMap* map_merge(CljMap* a, CljMap* b, bool overwrite) {
  if (!a) return b;
  if (!b) return a;

  CljMap *result = a;

  MAP_FOR_EACH(b, key, value) {
    if (!key) continue;
    if (!overwrite) {
      ID existing_value = map_get(result, key, NOT_FOUND);
      if (existing_value != NOT_FOUND) {
        continue;
      }
    }
    result = map_assoc(result, key, value);
  }

  return result;
}

/** Return a vector of keys (retained). */
ID map_keys(CljMap *map) {
  if (!map)
    return NULL;
  CljMap *map_data = map;
  if (!map_data)
    return NULL;
  CljVector* keys_vec = make_vector(map_data->count, CLJ_VECTOR);
  if (!keys_vec)
    return NULL;
  MAP_FOR_EACH(map_data, key, value) {
    if (key) {
      keys_vec = vector_conj(keys_vec, RETAIN(key));
    }
  }
  return keys_vec;
}

/** Return a vector of values (retained). */
ID map_vals(CljMap *map) {
  if (!map)
    return NULL;
  CljMap *map_data = map;
  if (!map_data)
    return NULL;
  CljVector* vals_vec = make_vector(map_data->count, CLJ_VECTOR);
  if (!vals_vec)
    return NULL;
  MAP_FOR_EACH(map_data, key, val) {
    (void)key;  // unused
    if (val) {
      vals_vec = vector_conj(vals_vec, RETAIN(val));
    }
  }
  return vals_vec;
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
             { func(key, value); });
}

/** Return 1 if key exists (pointer equality fast-path, fallback to structural equality).
 *
 * NOTE: This function is consistent with map_get() which also uses structural equality
 * as a fallback. If all symbols are correctly interned, pointer equality should be
 * sufficient. The structural equality fallback is for edge cases where symbols might
 * not be interned (e.g., during parsing or in test code).
 */
int map_contains(CljMap *map, ID key) {
  if (!map)
    return 0;
  // Note: key can be NULL (nil) - that's a valid key in Clojure!
  CljObject *key_obj = (CljObject*)key;

  // Use same logic as map_get: pointer equality first, then structural equality
  MAP_FOR_EACH(map, stored_key, value) {
    (void)value;  // unused
    // Fast path: pointer comparison first (for interned symbols and nil keys)
    if (stored_key == key_obj) {
      return 1;
    }
    // Fallback: structural comparison for non-interned objects
    if (stored_key && key_obj && clj_equal(stored_key, key_obj)) {
      // Note: For symbols, structural equality without pointer equality indicates interning issue
      // This is a warning condition but we still return true for correctness
      return 1;
    }
  }

  return 0;
}

/** Remove key if present - always returns a new map (COW disabled).
 * Returns the original map if key is not found.
 */
static CljMap* map_remove_impl(CljMap *map, ID key, bool autorelease_result) {
  if (!map || !key)
    return map;  // Return original map on error
  CljObject *key_obj = (CljObject*)key;
  if (!key_obj)
    return map;  // Return original map on error

  CljMap *map_data = map;
  if (!map_data)
    return map;  // Return original map on error

  int index = KV_FIND_INDEX(map_data->data, map_data->count, key_obj);
  if (index == INDEX_NOT_FOUND) {
    return map;  // Key not found - return original map
  }

  // Key found - create new map without this key
  // Allocate new map with same capacity
  size_t struct_size = sizeof(CljMap);
  size_t data_size = (size_t)map_data->capacity * 2 * sizeof(CljObject*);
  CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
  if (!new_map) {
    throw_oom();
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
  MAP_FOR_EACH(map_data, k, v) {
    if (_i != index) {
      // Copy entry to new map
      ASSIGN(KV_KEY(new_map->data, new_map->count), k);
      ASSIGN(KV_VALUE(new_map->data, new_map->count), v);
      new_map->count++;
    }
  }

  if (autorelease_result) {
    return AUTORELEASE(new_map);
  }
  return new_map;  // owned (rc=1)
}

CljMap* map_remove(CljMap *map, ID key) {
  return map_remove_impl(map, key, true);
}

static CljMap* map_remove_owned(CljMap *map, ID key) {
  return map_remove_impl(map, key, false);
}

void map_assoc_inplace(CljMap **map_slot, ID key, ID value) {
  if (!map_slot || !*map_slot) return;
  CljMap *current = *map_slot;
  CljMap *updated = map_assoc_owned(current, key, value);
  if (updated && updated != current) {
    RELEASE(current);
    *map_slot = updated;
  }
}

void map_remove_inplace(CljMap **map_slot, ID key) {
  if (!map_slot || !*map_slot) return;
  CljMap *current = *map_slot;
  CljMap *updated = map_remove_owned(current, key);
  if (updated && updated != current) {
    RELEASE(current);
    *map_slot = updated;
  }
}

/** Create a transient map from variable number of key-value pairs.
 * @param count Number of key-value pairs
 * @param ... Alternating key and value arguments (ID type)
 * @return Transient map with all key-value pairs, or NULL on error
 * @note Example: make_transient_map_from_kv(3, key1, val1, key2, val2, key3, val3)
 */
CljMap* make_transient_map_from_kv(unsigned int count, ...) {
    if (count == 0) {
        CljMap *empty = map_empty();
        CljMap *tmap = map_transient(empty);
        return tmap;
    }

    CljMap *map = make_map(count);  // throws OOM exception if allocation fails

    CljMap *tmap = map_transient(map);
    RELEASE(map);
    // map_transient throws OOM exception if allocation fails, so no NULL check needed

    va_list args;
    va_start(args, count);

    for (unsigned int i = 0; i < count; i++) {
        ID key = va_arg(args, ID);
        ID value = va_arg(args, ID);
        map_conj(tmap, key, value);
    }

    va_end(args);

    return tmap;
}

CljMap* make_map_from_stack(CljObject **pairs, int pair_count) {
    if (pair_count == 0) {
        return map_empty();
    }
    // Create map with extra capacity to allow adding new keys
    // Capacity should be at least pair_count + some headroom for growth
    int capacity = MAX(4, pair_count * 2);
    CljMap *map = make_map(capacity);
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
 * @param new_type Type for the new map (CLJ_MAP or CLJ_MAP_TRANSIENT)
 * @return New map with copied entries, or NULL on error
 */
static CljMap* map_copy(CljMap *src, CljType new_type) {
    if (!src) return NULL;

    // Allocate new map with embedded data array
    size_t struct_size = sizeof(CljMap);
    size_t data_size = (size_t)src->capacity * 2 * sizeof(CljObject*);
    CljMap *new_map = (CljMap*)malloc(struct_size + data_size);
    if (!new_map) {
        throw_oom();
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

    // Copy all entries with ASSIGN (handles RETAIN automatically)
    for (int i = 0; i < src->count * 2; i++) {
        if (src->data[i]) {
            ASSIGN(new_map->data[i], src->data[i]);
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
    tmap->base.type = CLJ_MAP_TRANSIENT;
    tmap->base.rc = 1;
    tmap->count = 0;
    tmap->capacity = total_capacity;

    // Initialize data array
    for (int i = 0; i < total_capacity * 2; i++) {
        tmap->data[i] = NULL;
    }

    // First, copy all parent bindings in-place
    if (parent_map) {
        MAP_FOR_EACH(parent_map, key, value) {
            if (key) {
                // Use map_conj for in-place addition (no heap allocation)
                map_conj(tmap, key, value);
            }
        }
    }

    // Then, add/update with additions in-place (later additions override earlier ones)
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    for (int i = 0; i < addition_count; i++) {
        CljObject *key = KV_KEY(additions, i);
        CljObject *value = KV_VALUE(additions, i);
        // Use map_conj for in-place addition/update (no heap allocation)
        // map_conj now handles NULL keys correctly
        map_conj(tmap, key, value);
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
    CljMap *tmap = map_copy(map, CLJ_MAP_TRANSIENT);
    return tmap;
}

/** Associate key->value in transient map (guaranteed in-place). */
CljMap* map_conj(CljMap *tmap, ID key, ID value) {
    // CRITICAL: value can be NULL (nil), which is a valid value in Clojure
    // CRITICAL: key can be NULL (nil), which is a valid key in Clojure
    // Only check tmap, not key or value
    if (!tmap) return NULL;
    CljObject *obj = (CljObject*)tmap;

    // Assertion: Only transient maps (and persistent maps with RC=1 in COW cases) can be mutated
    CLJ_ASSERT((obj->type == CLJ_MAP_TRANSIENT || obj->type == CLJ_MAP) && "map_conj requires transient map or persistent map with RC=1");
    // In COW cases, persistent maps with RC=1 can be mutated, but map_conj is primarily for transient maps
    if (obj->type == CLJ_MAP) {
        CLJ_ASSERT(obj->rc == 1 && "map_conj on persistent map requires RC=1 for COW");
    }

    // Runtime check: map_conj is primarily for transient maps
    if (obj->type != CLJ_MAP_TRANSIENT) {
        // Allow persistent maps with RC=1 for COW cases (but this should be rare)
        if (obj->type == CLJ_MAP && obj->rc == 1) {
            // COW case: persistent map with RC=1 can be mutated in-place
            // This is allowed but not recommended - use transient maps instead
        } else {
            return NULL;  // Not a transient map and not a COW case
        }
    }

    CljMap *m = tmap;
    if (!m) return NULL;

    // Check if key already exists (pointer equality first, then structural)
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    CljObject *key_obj = (CljObject*)key;

    bool key_found = false;
    for (int i = 0; i < m->count; i++) {
        CljObject *existing_key = m->data[i * 2];
        // Fast path: pointer comparison first (for interned symbols/keywords)
        if (existing_key == key_obj) {
            // Replace existing value - ASSIGN handles RETAIN/RELEASE automatically
            ASSIGN(m->data[i * 2 + 1], value ? (CljObject*)value : NULL);
            key_found = true;
            return tmap;
        }
        // Fallback: structural comparison for non-interned objects
        if (existing_key && key_obj && clj_equal(existing_key, key_obj)) {
            // Replace existing value - ASSIGN handles RETAIN/RELEASE automatically
            ASSIGN(m->data[i * 2 + 1], value ? (CljObject*)value : NULL);
            key_found = true;
            return tmap;
        }
    }

    // If key was not found, we should add it (if capacity allows)
    // This can happen if intern_symbol returns different pointers (symbol interning issue)
    // We don't abort here, but return NULL if capacity is exceeded
    if (!key_found && m->count >= m->capacity) {
        // Capacity exceeded - cannot add new key-value pair
        return NULL;
    }

    // Add new key-value pair
    if (m->count >= m->capacity) {
        // Cannot grow embedded arrays - transient maps have fixed capacity
        // This is a limitation of the embedded array approach
        return NULL;  // Out of capacity
    }

    ASSIGN(m->data[m->count * 2], key);
    ASSIGN(m->data[m->count * 2 + 1], value ? (CljObject*)value : NULL);
    m->count++;

    // ASSERT removed for performance - was verifying with map_get after every map_conj

    return tmap; // In-place mutation
}

/** Convert transient map back to persistent. */
CljMap* map_persistent(CljMap *tmap) {
    if (!tmap) return NULL;
    CljObject *obj = (CljObject*)tmap;
    if (obj->type != CLJ_MAP_TRANSIENT) {
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

