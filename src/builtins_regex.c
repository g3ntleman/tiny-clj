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
    const char *pattern = clj_string_data(pattern_str);

    char error[256];
    CljRegex *re = regex_compile(pattern, error, sizeof(error));
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
    const char *text = clj_string_data(str);

    const char *match_start = NULL;
    const char *match_end = NULL;

    if (!regex_find(re, text, &match_start, &match_end)) {
        return NULL; // nil - no match
    }

    size_t match_len = (size_t)(match_end - match_start);
    CljString *result = make_string_buffer(match_len);
    memcpy(result->data, match_start, match_len);
    result->data[match_len] = '\0';
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
    const char *text = clj_string_data(str);

    if (!regex_matches(re, text)) {
        return NULL; // nil - no match or partial match
    }

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
    const char *text = clj_string_data(str);
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
        return NULL;
    }

    // Reverse the list to get correct order
    CljList *reversed = NULL;
    while (result) {
        CljList *next = as_list(result->rest);
        result->rest = (ID)reversed;
        reversed = result;
        result = next;
    }

    return AUTORELEASE(reversed);
}
