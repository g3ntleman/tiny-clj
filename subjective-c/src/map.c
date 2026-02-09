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

// Empty-map singleton: CLJ_MAP_PERSISTENT with rc=SINGLETON_RC, statically initialized
// Note: Flexible array member data[] is not used when capacity=0
static CljPersistentMap map_empty_singleton_data = {
    .base = { .type = CLJ_MAP_PERSISTENT, .rc = SINGLETON_RC },
    .count = 0,
    .capacity = 0
    // data[] flexible array member not initialized (not needed for capacity=0)
};
CljPersistentMap *map_empty_singleton = &map_empty_singleton_data;

// Global sentinel for special values (NOT_FOUND)
CljObject g_not_found_sentinel = { .type = CLJ_NIL, .flags = 0, .rc = SINGLETON_RC };

// === CljValue API (Phase 1: Parallel) ===

/** Create a map with given capacity; capacity<=0 returns empty-map singleton. */
CljPersistentMap* make_map_impl(int capacity, ElementRetention retention) {
  if (capacity <= 0) {
    return map_empty();
  }

  size_t struct_size = sizeof(CljPersistentMap);
  size_t elem_size = 2 * sizeof(CljObject*);
  size_t min_size = struct_size + (size_t)capacity * elem_size;
#if defined(ESP32_BUILD)
  size_t total_size = round_up_to_fam_granularity(min_size);
  int cap_use = (int)((total_size - struct_size) / elem_size);
#else
  size_t total_size = min_size;
  int cap_use = capacity;
#endif

  CljPersistentMap *map = (CljPersistentMap*)alloc(total_size, 1, CLJ_MAP_PERSISTENT);
  if (!map) {
    throw_oom();
  }

  map->base.type = CLJ_MAP_PERSISTENT;
  map->base.flags = (retention == WEAK) ? CLJ_FLAG_WEAK_ELEMENTS : 0;
  map->count = 0;
  map->capacity = cap_use;

#ifdef DEBUG
  for (int i = 0; i < cap_use * 2; i++) {
    map->data[i] = NULL;
  }
#endif

  return map;
}

