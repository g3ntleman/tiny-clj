#include "subjective-c.h"
#include "value.h"

// Temporarily include interpreter headers until migration completes
#include "memory.h"
#include "exception.h"
#include "error_messages.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

CljValue character(uint32_t codepoint) {
    if (codepoint > CLJ_CHAR_MAX) {
        // Represent unprintable/invalid characters as a string.
        // Use the standard string constructor so allocation + release are correct and tracked.
        return (CljValue)make_string("?");
    }
    return (CljValue)(((uintptr_t)codepoint << TAG_BITS) | TAG_CHAR);
}

CljValue fixed(float value) {
    if (value > 32767.9998f || value < -32768.0f) {
        return throw_exception_formatted(EXCEPTION_ARITHMETIC, __FILE__, __LINE__, 0,
                                 "Fixed-point value %.2f exceeds representable range", value);
    }
    int32_t fixed = (int32_t)(value * 8192.0f);
    return (CljValue)(((uintptr_t)fixed << TAG_BITS) | TAG_FIXED);
}
