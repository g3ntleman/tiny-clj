/*
 * Native string functions for Tiny-CLJ
 * 
 * Extracted from builtins.c for better code organization.
 * Provides: str, subs, trim, upper-case, lower-case, pad-left,
 * last-index-of, reverse (string), format
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

    // Concatenate all strings
    for (unsigned int i = 0; i < argc; i++) {
        if (args[i] && TAG(args[i]) == CLJ_STRING) {
            strcat(buffer, string_data(args[i]));
        } else {
            CljString *s = to_string(args[i]);
            if (s) {
                strcat(buffer, string_data(s));
            }
        }
    }

    return (CljObject*)result;
}

// String substring: (subs s start) or (subs s start end)
ID native_subs(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
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
        snprintf(error_msg, sizeof(error_msg),
                "trim requires exactly 1 argument, got %u", argc);
        throw_exception(EXCEPTION_ARITY, error_msg, __FILE__, __LINE__, 0);
        return NULL;
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

// String index-of: (index-of s value from-index)
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

    CljString *str   = as_clj_string(str_arg);
    CljString *value = as_clj_string(value_arg);
    if (!str || !value) return NULL;

    const char *str_data   = string_data(str);
    const char *value_data = clj_string_data(value);
    uint16_t str_len   = string_length(str);
    uint16_t value_len = string_length(value);

    // Empty needle always found at from-index (or 0)
    if (value_len == 0) {
        int start = 0;
        if (from_index_arg && TAG(from_index_arg) == CLJ_INT) {
            start = as_fixnum(from_index_arg);
            if (start < 0) start = 0;
            if (start > str_len) start = str_len;
        }
        return fixnum(start);
    }

    int from_index = 0;
    if (from_index_arg) {
        if (TAG(from_index_arg) != CLJ_INT) {
            throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                      "index-of requires an integer as from-index");
            return NULL;
        }
        from_index = as_fixnum(from_index_arg);
        if (from_index < 0) from_index = 0;
    }

    for (int i = from_index; value_len <= str_len && i <= (int)(str_len - value_len); i++) {
        bool match = true;
        for (uint16_t j = 0; j < value_len; j++) {
            if (str_data[i + j] != value_data[j]) {
                match = false;
                break;
            }
        }
        if (match) return fixnum(i);
    }

    return NULL; // nil — not found
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

    // Allocate buffer for formatted string (start with reasonable size)
    size_t buf_size = 256;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to allocate buffer",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    // Format arguments based on format string
    if (argc == 1) {
        // No arguments, just copy format string
        snprintf(buffer, buf_size, "%s", fmt_str->data);
    } else {
        // We need to handle variadic arguments
        // For simplicity, support common format specifiers: %d, %f, %s
        // This is a simplified version - full implementation would need to parse format string
        const char *fmt = fmt_str->data;
        const char *p = fmt;
        char *out = buffer;
        size_t remaining = buf_size - 1;
        int arg_idx = 1;

        while (*p && arg_idx < (int)argc && remaining > 0) {
            if (*p == '%' && *(p + 1) != '\0') {
                p++; // Skip '%'
                char spec = *p++;

                switch (spec) {
                    case 'd': {
                        // Integer
                        int val = AS_FIXNUM(args[arg_idx]);
                        int n = snprintf(out, remaining, "%d", val);
                        if (n < 0 || n >= (int)remaining) {
                            // Buffer too small, reallocate
                            size_t used = out - buffer;
                            buf_size *= 2;
                            buffer = realloc(buffer, buf_size);
                            if (!buffer) {
                                throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                               __FILE__, __LINE__, 0);
                                return NULL;
                            }
                            out = buffer + used;
                            remaining = buf_size - used - 1;
                            n = snprintf(out, remaining, "%d", val);
                        }
                        out += n;
                        remaining -= n;
                        arg_idx++;
                        break;
                    }
                    case 'f': {
                        // Float
                        float val = (TAG(args[arg_idx]) == CLJ_INT) ?
                                   (float)AS_FIXNUM(args[arg_idx]) :
                                   as_fixed((CljValue)args[arg_idx]);
                        int n = snprintf(out, remaining, "%f", val);
                        if (n < 0 || n >= (int)remaining) {
                            size_t used = out - buffer;
                            buf_size *= 2;
                            buffer = realloc(buffer, buf_size);
                            if (!buffer) {
                                throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                               __FILE__, __LINE__, 0);
                                return NULL;
                            }
                            out = buffer + used;
                            remaining = buf_size - used - 1;
                            n = snprintf(out, remaining, "%f", val);
                        }
                        out += n;
                        remaining -= n;
                        arg_idx++;
                        break;
                    }
                    case 's': {
                        // String
                        CljString *str = (TAG(args[arg_idx]) == CLJ_STRING) ? (CljString*)args[arg_idx] : NULL;
                        if (!str) {
                            // Try to convert to string
                            CljString *str_repr = print_str(args[arg_idx]);
                            if (str_repr) {
                                int n = snprintf(out, remaining, "%s", string_data(str_repr));
                                if (n < 0 || n >= (int)remaining) {
                                    size_t used = out - buffer;
                                    buf_size *= 2;
                                    buffer = realloc(buffer, buf_size);
                                    if (!buffer) {
                                        throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                                       __FILE__, __LINE__, 0);
                                        return NULL;
                                    }
                                    out = buffer + used;
                                    remaining = buf_size - used - 1;
                                    n = snprintf(out, remaining, "%s", string_data(str_repr));
                                }
                                out += n;
                                remaining -= n;
                            }
                        } else {
                            int n = snprintf(out, remaining, "%s", str->data);
                            if (n < 0 || n >= (int)remaining) {
                                size_t used = out - buffer;
                                buf_size *= 2;
                                buffer = realloc(buffer, buf_size);
                                if (!buffer) {
                                    throw_exception(EXCEPTION_RUNTIME, "format: failed to reallocate buffer",
                                                   __FILE__, __LINE__, 0);
                                    return NULL;
                                }
                                out = buffer + used;
                                remaining = buf_size - used - 1;
                                n = snprintf(out, remaining, "%s", str->data);
                            }
                            out += n;
                            remaining -= n;
                        }
                        arg_idx++;
                        break;
                    }
                    case '%': {
                        // Literal %
                        *out++ = '%';
                        remaining--;
                        break;
                    }
                    default: {
                        // Unknown specifier, copy as-is
                        *out++ = '%';
                        *out++ = spec;
                        remaining -= 2;
                        break;
                    }
                }
            } else {
                *out++ = *p++;
                remaining--;
            }
        }
        *out = '\0';
    }

    // Create string object from buffer
    CljString *result = make_string(buffer);
    free(buffer);

    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "format: failed to create string",
                       __FILE__, __LINE__, 0);
        return NULL;
    }

    return AUTORELEASE(result);
}

static inline bool string_is_ascii_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline bool string_is_newline_char(char c) {
    return c == '\n' || c == '\r';
}

// clojure.string/blank?: true for nil, empty strings, and all-whitespace strings.
ID native_blank_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "blank?");

    ID str_arg = args[0];
    if (!str_arg) {
        return clj_true;
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "blank? requires a string or nil");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    uint16_t str_len = string_length(str);
    if (str_len == 0) {
        return clj_true;
    }

    const char *data = string_data(str);
    for (uint16_t i = 0; i < str_len; i++) {
        if (!string_is_ascii_whitespace(data[i])) {
            return clj_false;
        }
    }
    return clj_true;
}

// clojure.string/capitalize: upper-case first char, lower-case the remainder.
ID native_capitalize(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "capitalize");

    ID str_arg = args[0];
    if (!str_arg) {
        return NULL;  // nil -> nil
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "capitalize requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    uint16_t str_len = string_length(str);
    if (str_len == 0) {
        return string_empty_singleton;
    }

    const char *data = string_data(str);
    CljString *result = make_string_buffer(str_len);
    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "capitalize failed to allocate result", __FILE__, __LINE__, 0);
        return NULL;
    }

    result->data[0] = (char)toupper((unsigned char)data[0]);
    for (uint16_t i = 1; i < str_len; i++) {
        result->data[i] = (char)tolower((unsigned char)data[i]);
    }
    result->data[str_len] = '\0';
    return AUTORELEASE(result);
}

// clojure.string/includes?: true if s contains substr.
ID native_includes_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "includes?");

    ID str_arg = args[0];
    ID substr_arg = args[1];
    if (!str_arg || TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "includes? requires a string as first argument");
        return NULL;
    }
    if (!substr_arg || TAG(substr_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "includes? requires a string as second argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    CljString *substr = as_clj_string(substr_arg);
    const char *str_data = string_data(str);
    const char *substr_data = string_data(substr);
    uint16_t str_len = string_length(str);
    uint16_t substr_len = string_length(substr);

    if (substr_len == 0) {
        return clj_true;
    }
    if (substr_len > str_len) {
        return clj_false;
    }

    for (uint16_t i = 0; i <= (uint16_t)(str_len - substr_len); i++) {
        if (memcmp(str_data + i, substr_data, substr_len) == 0) {
            return clj_true;
        }
    }
    return clj_false;
}

// clojure.string/starts-with?: true if s starts with prefix.
ID native_starts_with_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "starts-with?");

    ID str_arg = args[0];
    ID prefix_arg = args[1];
    if (!str_arg || !prefix_arg) {
        return clj_false;
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "starts-with? requires a string as first argument");
        return NULL;
    }
    if (TAG(prefix_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "starts-with? requires a string as second argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    CljString *prefix = as_clj_string(prefix_arg);
    const char *str_data = string_data(str);
    const char *prefix_data = string_data(prefix);
    uint16_t str_len = string_length(str);
    uint16_t prefix_len = string_length(prefix);

    if (prefix_len > str_len) {
        return clj_false;
    }
    if (prefix_len == 0) {
        return clj_true;
    }
    return (memcmp(str_data, prefix_data, prefix_len) == 0) ? clj_true : clj_false;
}

// clojure.string/ends-with?: true if s ends with suffix.
ID native_ends_with_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "ends-with?");

    ID str_arg = args[0];
    ID suffix_arg = args[1];
    if (!str_arg || !suffix_arg) {
        return clj_false;
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ends-with? requires a string as first argument");
        return NULL;
    }
    if (TAG(suffix_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "ends-with? requires a string as second argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    CljString *suffix = as_clj_string(suffix_arg);
    const char *str_data = string_data(str);
    const char *suffix_data = string_data(suffix);
    uint16_t str_len = string_length(str);
    uint16_t suffix_len = string_length(suffix);

    if (suffix_len > str_len) {
        return clj_false;
    }
    if (suffix_len == 0) {
        return clj_true;
    }
    const char *tail = str_data + (str_len - suffix_len);
    return (memcmp(tail, suffix_data, suffix_len) == 0) ? clj_true : clj_false;
}

// clojure.string/triml: remove leading ASCII whitespace.
ID native_triml(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "triml");

    ID str_arg = args[0];
    if (!str_arg) {
        return NULL;  // nil -> nil
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "triml requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *data = string_data(str);
    uint16_t str_len = string_length(str);
    if (str_len == 0) {
        return string_empty_singleton;
    }

    uint16_t start = 0;
    while (start < str_len && string_is_ascii_whitespace(data[start])) {
        start++;
    }
    if (start == 0) {
        return str_arg;
    }
    if (start >= str_len) {
        return string_empty_singleton;
    }

    uint16_t new_len = (uint16_t)(str_len - start);
    CljString *result = make_string_buffer(new_len);
    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "triml failed to allocate result", __FILE__, __LINE__, 0);
        return NULL;
    }
    memcpy(result->data, data + start, new_len);
    result->data[new_len] = '\0';
    return AUTORELEASE(result);
}

// clojure.string/trimr: remove trailing ASCII whitespace.
ID native_trimr(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "trimr");

    ID str_arg = args[0];
    if (!str_arg) {
        return NULL;  // nil -> nil
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "trimr requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *data = string_data(str);
    uint16_t str_len = string_length(str);
    if (str_len == 0) {
        return string_empty_singleton;
    }

    uint16_t end = str_len;
    while (end > 0 && string_is_ascii_whitespace(data[end - 1])) {
        end--;
    }
    if (end == str_len) {
        return str_arg;
    }
    if (end == 0) {
        return string_empty_singleton;
    }

    CljString *result = make_string_buffer(end);
    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "trimr failed to allocate result", __FILE__, __LINE__, 0);
        return NULL;
    }
    memcpy(result->data, data, end);
    result->data[end] = '\0';
    return AUTORELEASE(result);
}

// clojure.string/trim-newline: remove trailing CR/LF bytes.
ID native_trim_newline(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "trim-newline");

    ID str_arg = args[0];
    if (!str_arg) {
        return NULL;  // nil -> nil
    }
    if (TAG(str_arg) != CLJ_STRING) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "trim-newline requires a string argument");
        return NULL;
    }

    CljString *str = as_clj_string(str_arg);
    const char *data = string_data(str);
    uint16_t str_len = string_length(str);
    if (str_len == 0) {
        return string_empty_singleton;
    }

    uint16_t end = str_len;
    while (end > 0 && string_is_newline_char(data[end - 1])) {
        end--;
    }
    if (end == str_len) {
        return str_arg;
    }
    if (end == 0) {
        return string_empty_singleton;
    }

    CljString *result = make_string_buffer(end);
    if (!result) {
        throw_exception(EXCEPTION_RUNTIME, "trim-newline failed to allocate result", __FILE__, __LINE__, 0);
        return NULL;
    }
    memcpy(result->data, data, end);
    result->data[end] = '\0';
    return AUTORELEASE(result);
}

// ============================================================================
// clojure.string native lookup (used by :native stubs)
// ============================================================================

typedef struct {
    CljSymbol *clojure_symbol;
    const char *qualified_cname;
    BuiltinFn native_func;
} BuiltinsStringsNativeFunctionEntry;

#define BUILTINS_STRINGS_ENTRY(sym_ptr, fn) { (sym_ptr), NULL, (fn) }
#define BUILTINS_STRINGS_ENTRY_QUALIFIED(cname, fn) { NULL, (cname), (fn) }

static const BuiltinsStringsNativeFunctionEntry builtins_strings_native_function_table[] = {
    BUILTINS_STRINGS_ENTRY(&sym_trim_data.sym, native_trim),
    BUILTINS_STRINGS_ENTRY(&sym_upper_case_data.sym, native_upper_case),
    BUILTINS_STRINGS_ENTRY(&sym_lower_case_data.sym, native_lower_case),
    BUILTINS_STRINGS_ENTRY(&sym_pad_left_data.sym, native_pad_left),
    BUILTINS_STRINGS_ENTRY(&sym_index_of_data.sym, native_index_of),
    BUILTINS_STRINGS_ENTRY(&sym_last_index_of_data.sym, native_last_index_of),
    BUILTINS_STRINGS_ENTRY(&sym_string_reverse_data.sym, native_string_reverse),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/blank?", native_blank_p),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/capitalize", native_capitalize),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/includes?", native_includes_p),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/starts-with?", native_starts_with_p),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/ends-with?", native_ends_with_p),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/triml", native_triml),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/trimr", native_trimr),
    BUILTINS_STRINGS_ENTRY_QUALIFIED("clojure.string/trim-newline", native_trim_newline),
    {NULL, NULL, NULL}
};

BuiltinFn builtins_strings_native_function_lookup(CljSymbol *symbol) {
    if (!symbol) return NULL;

    const char *cname = symbol->cname;
    const char *ns_name = (symbol->ns_name) ? symbol->ns_name->cname : NULL;

    char qualified_name[128];
    if (ns_name) {
        snprintf(qualified_name, sizeof(qualified_name), "%s/%s", ns_name, cname);
    }

    for (int i = 0; builtins_strings_native_function_table[i].native_func != NULL; i++) {
        CljSymbol *table_sym = builtins_strings_native_function_table[i].clojure_symbol;
        const char *qualified_entry = builtins_strings_native_function_table[i].qualified_cname;

        if (table_sym && table_sym == symbol) {
            return builtins_strings_native_function_table[i].native_func;
        }

        if (qualified_entry && ns_name && strcmp(qualified_entry, qualified_name) == 0) {
            return builtins_strings_native_function_table[i].native_func;
        }

        if (!table_sym || !cname || !table_sym->cname) continue;

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
