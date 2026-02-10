/*
 * Native string functions for Tiny-CLJ
 * 
 * Extracted from builtins.c for better code organization.
 * Provides: str, subs, trim, upper-case, lower-case, pad-left,
 * index-of, last-index-of, reverse (string), format
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "object.h"
#include "builtins_strings.h"
#include "value.h"
#include "memory.h"
#include "strings.h"
#include "to_string.h"
#include "exception.h"
#include "builtins.h"
#include "symbol.h"
#include "mini_format.h"

static bool format_ensure_capacity(char **buffer, size_t *buf_size, char **out, size_t needed) {
    size_t used = (size_t)(*out - *buffer);
    size_t required = used + needed + 1; // +1 for null terminator
    if (required <= *buf_size) return true;

    size_t new_size = *buf_size ? *buf_size : 64;
    while (new_size < required) {
        size_t next = new_size * 2;
        if (next <= new_size) {
            new_size = required;
            break;
        }
        new_size = next;
    }

    char *new_buf = CLJ_REALLOC(*buffer, new_size);
    if (!new_buf) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                       __FILE__, __LINE__, 0);
        return false;
    }

    *buffer = new_buf;
    *buf_size = new_size;
    *out = new_buf + used;
    return true;
}

// ============================================================================
// STRING FUNCTIONS
// ============================================================================

// String concatenation (variadic)
ID native_str(ID *args, unsigned int argc) {
    if (argc == 0) {
        return make_string("");
    }

    // Optimization: If only one argument and it's already a string, return it directly
    if (argc == 1 && args[0] && TAG(args[0]) == CLJ_STRING) {
        return args[0];
    }

    // Calculate total length
    size_t total_len = 0;
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i] && TAG(args[i]) == CLJ_STRING) {
            total_len += string_length(args[i]);
        } else {
            CljString *s = to_string(args[i]);
            if (s) {
                total_len += string_length(s);
            }
        }
    }

    // Allocate CljString buffer directly
    CljString *result = make_string_buffer(total_len);
    char *buffer = result->data;
    size_t offset = 0;

    // Concatenate all strings
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i] && TAG(args[i]) == CLJ_STRING) {
            size_t len = string_length(args[i]);
            if (len > 0) {
                memcpy(buffer + offset, string_data(args[i]), len);
                offset += len;
            }
        } else {
            CljString *s = to_string(args[i]);
            if (s) {
                size_t len = string_length(s);
                if (len > 0) {
                    memcpy(buffer + offset, string_data(s), len);
                    offset += len;
                }
            }
        }
    }
    buffer[offset] = '\0';

    return (CljObject*)result;
}

// String substring: (subs s start) or (subs s start end)
ID native_subs(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        char error_msg[256];
        mini_snprintf(error_msg, sizeof(error_msg),
                "subs requires exactly 2 or 3 argument%s, got %u",
                argc == 2 ? "" : "s", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
    }

    ID str_arg = args[0];
    ID start_arg = args[1];
    ID end_arg = argc == 3 ? args[2] : NULL;

    // Validate string argument
    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "subs requires a string as first argument");
        return NULL;
    }

    // Validate start index
    if (!start_arg || TAG(start_arg) != CLJ_INT) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "subs requires a number as start index");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    int start = AS_FIXNUM(start_arg);

    // Cache string length to avoid multiple calls
    int str_len = string_length(str);
    int end;

    // Determine end index: if not provided, use string length
    if (end_arg) {
        if (TAG(end_arg) != CLJ_INT) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "subs requires a number as end index");
            return NULL;
        }
        end = AS_FIXNUM(end_arg);
    } else {
        end = str_len;
    }

    // Bounds validation
    if (start < 0) {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "subs start index %d is negative", start);
        return NULL;
    }

    if (end > str_len) {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "subs end index %d is greater than string length %d", end, str_len);
        return NULL;
    }

    if (start > end) {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                                  "subs start index %d is greater than end index %d", start, end);
        return NULL;
    }

    // Calculate substring length
    int substr_len = end - start;

    // Special case: empty substring (start == end)
    if (substr_len == 0) {
        return string_empty_singleton;
    }

    // Create CljString directly without temporary C-string
    CljString *result = make_string_buffer(substr_len);
    const char *str_data = string_data(str);
    memcpy(result->data, str_data + start, substr_len);
    result->data[substr_len] = '\0';

    return AUTORELEASE(result);
}

// String trim: (trim s) - removes whitespace from both ends
ID native_trim(ID *args, unsigned int argc) {
    if (argc != 1) {
        char error_msg[256];
        mini_snprintf(error_msg, sizeof(error_msg),
                "trim requires exactly 1 argument, got %u", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
    }

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "trim requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    int str_len = string_length(str);

    // Find start (skip leading whitespace)
    int start = 0;
    while (start < str_len && (str_data[start] == ' ' || str_data[start] == '\t' ||
                               str_data[start] == '\n' || str_data[start] == '\r')) {
        start++;
    }

    // Find end (skip trailing whitespace)
    int end = str_len - 1;
    while (end >= start && (str_data[end] == ' ' || str_data[end] == '\t' ||
                            str_data[end] == '\n' || str_data[end] == '\r')) {
        end--;
    }

    // Calculate trimmed length
    int trimmed_len = end - start + 1;

    // Special case: empty string or all whitespace
    if (trimmed_len <= 0) {
        return string_empty_singleton;
    }

    // Create CljString directly without temporary C-string
    CljString *result = make_string_buffer(trimmed_len);
    memcpy(result->data, str_data + start, trimmed_len);
    result->data[trimmed_len] = '\0';
    return AUTORELEASE(result);
}

// String upper-case: (upper-case s) - converts string to upper-case
ID native_upper_case(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "upper-case");

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "upper-case requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);

    if (str_len == 0) {
        return string_empty_singleton;
    }

    // Convert to upper-case directly in CljString buffer
    CljString *result = make_string_buffer(str_len);
    for (uint16_t i = 0; i < str_len; i++) {
        result->data[i] = (char)toupper((unsigned char)str_data[i]);
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// String lower-case: (lower-case s) - converts string to lower-case
ID native_lower_case(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "lower-case");

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "lower-case requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);

    if (str_len == 0) {
        return string_empty_singleton;
    }

    // Convert to lower-case directly in CljString buffer
    CljString *result = make_string_buffer(str_len);
    for (uint16_t i = 0; i < str_len; i++) {
        result->data[i] = (char)tolower((unsigned char)str_data[i]);
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// String pad-left: (pad-left s width pad-char)
// Returns s padded on the left with pad-char to width characters.
// If s is already >= width, returns s unchanged.
ID native_pad_left(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 3, "pad-left");
    
    ID str_arg = args[0];
    ID width_arg = args[1];
    ID pad_char_arg = args[2];
    
    // Validate string argument
    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "pad-left requires a string as first argument");
        return NULL;
    }
    
    // Validate width argument
    if (!is_fixnum(width_arg)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "pad-left requires an integer width as second argument");
        return NULL;
    }
    
    // Validate pad-char argument
    if (!pad_char_arg || TAG(pad_char_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "pad-left requires a string as third argument (pad-char)");
        return NULL;
    }
    
    CljString *str = as_clj_string(str_arg);
    int width = as_fixnum(width_arg);
    CljString *pad_str = as_clj_string(pad_char_arg);
    
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);
    
    // No padding needed
    if (str_len >= (uint16_t)width) {
        return str_arg;  // Return unchanged
    }
    
    // Get pad character (first char of pad-char string)
    const char *pad_data = string_data(pad_str);
    uint16_t pad_len = string_length(pad_str);
    char pad_char = (pad_len > 0) ? pad_data[0] : ' ';
    
    // Calculate padding needed
    int padding = width - str_len;
    
    // Create result string
    CljString *result = make_string_buffer(width);
    
    // Fill padding
    for (int i = 0; i < padding; i++) {
        result->data[i] = pad_char;
    }
    
    // Copy original string
    memcpy(result->data + padding, str_data, str_len);
    result->data[width] = '\0';
    
    return AUTORELEASE(result);
}

// String index-of: (index-of s value) or (index-of s value from-index)
ID native_index_of(ID *args, unsigned int argc) {
    CHECK_ARITY_RANGE(argc, 2, 3, "index-of");

    ID str_arg = args[0];
    ID value_arg = args[1];
    ID from_index_arg = argc == 3 ? args[2] : NULL;

    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "index-of requires a string as first argument");
        return NULL;
    }

    if (!value_arg || TAG(value_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "index-of requires a string as second argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    CljString *value = as_clj_string(value_arg);
    if (!str || !value) return NULL;

    const char *str_data = string_data(str);
    const char *value_data = clj_string_data(value);
    int str_len = (int)string_length(str);
    int value_len = (int)string_length(value);

    int from_index = 0;
    if (from_index_arg) {
        if (TAG(from_index_arg) != CLJ_INT) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "index-of requires an integer as from-index");
            return NULL;
        }
        from_index = as_fixnum(from_index_arg);
    }

    if (from_index < 0 || from_index > str_len) {
        return NULL;
    }

    if (value_len == 0) {
        return fixnum(from_index);
    }

    if (value_len > str_len || from_index >= str_len) {
        return NULL;
    }

    int last_start = str_len - value_len;
    for (int i = from_index; i <= last_start; i++) {
        if (memcmp(str_data + i, value_data, (size_t)value_len) == 0) {
            return fixnum(i);
        }
    }

    return NULL;
}

// String last-index-of: (last-index-of s value) or (last-index-of s value from-index)
ID native_last_index_of(ID *args, unsigned int argc) {
    CHECK_ARITY_RANGE(argc, 2, 3, "last-index-of");

    ID str_arg = args[0];
    ID value_arg = args[1];
    ID from_index_arg = argc == 3 ? args[2] : NULL;

    // Validate string argument
    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "last-index-of requires a string as first argument");
        return NULL;
    }

    // Validate value argument
    if (!value_arg || TAG(value_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "last-index-of requires a string as second argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    CljString *value = as_clj_string(value_arg);
    if (!str || !value) return NULL;

    const char *str_data = string_data(str);
    const char *value_data = clj_string_data(value);
    uint16_t str_len = string_length(str);
    uint16_t value_len = string_length(value);

    // Handle empty value
    if (value_len == 0) {
        // Empty string always found at end
        return fixnum(str_len);
    }

    // Validate from-index if provided
    int from_index = str_len - 1; // Default: search from end
    if (from_index_arg) {
        if (TAG(from_index_arg) != CLJ_INT) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "last-index-of requires an integer as from-index");
            return NULL;
        }
        from_index = as_fixnum(from_index_arg);
        if (from_index < 0) from_index = 0;
        if (from_index >= str_len) from_index = str_len - 1;
    }

    // Search backwards from from_index
    for (int i = from_index; i >= 0; i--) {
        if (i + value_len > str_len) continue;

        // Check if substring matches
        bool match = true;
        for (uint16_t j = 0; j < value_len; j++) {
            if (str_data[i + j] != value_data[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            return fixnum(i);
        }
    }

    // Not found
    return NULL; // nil
}

// String reverse: (reverse s) - reverses a string (not lists)
ID native_string_reverse(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "reverse");

    ID str_arg = args[0];

    // Handle nil
    if (!str_arg) {
        return NULL; // nil -> nil
    }

    // Validate string argument
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "reverse requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *str_data = string_data(str);
    uint16_t str_len = string_length(str);

    if (str_len == 0) {
        return string_empty_singleton;
    }

    // Reverse string directly in CljString buffer
    CljString *result = make_string_buffer(str_len);
    for (uint16_t i = 0; i < str_len; i++) {
        result->data[i] = str_data[str_len - 1 - i];
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// String format: (format fmt & args) - format string with arguments
ID native_format(ID *args, unsigned int argc) {
    CHECK_ARITY_MIN(argc, 1, "format");

    // First argument must be a string (format string)
    if (TAG(args[0]) != CLJ_STRING) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "format first argument must be a string",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    CljString *fmt_str = (CljString*)args[0];
    if (!fmt_str || TAG(args[0]) != CLJ_STRING) return NULL;
    const char *fmt_data = string_data(fmt_str);
    size_t fmt_len = string_length(fmt_str);

    // Allocate buffer for formatted string (start with reasonable size)
    size_t buf_size = 256;
    char *buffer = CLJ_MALLOC(buf_size);
    if (!buffer) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to allocate buffer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Format arguments based on format string
    if (argc == 1) {
        // No arguments, just copy format string
        char *out = buffer;
        if (!format_ensure_capacity(&buffer, &buf_size, &out, fmt_len)) {
            CLJ_FREE(buffer);
            return NULL;
        }
        memcpy(out, fmt_data, fmt_len);
        out[fmt_len] = '\0';
    } else {
        // We need to handle variadic arguments
        // For simplicity, support common format specifiers: %d, %f, %s
        // This is a simplified version - full implementation would need to parse format string
        size_t idx = 0;
        char *out = buffer;
        int arg_idx = 1;

        while (idx < fmt_len) {
            if (fmt_data[idx] == '%' && (idx + 1) < fmt_len) {
                char spec = fmt_data[idx + 1];
                if (spec != '%' && arg_idx >= (int)argc) {
                    if (!format_ensure_capacity(&buffer, &buf_size, &out, 2)) {
                        CLJ_FREE(buffer);
                        return NULL;
                    }
                    *out++ = '%';
                    *out++ = spec;
                    idx += 2;
                    continue;
                }
                idx += 2; // Consume '%' + spec

                switch (spec) {
                    case 'd': {
                        // Integer
                        int val = AS_FIXNUM(args[arg_idx]);
                        int n = 0;
                        while (true) {
                            size_t used = (size_t)(out - buffer);
                            size_t avail = buf_size - used;
                            n = mini_snprintf(out, avail, "%d", val);
                            if (n < 0) {
                                throw_exception(EXCEPTION_RUNTIME, "format: failed to format integer",
                                               __FILE__, __LINE__, 0);
                                CLJ_FREE(buffer);
                                return NULL;
                            }
                            if ((size_t)n < avail) {
                                out += n;
                                break;
                            }
                            if (!format_ensure_capacity(&buffer, &buf_size, &out,
                                                       (n > 0) ? (size_t)n : avail)) {
                                CLJ_FREE(buffer);
                                return NULL;
                            }
                        }
                        arg_idx++;
                        break;
                    }
                    case 'f': {
                        // Float
                        float val = (TAG(args[arg_idx]) == CLJ_INT) ?
                                   (float)AS_FIXNUM(args[arg_idx]) :
                                   as_fixed((CljValue)args[arg_idx]);
                        int n = 0;
                        while (true) {
                            size_t used = (size_t)(out - buffer);
                            size_t avail = buf_size - used;
                            n = mini_snprintf(out, avail, "%f", val);
                            if (n < 0) {
                                throw_exception(EXCEPTION_RUNTIME, "format: failed to format float",
                                               __FILE__, __LINE__, 0);
                                CLJ_FREE(buffer);
                                return NULL;
                            }
                            if ((size_t)n < avail) {
                                out += n;
                                break;
                            }
                            if (!format_ensure_capacity(&buffer, &buf_size, &out,
                                                       (n > 0) ? (size_t)n : avail)) {
                                CLJ_FREE(buffer);
                                return NULL;
                            }
                        }
                        arg_idx++;
                        break;
                    }
                    case 's': {
                        // String
                        CljString *str = (TAG(args[arg_idx]) == CLJ_STRING)
                            ? (CljString*)args[arg_idx]
                            : print_str(args[arg_idx]);
                        if (str) {
                            const char *sdata = string_data(str);
                            size_t slen = string_length(str);
                            if (!format_ensure_capacity(&buffer, &buf_size, &out, slen)) {
                                CLJ_FREE(buffer);
                                return NULL;
                            }
                            if (slen > 0) {
                                memcpy(out, sdata, slen);
                                out += slen;
                            }
                        }
                        arg_idx++;
                        break;
                    }
                    case '%': {
                        // Literal %
                        if (!format_ensure_capacity(&buffer, &buf_size, &out, 1)) {
                            CLJ_FREE(buffer);
                            return NULL;
                        }
                        *out++ = '%';
                        break;
                    }
                    default: {
                        // Unknown specifier, copy as-is
                        if (!format_ensure_capacity(&buffer, &buf_size, &out, 2)) {
                            CLJ_FREE(buffer);
                            return NULL;
                        }
                        *out++ = '%';
                        *out++ = spec;
                        break;
                    }
                }
            } else {
                if (!format_ensure_capacity(&buffer, &buf_size, &out, 1)) {
                    CLJ_FREE(buffer);
                    return NULL;
                }
                *out++ = fmt_data[idx++];
            }
        }
        *out = '\0';
    }

    // Create string object from buffer
    CljString *result = make_string(buffer);
    CLJ_FREE(buffer);

    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to create string",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    return AUTORELEASE(result);
}

// ============================================================================
// clojure.string native lookup (used by :native stubs)
// ============================================================================

typedef struct {
    CljSymbol *clojure_symbol;
    BuiltinFn native_func;
} BuiltinsStringsNativeFunctionEntry;

static const BuiltinsStringsNativeFunctionEntry builtins_strings_native_function_table[] = {
    {&sym_trim_data.sym, native_trim},
    {&sym_upper_case_data.sym, native_upper_case},
    {&sym_lower_case_data.sym, native_lower_case},
    {&sym_pad_left_data.sym, native_pad_left},
    {&sym_last_index_of_data.sym, native_last_index_of},
    {&sym_string_reverse_data.sym, native_string_reverse},
    {NULL, NULL}
};

BuiltinFn builtins_strings_native_function_lookup(CljSymbol *symbol) {
    if (!symbol) return NULL;

    const char *cname = symbol->cname;
    const char *ns_name = (symbol->ns_name) ? symbol->ns_name->cname : NULL;

    char qualified_name[128];
    if (ns_name) {
        mini_snprintf(qualified_name, sizeof(qualified_name), "%s/%s", ns_name, cname);
    }

    if (cname && strcmp(cname, "index-of") == 0) {
        if (!ns_name || strcmp(ns_name, "clojure.string") == 0) {
            return native_index_of;
        }
    }

    for (int i = 0; builtins_strings_native_function_table[i].clojure_symbol != NULL; i++) {
        CljSymbol *table_sym = builtins_strings_native_function_table[i].clojure_symbol;
        if (!table_sym) continue;

        if (table_sym == symbol) {
            return builtins_strings_native_function_table[i].native_func;
        }

        if (!cname || !table_sym->cname) continue;

        const char *table_ns = table_sym->ns_name ? table_sym->ns_name->cname : NULL;

        if (ns_name) {
            if (table_ns && strcmp(table_ns, ns_name) == 0 && strcmp(table_sym->cname, cname) == 0) {
                return builtins_strings_native_function_table[i].native_func;
            }
            if (!table_ns && strcmp(table_sym->cname, qualified_name) == 0) {
                return builtins_strings_native_function_table[i].native_func;
            }
            if (!table_ns && strcmp(ns_name, "clojure.core") == 0 && strcmp(table_sym->cname, cname) == 0) {
                return builtins_strings_native_function_table[i].native_func;
            }
        } else {
            if (strcmp(table_sym->cname, cname) == 0) {
                return builtins_strings_native_function_table[i].native_func;
            }
        }
    }

    return NULL;
}
