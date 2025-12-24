#ifndef SUBJECTIVE_C_MAP_H
#define SUBJECTIVE_C_MAP_H

#include <stdbool.h>
#include "object.h"
#include "common.h"

// CljMap struct definition
typedef struct {
    CljObject base;
    int count;
    int capacity;
    CljObject *data[];
} CljMap;

// Type-safe casting
#ifdef DEBUG
static inline CljMap* as_map(ID obj) {
    if (obj) {
        int tag = TAG(obj);
        if (tag == CLJ_MAP || tag == CLJ_MAP_TRANSIENT) {
            return (CljMap*)obj;
        }
    }
    CLJ_ASSERT(0 && "Expected Map type");
    return NULL;
}
#else
// Release: zero-overhead cast
static inline CljMap* as_map(ID obj) {
    return (CljMap*)obj;
}
#endif

// Map operations
CljMap* make_map(int capacity);
static inline CljMap* map_empty(void) {
    extern CljMap *map_empty_singleton;
    return map_empty_singleton;
}
ID map_get(CljMap *map, ID key, ID not_found);
CljMap* map_assoc(CljMap* map, ID key, ID value);
CljMap* map_merge(CljMap* a, CljMap* b, bool overwrite);
ID map_keys(CljMap *map);
ID map_vals(CljMap *map);
int map_count(CljMap *map);
void map_put(CljMap *map, ID key, ID value);
void map_foreach(CljMap *map, void (*func)(ID, ID));
int map_contains(CljMap *map, ID key);
CljMap* map_remove(CljMap *map, ID key);
CljMap* make_transient_map_from_kv(unsigned int count, ...);
CljMap* make_map_kv(ID first_key, ...);  // NOT_FOUND terminated
CljMap* make_map_from_stack(CljObject **pairs, int pair_count);
CljMap* map_copy_with_additions(CljMap *parent_map, CljObject **additions, int addition_count);

// In-place helpers for long-lived slots (no AUTORELEASE + releases old map on replacement)
void map_assoc_inplace(CljMap **map_slot, ID key, ID value);
void map_remove_inplace(CljMap **map_slot, ID key);

// Transient API
CljMap* map_transient(CljMap *map);
CljMap* map_conj(CljMap *tmap, ID key, ID value);
CljMap* map_persistent(CljMap *tmap);

#define MAP_FOR_EACH(map, key_var, value_var) \
    for (int _i = 0, _cnt = map_count(map); (map) && _i < _cnt; ++_i) \
        for (CljObject **_data_ptr = (map)->data; _data_ptr; _data_ptr = NULL) \
            for (CljObject *key_var __attribute__((unused)) = _data_ptr[2*_i], *value_var __attribute__((unused)) = _data_ptr[2*_i+1]; _data_ptr; _data_ptr = NULL)

#endif // SUBJECTIVE_C_MAP_H