ID map_get_sentinel(ID map, ID key, ID not_found) {
  CljPersistentMap *map_data = map_backing(map);
  if (map_data) {
    // Note: key can be NULL (nil) - that's a valid key in Clojure!
    CljObject *key_obj = (CljObject*)key;

    MAP_FOR_EACH(map_data, stored_key, value) {
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
 * Returns a usable reference (RC=1). Callers must release if they keep it.
 */
static inline int map_find_index_eq(CljPersistentMap *m, CljObject *target) {
  int found_idx = -1;
  MAP_FOR_EACH(m, k, v) {
    (void)v;
    if (k == target || (k && target && clj_equal(k, target))) {
      found_idx = _i;
      break;
    }
  }
  return found_idx;
}

static CljPersistentMap* map_assoc_core(CljPersistentMap* map, ID key, ID value) {
  CLJ_ASSERT(map && map->base.type == CLJ_MAP_PERSISTENT);
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  // Note: key can be NULL (nil) - that's a valid key in Clojure!

  // Fast path: in-place update when rc==1 and capacity allows.
  if (map->base.rc == 1) {
    int found_idx = map_find_index_eq(map, key_obj);
    if (found_idx >= 0) {
      ASSIGN(KV_VALUE(map->data, found_idx), value_obj);
      return map; // in-place
    }
    if (map->count < map->capacity) {
      int idx = map->count;
      KV_KEY(map->data, idx) = RETAIN(key_obj);
      KV_VALUE(map->data, idx) = RETAIN(value_obj);
      map->count++;
      return map; // in-place append
    }
    // else fall through to COW grow
  }

  // COW path: RC>1 or capacity insufficient → create new map
  int new_capacity = map->capacity;
  if (map->count >= map->capacity) {
    new_capacity = map->capacity * 2;
    if (new_capacity < 4) new_capacity = 4;
  }

  // Allocate new map with embedded data array
  size_t struct_size = sizeof(CljPersistentMap);
  size_t data_size = (size_t)new_capacity * 2 * sizeof(CljObject*);
  CljPersistentMap *new_map = (CljPersistentMap*)alloc(struct_size + data_size, 1, CLJ_MAP_PERSISTENT);
  if (!new_map) {
    throw_oom();
  }

  new_map->base.type = CLJ_MAP_PERSISTENT;
  new_map->count = 0;  // Start with 0, will be set correctly below
  new_map->capacity = new_capacity;

#ifdef DEBUG
  // Initialize new data array (debug-only)
  for (int i = 0; i < new_capacity * 2; i++) {
    new_map->data[i] = NULL;
  }
#endif

  // Copy existing entries
  bool key_found = false;
  int new_idx = 0;

  MAP_FOR_EACH(map, k, v) {
    // Check if key matches (pointer comparison first, then structural)
    bool key_matches = (k == key_obj) || (clj_equal(k, key_obj));

    if (key_matches) {
      // Key found - update value
      KV_KEY(new_map->data, new_idx) = RETAIN(k);
      KV_VALUE(new_map->data, new_idx) = RETAIN(value_obj);
      key_found = true;
      new_idx++;
    } else {
      // Copy existing entry
      KV_KEY(new_map->data, new_idx) = RETAIN(k);
      KV_VALUE(new_map->data, new_idx) = RETAIN(v);
      new_idx++;
    }
  }

  // Add new key if not found
  if (!key_found && new_idx < new_map->capacity) {
    KV_KEY(new_map->data, new_idx) = RETAIN(key_obj);
    KV_VALUE(new_map->data, new_idx) = RETAIN(value_obj);
    new_idx++;
  }

  // Set final count - new_idx is the number of entries copied/updated/added
  new_map->count = new_idx;

  // ASSERT removed for performance - was verifying with map_get after every map_assoc

  return new_map;  // owned (rc=1)

  // Error case: invalid map or wrong type
  return map;  // Return original map on error
}

CljPersistentMap* map_assoc(CljPersistentMap* map, ID key, ID value) {
  return map_assoc_core(map, key, value);
}

static CljPersistentMap* map_assoc_owned(CljPersistentMap* map, ID key, ID value) {
  return map_assoc_core(map, key, value);
}

/** Merge two maps with optional overwrite. */
CljPersistentMap* map_merge(CljPersistentMap* a, CljPersistentMap* b, bool overwrite) {
  if (!a) return b;
  if (!b) return a;

  CljPersistentMap *result = a;

  MAP_FOR_EACH(b, key, value) {
    // nil keys are valid in Clojure maps - don't skip them
    if (!overwrite) {
      ID existing_value = map_get(result, key);
      if (existing_value != NOT_FOUND) {
        continue;
      }
    }
    CljPersistentMap *old_result = result;
    // map_assoc returns RC=1 (owned) when it allocates; otherwise same pointer
    result = map_assoc(result, key, value);
    if (result != old_result) {
      // New map produced; keep using it without extra retain/release churn
    }
    // If result == old_result, map_assoc modified in-place
    // Original 'a' stays alive (caller's responsibility in single-threaded system)
  }

  // Return result (owned rc=1 if a new map was produced, or original 'a' if in-place)
  return result;
}

/** Return a vector of keys (retained). Owned (no AUTORELEASE); caller must RELEASE. */
ID map_keys(ID map) {
  CljPersistentMap *map_data = map_backing(map);
  if (!map_data) return NULL;
  CljPersistentVector* keys_vec = make_vector(map_data->count, STRONG);
  if (!keys_vec)
    return NULL;
  MAP_FOR_EACH(map_data, key, value) {
    CljPersistentVector *next = vector_conj_owned(keys_vec, RETAIN(key));
    if (next != keys_vec) { RELEASE(keys_vec); keys_vec = next; }
  }
  return keys_vec;
}

/** Return a vector of values (retained). Owned (no AUTORELEASE); caller must RELEASE. */
ID map_vals(ID map) {
  CljPersistentMap *map_data = map_backing(map);
  if (!map_data) return NULL;
  CljPersistentVector* vals_vec = make_vector(map_data->count, STRONG);
  if (!vals_vec)
    return NULL;
  MAP_FOR_EACH(map_data, key, val) {
    (void)key;
    if (val) {
      CljPersistentVector *next = vector_conj_owned(vals_vec, RETAIN(val));
      if (next != vals_vec) { RELEASE(vals_vec); vals_vec = next; }
    }
  }
  return vals_vec;
}

/** Return number of key/value pairs. */
int map_count(ID map) {
  CljPersistentMap *map_data = map_backing(map);
  if (!map_data) return 0;
  return map_data->count;
}

/** Append key/value without structural duplicate check (retains both). */
void map_put(CljPersistentMap *map, ID key, ID value) {
  if (!map || !key)
    return;
  CljObject *key_obj = (CljObject*)key;
  CljObject *value_obj = (CljObject*)value;
  if (!key_obj)
    return;
  CljPersistentMap *map_data = map;
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
void map_foreach(ID map, void (*func)(ID, ID)) {
  if (!map || !func)
    return;
  CljPersistentMap *map_data = map_backing(map);
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
int map_contains(ID map, ID key) {
  CljPersistentMap *map_data = map_backing(map);
  if (!map_data)
    return 0;
  // Note: key can be NULL (nil) - that's a valid key in Clojure!
  CljObject *key_obj = (CljObject*)key;

  // Use same logic as map_get: pointer equality first, then structural equality
  MAP_FOR_EACH(map_data, stored_key, value) {
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
 * Returns owned object (rc=1, no AUTORELEASE).
 */
static CljPersistentMap* map_remove_core(CljPersistentMap *map, ID key) {
  CLJ_ASSERT(map);
  // NULL is nil and a valid value for key.

  CljPersistentMap *map_data = map;
  if (!map_data)
    return map;  // Return original map on error

  CljObject *key_obj = (CljObject*)key;
  int index = KV_FIND_INDEX(map_data->data, map_data->count, key_obj);
  if (index == INDEX_NOT_FOUND) {
    return map;  // Key not found - return original map
  }

  // Key found - create new map without this key
  // Allocate new map with same capacity
  size_t struct_size = sizeof(CljPersistentMap);
  size_t data_size = (size_t)map_data->capacity * 2 * sizeof(CljObject*);
  CljPersistentMap *new_map = (CljPersistentMap*)alloc(struct_size + data_size, 1, CLJ_MAP_PERSISTENT);
  if (!new_map) {
    throw_oom();
  }

  new_map->base.type = CLJ_MAP_PERSISTENT;
  new_map->count = 0;
  new_map->capacity = map_data->capacity;

  // Initialize new data array (debug-only)
#ifdef DEBUG
  for (int i = 0; i < map_data->capacity * 2; i++) {
    new_map->data[i] = NULL;
  }
#endif

  // Copy all entries except the one at index
  MAP_FOR_EACH(map_data, k, v) {
    if (_i != index) {
      // Copy entry to new map
      KV_KEY(new_map->data, new_map->count) = RETAIN(k);
      KV_VALUE(new_map->data, new_map->count) = RETAIN(v);
      new_map->count++;
    }
  }

  return new_map;  // owned (rc=1)
}

CljPersistentMap* map_remove(CljPersistentMap *map, ID key) {
  return map_remove_core(map, key);
}

static CljPersistentMap* map_remove_owned(CljPersistentMap *map, ID key) {
  return map_remove_core(map, key);
}

void map_assoc_inplace(CljPersistentMap **map_slot, ID key, ID value) {
  if (!map_slot || !*map_slot) return;
  CljPersistentMap *current = *map_slot;
  CljPersistentMap *updated = map_assoc_owned(current, key, value);
  if (updated && updated != current) {
    RELEASE(current);
    *map_slot = updated;
  }
}

void map_remove_inplace(CljPersistentMap **map_slot, ID key) {
  if (!map_slot || !*map_slot) return;
  CljPersistentMap *current = *map_slot;
  CljPersistentMap *updated = map_remove_owned(current, key);
  if (updated && updated != current) {
    RELEASE(current);
    *map_slot = updated;
  }
}

/** Create a persistent map from variable number of key-value pairs.
 * @param count Number of key-value pairs
 * @param ... Alternating key and value arguments (ID type)
 * @return Persistent map with all key-value pairs (rc=1, or singleton if empty)
 * @note Example: make_map_from_kv(3, key1, val1, key2, val2, key3, val3)
 */
CljPersistentMap* make_map_from_kv(unsigned int count, ...) {
    if (count == 0) return map_empty();

    CljPersistentMap *map = map_empty();

    va_list args;
    va_start(args, count);

    for (unsigned int i = 0; i < count; i++) {
        ID key = va_arg(args, ID);
        ID value = va_arg(args, ID);
        ASSIGN(map, map_assoc(map, key, value));
    }

    va_end(args);

    RETAIN(map);  // rc=1 for caller
    return map;
}

/** Create a persistent map from key-value pairs, terminated by NOT_FOUND.
 * @param first_key First key, or NOT_FOUND for empty map
 * @param ... Alternating value, key, value, ..., terminated by NOT_FOUND
 * @return Persistent map with rc=1
 * @note Example: make_map_kv(key1, val1, key2, val2, NOT_FOUND)
 */
CljPersistentMap* make_map_kv(ID first_key, ...) {
    if (first_key == NOT_FOUND) {
        return RETAIN(map_empty());
    }

    // First pass: count pairs
    va_list args;
    va_start(args, first_key);
    unsigned int count = 1;
    va_arg(args, ID);  // skip first value
    while (va_arg(args, ID) != NOT_FOUND) {
        va_arg(args, ID);  // skip value
        count++;
    }
    va_end(args);

    // Second pass: build map
    CljPersistentMap *map = make_map(count, STRONG);

    va_start(args, first_key);
    ID key = first_key;
    for (unsigned int i = 0; i < count; i++) {
        ID value = va_arg(args, ID);
        map_put(map, key, value);
        if (i < count - 1) {
            key = va_arg(args, ID);
        }
    }
    va_end(args);

    return map;  // rc=1 from make_map
}

CljPersistentMap* make_map_from_stack(CljObject **pairs, int pair_count) {
    if (pair_count == 0) {
        return map_empty();
    }
    // Create map with extra capacity to allow adding new keys
    // Capacity should be at least pair_count + some headroom for growth
    int capacity = MAX(4, pair_count * 2);
    CljPersistentMap *map = make_map(capacity, STRONG);
    for (int i = 0; i < pair_count; i++) {
        CljObject *key = KV_KEY(pairs, i);
        CljObject *value = KV_VALUE(pairs, i);
        KV_ASSIGN_PAIR(map->data, i, key, value);
        RETAIN(key);
        RETAIN(value);
    }
    map->count = pair_count;
    return map;
}

/** Copy a map with optional type change (DRY helper function).
 * Creates a new map with the same capacity and entries as the source map.
 *
 * @param src Source map to copy from (must not be NULL)
 * @param new_type Type for the new map (CLJ_MAP_PERSISTENT or CLJ_MAP_TRANSIENT)
 * @return New map with copied entries, or NULL on error
 */
static CljPersistentMap* map_copy(CljPersistentMap *src) __attribute__((unused));
static CljPersistentMap* map_copy(CljPersistentMap *src) {
    if (!src) return NULL;

    // Allocate new map with embedded data array
    size_t struct_size = sizeof(CljPersistentMap);
    size_t data_size = (size_t)src->capacity * 2 * sizeof(CljObject*);
    CljPersistentMap *new_map = (CljPersistentMap*)alloc(struct_size + data_size, 1, CLJ_MAP_PERSISTENT);
    if (!new_map) {
        throw_oom();
    }

    // Initialize new map
  new_map->base.type = CLJ_MAP_PERSISTENT;
  new_map->count = src->count;
  new_map->capacity = src->capacity;

    // Initialize data array (debug-only)
#ifdef DEBUG
    for (int i = 0; i < src->capacity * 2; i++) {
        new_map->data[i] = NULL;
    }
#endif

    // Copy all entries (nil is a valid value)
    for (int i = 0; i < src->count * 2; i++) {
        new_map->data[i] = RETAIN(src->data[i]);
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
CljPersistentMap* map_copy_with_additions(CljPersistentMap *parent_map, CljObject **additions, int addition_count) {
    // Calculate total capacity needed
    int parent_count = parent_map ? parent_map->count : 0;
    int total_capacity = parent_count + addition_count + 4;  // Extra headroom
    if (total_capacity < 4) total_capacity = 4;

    CljPersistentMap *base = make_map(total_capacity, STRONG);
    if (!base) {
        return NULL;  // OOM
    }

    CljTransientMap *tmap = map_transient(base);
    RELEASE(base);
    if (!tmap) return NULL;

    // First, copy all parent bindings in-place
    if (parent_map) {
        MAP_FOR_EACH(parent_map, key, value) {
            // Use map_conj for in-place addition (no heap allocation)
            map_conj(tmap, key, value);
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

  CljPersistentMap *result = map_persistent(tmap);
  // map_persistent returns a borrowed reference; retain before releasing transient wrapper.
  RETAIN(result);
  RELEASE(tmap);
  return result;
}

// === Transient API (Phase 2) ===

/** Convert persistent map to transient. */
CljTransientMap* map_transient(CljPersistentMap *map) {
    if (!map) return NULL;
    if (map->base.type != CLJ_MAP_PERSISTENT) {
        return NULL;
    }

    CljTransientMap *tmap = (CljTransientMap*)alloc(sizeof(CljTransientMap), 1, CLJ_MAP_TRANSIENT);
    if (!tmap) {
        throw_oom();
        return NULL;
    }
  tmap->base.type = CLJ_MAP_TRANSIENT;
  tmap->backing = (CljPersistentMap*)RETAIN(map);
  return tmap;
}

/** Associate key->value in transient map. */
void map_conj(CljTransientMap *tmap, ID key, ID value) {
    if (!tmap) return;
    if (!tmap->backing) return;

    CljPersistentMap *backing = tmap->backing;
    CljPersistentMap *updated = map_assoc_core(backing, key, value);
    if (updated && updated != backing) {
        ASSIGN(tmap->backing, updated);
    }
}

/** Remove key in transient map. */
void map_dissoc(CljTransientMap *tmap, ID key) {
    if (!tmap) return;
    if (!tmap->backing) return;

    CljPersistentMap *backing = tmap->backing;
    CljPersistentMap *updated = map_remove_core(backing, key);
    if (updated && updated != backing) {
        ASSIGN(tmap->backing, updated);
    }
}

/** Convert transient map back to persistent. */
CljPersistentMap* map_persistent(CljTransientMap *tmap) {
    if (!tmap) return NULL;
    CLJ_ASSERT(tmap->base.type == CLJ_MAP_TRANSIENT);
    return tmap->backing;
}
