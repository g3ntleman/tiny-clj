/*
 * Native regex functions for Tiny-CLJ
 *
 * Extracted from builtins.c for better code organization.
 * Provides: regex?, re-pattern, re-find, re-matches, re-seq
 */

#include <string.h>
#include <stdbool.h>

#include "builtins_regex.h"
#include "exception.h"
#include "list.h"
#include "memory.h"
#include "regex.h"
#include "strings.h"
#include "value.h"

static char *copy_string_cstr(CljString *str, const char *context) {
    if (!str) return NULL;
    size_t len = string_length((ID)str);
    const char *data = string_data((ID)str);
    char *buf = CLJ_MALLOC(len + 1);
    if (!buf) {
        throw_exception(EXCEPTION_RUNTIME, context, __FILE__, __LINE__, 0);
        return NULL;
    }
    if (len > 0) {
        memcpy(buf, data, len);
    }
    buf[len] = '\0';
    return buf;
}

// regex?: Returns true if x is a compiled regex pattern
ID native_regex_p(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "regex?");

    ID x = args[0];
    if (x && !IS_IMMEDIATE(x) && TAG(x) == CLJ_REGEX) {
        return clj_true;
    }
    return clj_false;
}

// re-pattern: Compile a string into a regex pattern
ID native_re_pattern(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "re-pattern");

    ID pattern_arg = args[0];

    if (!pattern_arg || IS_IMMEDIATE(pattern_arg) || TAG(pattern_arg) != CLJ_STRING) {
        throw_exception(EXCEPTION_RUNTIME, "re-pattern: argument must be a string",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljString *pattern_str = as_clj_string(pattern_arg);
    char *pattern = copy_string_cstr(pattern_str, "re-pattern: failed to allocate pattern buffer");
    if (!pattern) return NULL;

    char error[256];
    CljRegex *re = regex_compile(pattern, error, sizeof(error));
    CLJ_FREE(pattern);
    if (!re) {
        throw_exception(EXCEPTION_RUNTIME, error, __FILE__, __LINE__, 0);
        return NULL;
    }

    return AUTORELEASE(re);
}

// re-find: Find first match of pattern in string
ID native_re_find(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "re-find");

    ID re_arg = args[0];
    ID str_arg = args[1];

    // Validate regex argument
    if (!re_arg || IS_IMMEDIATE(re_arg) || TAG(re_arg) != CLJ_REGEX) {
        throw_exception(EXCEPTION_RUNTIME, "re-find: first argument must be a regex pattern",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Validate string argument
    if (!str_arg || IS_IMMEDIATE(str_arg) || TAG(str_arg) != CLJ_STRING) {
        throw_exception(EXCEPTION_RUNTIME, "re-find: second argument must be a string",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljRegex *re = (CljRegex *)re_arg;
    CljString *str = as_clj_string(str_arg);
    char *text = copy_string_cstr(str, "re-find: failed to allocate text buffer");
    if (!text) return NULL;

    const char *match_start = NULL;
    const char *match_end = NULL;

    if (!regex_find(re, text, &match_start, &match_end)) {
        CLJ_FREE(text);
        return NULL; // nil - no match
    }

    size_t match_len = (size_t)(match_end - match_start);
    CljString *result = make_string_buffer(match_len);
    memcpy(result->data, match_start, match_len);
    result->data[match_len] = '\0';
    CLJ_FREE(text);
    return AUTORELEASE(result);
}

// re-matches: Returns match if entire string matches pattern
ID native_re_matches(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "re-matches");

    ID re_arg = args[0];
    ID str_arg = args[1];

    // Validate regex argument
    if (!re_arg || IS_IMMEDIATE(re_arg) || TAG(re_arg) != CLJ_REGEX) {
        throw_exception(EXCEPTION_RUNTIME, "re-matches: first argument must be a regex pattern",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Validate string argument
    if (!str_arg || IS_IMMEDIATE(str_arg) || TAG(str_arg) != CLJ_STRING) {
        throw_exception(EXCEPTION_RUNTIME, "re-matches: second argument must be a string",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljRegex *re = (CljRegex *)re_arg;
    CljString *str = as_clj_string(str_arg);
    char *text = copy_string_cstr(str, "re-matches: failed to allocate text buffer");
    if (!text) return NULL;

    if (!regex_matches(re, text)) {
        CLJ_FREE(text);
        return NULL; // nil - no match or partial match
    }

    CLJ_FREE(text);
    // Return the matched string (entire string in this case)
    return str_arg;
}

// re-seq: Returns lazy sequence of matches (simplified: returns list)
ID native_re_seq(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "re-seq");

    ID re_arg = args[0];
    ID str_arg = args[1];

    // Validate regex argument
    if (!re_arg || IS_IMMEDIATE(re_arg) || TAG(re_arg) != CLJ_REGEX) {
        throw_exception(EXCEPTION_RUNTIME, "re-seq: first argument must be a regex pattern",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    // Validate string argument
    if (!str_arg || IS_IMMEDIATE(str_arg) || TAG(str_arg) != CLJ_STRING) {
        throw_exception(EXCEPTION_RUNTIME, "re-seq: second argument must be a string",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    CljRegex *re = (CljRegex *)re_arg;
    CljString *str = as_clj_string(str_arg);
    char *text = copy_string_cstr(str, "re-seq: failed to allocate text buffer");
    if (!text) return NULL;
    const char *pos = text;

    // Build list of matches (in reverse, then reverse at end)
    CljList *result = NULL;

    while (*pos) {
        const char *match_start = NULL;
        const char *match_end = NULL;

        if (!regex_find(re, pos, &match_start, &match_end)) {
            break;
        }

        size_t match_len = (size_t)(match_end - match_start);
        CljString *match_str = make_string_buffer(match_len);
        memcpy(match_str->data, match_start, match_len);
        match_str->data[match_len] = '\0';

        result = make_list(match_str, result);
        RELEASE(match_str);

        pos = match_end;
        if (match_len == 0) {
            if (*pos) pos++; // Avoid infinite loop on zero-length matches
            else break;
        }
    }

    if (!result) {
        CLJ_FREE(text);
        return NULL;
    }

    // Reverse the list to get correct order.
    // Build a new list to keep retain/release balanced (avoid in-place pointer rewrites).
    CljList *reversed = NULL;
    for (CljList *cur = result; cur; cur = as_list(cur->rest)) {
        ID elem = cur->first;
        reversed = make_list(elem, reversed);
    }
    // Release original list; reversed retains elements.
    RELEASE(result);
    CLJ_FREE(text);

    return reversed ? AUTORELEASE(reversed) : NULL;
}
