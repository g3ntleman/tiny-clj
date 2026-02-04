#ifndef SUBJECTIVE_C_MAP_H
#define SUBJECTIVE_C_MAP_H

#include <stdbool.h>
#include "object.h"
#include "common.h"

typedef struct CljPersistentMap {
    CljObject base;
    int count;
    int capacity;
    CljObject *data[];
} CljPersistentMap;

typedef struct CljTransientMap {
    CljObject base;                 // type == CLJ_MAP_TRANSIENT
    CljPersistentMap *backing;      // always CLJ_MAP_PERSISTENT
} CljTransientMap;

// Type predicate - O(1) check if object is a map
static inline bool is_persistent_map(ID obj) {
    return obj && TAG(obj) == CLJ_MAP_PERSISTENT;
}

static inline bool is_transient_map(ID obj) {
    return obj && TAG(obj) == CLJ_MAP_TRANSIENT;
}

static inline bool is_map(ID obj) {
    if (!obj) return false;
    CljType tag = TAG(obj);
    return tag == CLJ_MAP_PERSISTENT || tag == CLJ_MAP_TRANSIENT;
}

// Type-safe casting
static inline CljPersistentMap* as_persistent_map(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || TAG(obj) == CLJ_MAP_PERSISTENT);
#endif
    return (CljPersistentMap*)obj;
}

static inline CljTransientMap* as_transient_map(ID obj) {
#ifdef DEBUG
    CLJ_ASSERT(obj == NULL || TAG(obj) == CLJ_MAP_TRANSIENT);
#endif
    return (CljTransientMap*)obj;
}

static inline CljPersistentMap* map_backing(ID obj) {
    if (!obj) return NULL;
    CljType tag = TAG(obj);
    if (tag == CLJ_MAP_PERSISTENT) return (CljPersistentMap*)obj;
    if (tag == CLJ_MAP_TRANSIENT) {
        CljTransientMap *tmap = (CljTransientMap*)obj;
        return tmap ? tmap->backing : NULL;
    }
    return NULL;
}

static inline CljPersistentMap* as_map(ID obj) {
    return map_backing(obj);
}

// Map operations
CljPersistentMap* make_map(int capacity);
static inline CljPersistentMap* map_empty(void) {
    extern CljPersistentMap *map_empty_singleton;
    return map_empty_singleton;
}
ID map_get_sentinel(ID map, ID key, ID not_found);
static inline ID map_get(ID map, ID key) {
    return map_get_sentinel(map, key, NOT_FOUND);
}
CljPersistentMap* map_assoc(CljPersistentMap* map, ID key, ID value);
CljPersistentMap* map_merge(CljPersistentMap* a, CljPersistentMap* b, bool overwrite);
ID map_keys(ID map);
ID map_vals(ID map);
int map_count(ID map);
void map_put(CljPersistentMap *map, ID key, ID value);
void map_foreach(ID map, void (*func)(ID, ID));
int map_contains(ID map, ID key);
CljPersistentMap* map_remove(CljPersistentMap *map, ID key);

// In-place helpers for long-lived slots (no AUTORELEASE + releases old map on replacement).
// These functions update the pointer stored in *map_slot and RELEASE the old map
// if a new map instance is produced (grow/COW).
void map_assoc_inplace(CljPersistentMap **map_slot, ID key, ID value);
void map_remove_inplace(CljPersistentMap **map_slot, ID key);
CljTransientMap* make_transient_map_from_kv(unsigned int count, ...);
CljPersistentMap* make_map_kv(ID first_key, ...);  // NOT_FOUND terminated
CljPersistentMap* make_map_from_stack(CljObject **pairs, int pair_count);
CljPersistentMap* map_copy_with_additions(CljPersistentMap *parent_map, CljObject **additions, int addition_count);

// Transient API
CljTransientMap* map_transient(CljPersistentMap *map);
void map_conj(CljTransientMap *tmap, ID key, ID value);
void map_dissoc(CljTransientMap *tmap, ID key);
CljPersistentMap* map_persistent(CljTransientMap *tmap);

#define MAP_FOR_EACH(map, key_var, value_var) \
    for (CljPersistentMap *_map = map_backing((ID)(map)); _map; _map = NULL) \
        for (int _i = 0, _cnt = _map->count; _i < _cnt; ++_i) \
            for (CljObject **_data_ptr = _map->data; _data_ptr; _data_ptr = NULL) \
                for (CljObject *key_var __attribute__((unused)) = _data_ptr[2*_i], *value_var __attribute__((unused)) = _data_ptr[2*_i+1]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_MAP_H
