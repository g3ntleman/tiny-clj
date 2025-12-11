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
    return obj && obj->type == CLJ_STRING;
}

CljString* make_clj_string(const char *str);
CljString* make_string(const char *str);
CljString* make_string_buffer(size_t length);

static inline const char* clj_string_data(CljString *str) {
    return str ? str->data : "";
}

CljString* pr_str(ID v);
CljString* print_str(ID v);
CljString* to_string(ID v);
CljString* to_string_with_escape(ID v, bool escape_strings);

bool strings_set_special_form_rendering(bool as_tags);
bool strings_get_special_form_rendering(void);
void strings_register_special_form(const char *name);
void strings_clear_special_forms(void);

#endif // SUBJECTIVE_C_STRINGS_H
