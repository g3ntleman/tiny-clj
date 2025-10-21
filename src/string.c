#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "object.h"

// Empty-string singleton: CLJ_STRING with rc=0 and data="" (static storage)
// Directly initialized struct - no runtime initialization needed
static struct {
    CljObject base;
    char *str_ptr;
} empty_string_singleton_data = {
    .base = { .type = CLJ_STRING, .rc = 0 },
    .str_ptr = ""
};

CljObject* empty_string_singleton = (CljObject*)&empty_string_singleton_data;

// make_string_old function removed - use make_string from value.h instead


