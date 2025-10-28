#ifndef TINY_CLJ_STRINGS_H
#define TINY_CLJ_STRINGS_H

#include "object.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief CljString structure with flexible array member
 * 
 * Layout:
 * - CljObject base (4 bytes): type + rc
 * - uint16_t length (2 bytes): string length
 * - char data[] (flexible): null-terminated string data
 * 
 * Total: 6 bytes + string data + null terminator
 */
typedef struct CljString {
    CljObject base;      // type + rc (4 bytes)
    uint16_t length;     // String length (2 bytes) - max 65,535 chars
    char data[];         // Flexible array member (null-terminated)
} CljString;

// Forward declaration for empty string singleton
extern CljString* empty_string_singleton;

/**
 * @brief Accessor macros for CljString
 */
#define as_clj_string(obj) ((CljString*)(obj))
#define string_data(str) ((str)->data)
#define string_length(str) ((str)->length)

/**
 * @brief Check if a CljObject is a string
 */
static inline bool is_clj_string(CljObject *obj) {
    return obj && obj->type == CLJ_STRING;
}

/**
 * @brief Create a new CljString
 * @param str C-string to copy
 * @return New CljString object (caller must release)
 */
CljString* make_clj_string(const char *str);

/**
 * @brief Get string length
 * @param str CljString object
 * @return String length
 */
static inline uint16_t clj_string_length(CljString *str) {
    return str ? str->length : 0;
}

/**
 * @brief Get string data as C-string
 * @param str CljString object
 * @return C-string (null-terminated)
 */
static inline const char* clj_string_data(CljString *str) {
    return str ? str->data : "";
}

// String representation functions
/** Return newly allocated C-string representation (caller frees). */
char* pr_str(CljObject *v);
char* to_string(CljObject *v);

#endif // TINY_CLJ_STRINGS_H