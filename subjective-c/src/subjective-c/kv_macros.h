#ifndef SUBJECTIVE_C_KV_MACROS_H
#define SUBJECTIVE_C_KV_MACROS_H

#include "object.h"
#include "memory.h"

#define KV_KEY(kv_array, i)   ((kv_array)[2*(i)])
#define KV_VALUE(kv_array, i) ((kv_array)[2*(i) + 1])

#define KV_SET_KEY(kv_array, i, key)     ((kv_array)[2*(i)] = (key))
#define KV_SET_VALUE(kv_array, i, value) ((kv_array)[2*(i) + 1] = (value))

#define KV_ASSIGN_PAIR(kv_array, i, key, value) do { \
    ASSIGN(KV_KEY(kv_array, i), (key)); \
    ASSIGN(KV_VALUE(kv_array, i), (value)); \
} while(0)

#define KV_FOREACH(kv_array, count, key_var, value_var, body) do { \
    for (int _i = 0; _i < (count); _i++) { \
        CljObject* key_var = KV_KEY(kv_array, _i); \
        CljObject* value_var = KV_VALUE(kv_array, _i); \
        if (key_var != NULL) { \
            body \
        } \
    } \
} while(0)

#define KV_FIND_INDEX(kv_array, count, target_key) ({ \
    int _found_index = INDEX_NOT_FOUND; \
    for (int _i = 0; _i < (count); _i++) { \
        if (KV_KEY(kv_array, _i) == (target_key)) { \
            _found_index = _i; \
            break; \
        } \
    } \
    _found_index; \
})

#define KV_CONTAINS(kv_array, count, target_key) (KV_FIND_INDEX(kv_array, count, target_key) != INDEX_NOT_FOUND)

#define KV_COUNT_VALID(kv_array, max_count) ({ \
    int _valid_count = 0; \
    for (int _i = 0; _i < (max_count); _i++) { \
        if (KV_KEY(kv_array, _i) != NULL) { \
            _valid_count++; \
        } \
    } \
    _valid_count; \
})

#endif // SUBJECTIVE_C_KV_MACROS_H
