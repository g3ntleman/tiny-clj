#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "object.h"
#include "strings.h"

// Empty string singleton with CljString layout
static struct {
    CljObject base;
    uint16_t length;
    char data[1];  // Just the null terminator
} empty_string_data = {
    .base = { .type = CLJ_STRING, .rc = 0 },
    .length = 0,
    .data = ""
};

CljString* empty_string_singleton = (CljString*)&empty_string_data;

// make_string_old function removed - use make_string from value.h instead


