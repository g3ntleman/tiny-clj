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
extern CljString* string_empty_singleton;

/**
 * @brief Accessor macros for CljString
 */
#define as_clj_string(obj) ((CljString*)(obj))
#define string_data(str) ((as_clj_string(str))->data)
#define string_length(str) ((as_clj_string(str))->length)

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
 * @brief Create a string value
 * @param str String to create
 * @return CljString object (caller must release)
 */
CljString* make_string(const char *str);

/**
 * @brief Create a string buffer of given length (null-terminated, all zeros)
 * @param length Length of the string buffer (must be <= UINT16_MAX)
 * @return CljString object with zero-initialized data (caller must release)
 */
CljString* make_string_buffer(size_t length);


/**
 * @brief Get string data as C-string
 * @param str CljString object
 * @return C-string (null-terminated)
 */
static inline const char* clj_string_data(CljString *str) {
    return str ? str->data : "";
}

// String representation functions
/** Return CljString representation (caller must release). */
CljString* pr_str(ID v);
CljString* print_str(ID v);
CljString* to_string(ID v);
/** Internal function with escape_strings parameter. */
CljString* to_string_with_escape(ID v, bool escape_strings);

bool strings_set_special_form_rendering(bool as_tags);
bool strings_get_special_form_rendering(void);

#endif // TINY_CLJ_STRINGS_H
