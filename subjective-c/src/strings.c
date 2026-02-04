#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "object.h"
#include "strings.h"
#include "memory.h"
#include "types.h"  // For SINGLETON_RC
#include "exception.h"  // For throw_exception
#include "common.h"  // For CLJ_ASSERT

// Empty string singleton with CljString layout
static struct {
    CljObject base;
    uint16_t length;
    char data[1];  // Just the null terminator
} empty_string_data = {
    .base = { .type = CLJ_STRING, .rc = SINGLETON_RC },
    .length = 0,
    .data = ""
};

CljString* string_empty_singleton = (CljString*)&empty_string_data;

static void* make_string_like(const char *str, CljType type, bool allow_empty_singleton) {
    const char *source = (str && str[0] != '\0') ? str : "";
    if (!source[0] && allow_empty_singleton && type == CLJ_STRING) {
        return string_empty_singleton;
    }

    size_t len = strlen(source);
    assert(len <= UINT16_MAX && "String length exceeds 16-bit limit (65,535 chars)");

    CljString *s = (CljString*)alloc(sizeof(CljString) + len + 1, 1, type);

    s->base.type = type;
    s->length = (uint16_t)len;
    memcpy(s->data, source, len + 1);

    return s;
}

static CljString* make_string_impl(const char *str) {
    return (CljString*)make_string_like(str, CLJ_STRING, true);
}

CljString* make_clj_string(const char *str) {
    return make_string_impl(str);
}

/**
 * @brief Create a string value
 * @param str String to create
 * @return CljString object (caller must release)
 */
CljString* make_string(const char *str) {
    return make_string_impl(str);
}

CljString* make_string_buffer(size_t length) {
    // Return empty string singleton if length is 0
    if (length == 0) {
        return string_empty_singleton;
    }

    // Check that length fits in 16-bit field (max 65,535 characters)
    if (length > UINT16_MAX) {
        throw_exception(EXCEPTION_RUNTIME, "make_string_buffer: length exceeds maximum (65,535)",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Allocate CljString + space for string data + null terminator
    CljString *s = (CljString*)alloc(sizeof(CljString) + length + 1, 1, CLJ_STRING);
    if (!s) {
        throw_oom();
        return NULL;
    }

    s->base.type = CLJ_STRING;
    s->length = (uint16_t)length;
    // Zero-initialize the buffer (including null terminator)
    memset(s->data, 0, length + 1);

    return s;
}

// Helper: Calculate length of string with escaping
// Exported for use in object.c (DRY principle)
size_t escape_string_calc_length(CljString *s) {
    size_t len = s->length;
    size_t escaped_len = len;
    const char *data = s->data;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '"' || data[i] == '\\') {
            escaped_len++;  // Each needs a backslash
        }
    }
    return escaped_len + 2;  // +2 for quotes
}

// Helper: Write string with escaping to buffer
// Exported for use in object.c (DRY principle)
void escape_string_write(CljString *s, char *buffer, size_t *offset) {
    buffer[*offset] = '"';
    (*offset)++;

    const char *data = s->data;
    size_t len = s->length;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '"' || data[i] == '\\') {
            buffer[*offset] = '\\';
            (*offset)++;
        }
        buffer[*offset] = data[i];
        (*offset)++;
    }

    buffer[*offset] = '"';
    (*offset)++;
}


