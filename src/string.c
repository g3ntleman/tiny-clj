#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "clj_string.h"
#include "CljObject.h"
#include "runtime.h"

// Empty-string singleton: CLJ_STRING with rc=0 and data="" (static storage)
static CljObject empty_string_singleton;
static void init_empty_string_once(void) {
    static bool initialized = false;
    if (initialized) return;
    empty_string_singleton.type = CLJ_STRING;
    empty_string_singleton.rc = 0;
    empty_string_singleton.as.data = (void*)""; // static literal
    initialized = true;
}

CljObject* make_string(const char *s) {
    if (!s || s[0] == '\0') {
        init_empty_string_once();
        return &empty_string_singleton;
    }
    CljObject *v = ALLOC(CljObject, 1);
    if (!v) return NULL;
    v->type = CLJ_STRING;
    v->rc = 1;
    v->as.data = strdup(s);
    return v;
}


