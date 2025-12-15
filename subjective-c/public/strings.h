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

CljString* make_clj_string(const char *str);
CljString* make_string(const char *str);
CljString* make_string_buffer(size_t length);

static inline const char* clj_string_data(CljString *str) {
    return str ? str->data : "";
}

// String escape functions (exported for use in object.c - DRY principle)
size_t escape_string_calc_length(CljString *s);
void escape_string_write(CljString *s, char *buffer, size_t *offset);

#endif // SUBJECTIVE_C_STRINGS_H
