#ifndef SUBJECTIVE_C_STRINGS_H
#define SUBJECTIVE_C_STRINGS_H

#include "object.h"
#include <stdint.h>
#include <stddef.h>

typedef struct CljString {
    CljObject base;
    uint16_t length;
    char data[];
} CljString;

extern CljString* string_empty_singleton;

#define as_clj_string(obj) ((CljString*)(obj))
#define string_data(str) ((as_clj_string(str))->data)
#define string_length(str) ((as_clj_string(str))->length)

static inline bool is_string(CljObject *obj) {
    if (!obj) return false;
    CljType type = (CljType)obj->type;
    return type == CLJ_STRING;
}

/** @brief Create string from C string (copies data)
 * @param str C string to copy
 * @return New string object (AUTORELEASE'd)
 */
CljString* make_clj_string(const char *str);

/** @brief Create string from C string (copies data)
 * @param str C string to copy
 * @return New string object (AUTORELEASE'd)
 */
CljString* make_string(const char *str);

/** @brief Create empty string buffer with specified capacity
 * @param length Buffer size in bytes
 * @return New string buffer
 */
CljString* make_string_buffer(size_t length);

static inline const char* clj_string_data(CljString *str) {
    return str ? str->data : "";
}

/** @brief Calculate length needed for escaped string
 * @param s String to calculate for
 * @return Required buffer size
 */
size_t escape_string_calc_length(CljString *s);

/** @brief Write escaped string to buffer
 * @param s String to escape
 * @param buffer Output buffer
 * @param offset Current offset (updated by function)
 */
void escape_string_write(CljString *s, char *buffer, size_t *offset);

#endif // SUBJECTIVE_C_STRINGS_H
