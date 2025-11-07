#include "value.h"
#include "object.h"
#include "memory.h"
#include "exception.h"
#include "symbol.h"
#include "error_messages.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Implementation of large inline functions extracted from value.h

/**
 * @brief Create a character value (extracted from inline)
 * @param codepoint Unicode codepoint
 * @return CljValue character or heap-allocated string for invalid chars
 */
CljValue character(uint32_t codepoint) {
    if (codepoint > CLJ_CHAR_MAX) {
        // Fallback to heap allocation for invalid characters
        CljObject *v = (CljObject*)malloc(sizeof(CljObject) + sizeof(char*));
        if (!v) return NULL;
        v->type = CLJ_STRING;
        v->rc = 1;
        // Store string pointer after CljObject header
        char **str_ptr = (char**)((char*)v + sizeof(CljObject));
        *str_ptr = strdup("?");
        
        return (CljValue)v;
    }
    // Encode as tagged pointer: codepoint << 3 | TAG_CHAR
    return (CljValue)(((uintptr_t)codepoint << TAG_BITS) | TAG_CHAR);
}

/**
 * @brief Create a fixed-point value (extracted from inline)
 * @param value Float value to convert
 * @return CljValue fixed-point representation
 */
CljValue fixed(float value) {
    // Check for overflow before conversion
    if (value > 32767.9998f || value < -32768.0f) {
        throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0, 
                                 "Fixed-point value %.2f exceeds representable range", value);
        return NULL;
    }
    
    int32_t fixed = (int32_t)(value * 8192.0f);
    return (CljValue)(((uintptr_t)fixed << TAG_BITS) | TAG_FIXED);
}

/**
 * @brief Create a string value (extracted from inline)
 * @param str String to create
 * @return CljValue string object
 */
struct CljString* make_string(const char *str) {
    if (!str || str[0] == '\0') {
        return empty_string_singleton;
    }
    
    // Allocate CljString + space for string data + null terminator
    size_t len = strlen(str);
    
    // Assert that string length fits in 16-bit field (max 65,535 characters)
    assert(len <= UINT16_MAX && "String length exceeds 16-bit limit (65,535 chars)");
    
    CljString *s = (CljString*)alloc(sizeof(CljString) + len + 1, 1, CLJ_STRING);
    if (!s) throw_oom(CLJ_STRING);
    
    s->base.type = CLJ_STRING;
    s->base.rc = 1;
    s->length = (uint16_t)len;
    memcpy(s->data, str, len + 1);  // includes null terminator
    
    return s;
}

