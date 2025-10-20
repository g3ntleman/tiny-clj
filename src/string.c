#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "clj_string.h"
#include "object.h"
#include "memory.h"
#include "runtime.h"

// Empty-string singleton: CLJ_STRING with rc=0 and data="" (static storage)
// We need to allocate enough space for CljObject + char* pointer
static char empty_string_singleton_data[sizeof(CljObject) + sizeof(char*)];
static CljObject* empty_string_singleton = (CljObject*)empty_string_singleton_data;
static char* empty_string_data = ""; // Static string data
static void init_empty_string_once(void) {
    static bool initialized = false;
    if (initialized) return;
    empty_string_singleton->type = CLJ_STRING;
    empty_string_singleton->rc = 0;
    // Store pointer to static string data after CljObject header
    char **str_ptr = (char**)((char*)empty_string_singleton + sizeof(CljObject));
    *str_ptr = empty_string_data;
    initialized = true;
}

CljObject* make_string_old(const char *s) {
    if (!s || s[0] == '\0') {
        init_empty_string_once(); // TODO: do not call every time. Create an initialze_* funtction.
        return empty_string_singleton;
    }
    // Allocate CljObject + space for char* pointer
    CljObject *v = (CljObject*)malloc(sizeof(CljObject) + sizeof(char*));
    if (!v) return NULL;
    v->type = CLJ_STRING;
    v->rc = 1;
    // Store string pointer after CljObject header
    char **str_ptr = (char**)((char*)v + sizeof(CljObject));
    *str_ptr = strdup(s);
    
    return v;
}


