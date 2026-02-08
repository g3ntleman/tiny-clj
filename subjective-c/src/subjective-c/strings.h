#ifndef SUBJECTIVE_C_STRINGS_H
#define SUBJECTIVE_C_STRINGS_H

#include "object.h"
#include <stdint.h>
#include <stddef.h>
#ifndef CLJ_FLAG_EXTERNAL_DATA
#define CLJ_FLAG_EXTERNAL_DATA 0x10
#endif

typedef struct CljString {
    CljObject base;
    uint16_t length;
    char data[];
} CljString;

extern CljString* string_empty_singleton;

#define as_clj_string(obj) ((CljString*)(obj))

// Mirrors CljByteArray layout to avoid include cycles via <string.h>/<strings.h>.
typedef struct {
    CljObject base;
    int length;
    uint8_t *data;
} CljByteArrayLayout;

static inline const char* string_data(ID str) {
    CljString *s = as_clj_string(str);
    if (!s) return "";
    if ((s->base.flags & CLJ_FLAG_EXTERNAL_DATA) != 0) {
        CljByteArrayLayout *ba = (CljByteArrayLayout*)s;
        return (const char*)ba->data;
    }
    return s->data;
}

static inline uint16_t string_length(ID str) {
    CljString *s = as_clj_string(str);
    if (!s) return 0;
    if ((s->base.flags & CLJ_FLAG_EXTERNAL_DATA) != 0) {
        CljByteArrayLayout *ba = (CljByteArrayLayout*)s;
        if (!ba) return 0;
        if (ba->length <= 0) return 0;
        if (ba->length > (int)UINT16_MAX) return UINT16_MAX;
        return (uint16_t)ba->length;
    }
    return s->length;
}

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
    return str ? string_data((ID)str) : "";
}

/** @brief Create zero-copy string view over byte-array data (retains byte-array until string is released)
 * @param ba Byte-array object
 * @return String view or NULL on error
 */
CljString* string_view_from_byte_array(ID ba);

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
