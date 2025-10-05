/*
 * Key-Value Macros Header
 * 
 * Type-safe macros for efficient access to interleaved key-value arrays:
 * - KV_KEY/KV_VALUE: Access keys and values in key-value pairs
 * - KV_SET_KEY/KV_SET_VALUE: Set keys and values
 * - KV_FOREACH: Iterate over key-value pairs
 * - KV_FIND_INDEX: Find index of a key
 * - KV_CONTAINS: Check if key exists
 * - Used for environment bindings and meta-data registry
 */

#ifndef KV_MACROS_H
#define KV_MACROS_H

// Access the key at index i in an interleaved key-value array
#define KV_KEY(kv_array, i)   ((kv_array)[2*(i)])

// Access the value at index i in an interleaved key-value array
#define KV_VALUE(kv_array, i) ((kv_array)[2*(i) + 1])

// Set key-value pair at index i in an interleaved array
#define KV_SET_KEY(kv_array, i, key)   ((kv_array)[2*(i)] = (key))
#define KV_SET_VALUE(kv_array, i, value) ((kv_array)[2*(i) + 1] = (value))

// Set both key and value at index i
#define KV_SET_PAIR(kv_array, i, key, value) do { \
    (kv_array)[2*(i)] = (key); \
    (kv_array)[2*(i) + 1] = (value); \
} while(0)

// Iterate over all key-value pairs in an interleaved array
#define KV_FOREACH(kv_array, count, key_var, value_var, body) do { \
    for (int _i = 0; _i < (count); _i++) { \
        CljObject* key_var = KV_KEY(kv_array, _i); \
        CljObject* value_var = KV_VALUE(kv_array, _i); \
        if (key_var != NULL) { \
            body \
        } \
    } \
} while(0)

// Find index of a key in an interleaved array (returns -1 if not found)
#define KV_FIND_INDEX(kv_array, count, target_key) ({ \
    int _found_index = -1; \
    for (int _i = 0; _i < (count); _i++) { \
        if (KV_KEY(kv_array, _i) == (target_key)) { \
            _found_index = _i; \
            break; \
        } \
    } \
    _found_index; \
})

// Check if a key exists in an interleaved array
#define KV_CONTAINS(kv_array, count, target_key) (KV_FIND_INDEX(kv_array, count, target_key) >= 0)

// Get the number of valid key-value pairs (skips NULL keys)
#define KV_COUNT_VALID(kv_array, max_count) ({ \
    int _valid_count = 0; \
    for (int _i = 0; _i < (max_count); _i++) { \
        if (KV_KEY(kv_array, _i) != NULL) { \
            _valid_count++; \
        } \
    } \
    _valid_count; \
})

#endif
