#include "value.h"
#include "object.h"
#include "exception.h"
#include <stdlib.h>
#include <string.h>

// Implementation of large inline functions extracted from value.h

/**
 * @brief Create a character value (extracted from inline)
 * @param codepoint Unicode codepoint
 * @return CljValue character or heap-allocated string for invalid chars
 */
CljValue make_char_impl(uint32_t codepoint) {
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
CljValue make_fixed_impl(float value) {
    int32_t fixed = (int32_t)(value * 8192.0f);
    // Saturierung zu ±32767.9998 (±268435455 in Fixed-Point)
    if (fixed > 268435455) fixed = 268435455;
    if (fixed < -268435456) fixed = -268435456;
    return (CljValue)(((uintptr_t)fixed << TAG_BITS) | TAG_FIXED);
}

/**
 * @brief Create a string value (extracted from inline)
 * @param str String to create
 * @return CljValue string object
 */
CljValue make_string_impl(const char *str) {
    if (!str || str[0] == '\0') {
        return (CljValue)empty_string_singleton;
    }
    // Allocate CljObject + space for char* pointer
    CljObject *v = (CljObject*)malloc(sizeof(CljObject) + sizeof(char*));
    if (!v) return NULL;
    v->type = CLJ_STRING;
    v->rc = 1;
    // Store string pointer after CljObject header
    char **str_ptr = (char**)((char*)v + sizeof(CljObject));
    *str_ptr = strdup(str);
    
    return (CljValue)v;
}

/**
 * @brief Create a symbol value (extracted from inline)
 * @param name Symbol name
 * @param ns Namespace (can be NULL)
 * @return CljValue symbol object
 */
CljValue make_symbol_impl(const char *name, const char *ns) {
    if (!name) {
        throw_exception_formatted("ArgumentError", __FILE__, __LINE__, 0,
                "make_symbol: name cannot be NULL");
        return NULL;
    }
    
    // Range check for name length (keep for safety)
    if (strlen(name) >= SYMBOL_NAME_MAX_LEN) {
        throw_exception_formatted("ArgumentError", __FILE__, __LINE__, 0,
                "Symbol name '%s' exceeds maximum length of %d characters", 
                name, SYMBOL_NAME_MAX_LEN - 1);
        return NULL;
    }
    
    // Use malloc directly instead of ALLOC macro
    CljSymbol *sym = (CljSymbol*)malloc(sizeof(CljSymbol));
    if (!sym) {
        throw_exception_formatted("OutOfMemoryError", __FILE__, __LINE__, 0,
                "Failed to allocate memory for symbol '%s'", name);
        return NULL;
    }
    
    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = 1;
    
    // Store pointer to string literal (will be strdup'd for heap symbols)
    sym->name = strdup(name);
    if (!sym->name) {
        free(sym);
        throw_exception_formatted("OutOfMemoryError", __FILE__, __LINE__, 0,
                "Failed to duplicate string for symbol '%s'", name);
        return NULL;
    }
    
    // Get or create namespace object
    if (ns) {
        sym->ns = ns_get_or_create(ns, NULL);  // NULL for file parameter
        if (!sym->ns) {
            free(sym);
            throw_exception_formatted("NamespaceError", __FILE__, __LINE__, 0,
                    "Failed to create namespace '%s' for symbol '%s'", ns, name);
            return NULL;
        }
        // Namespace is already retained by ns_get_or_create
    } else {
        sym->ns = NULL;  // No namespace
    }
    
    return (CljValue)sym;
}
