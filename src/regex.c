/*
 * Regex support for Tiny-CLJ
 * 
 * Wrapper around tiny-regex-c by kokke (Public Domain / UNLICENSE)
 * https://github.com/kokke/tiny-regex-c
 *
 * Provides Clojure-compatible regex functions:
 * re-pattern, re-find, re-matches, re-seq, regex?
 */

#include "regex.h"
#include "subjective-c/common.h"
#include "subjective-c/value.h"
#include "subjective-c/memory.h"
#include "subjective-c/object.h"
#include "../external/tiny_regex.h"
#include <string.h>
#include <stdio.h>

// Maximum pattern length
#define MAX_PATTERN_LEN 256

// CljRegex structure stores the pattern string
// We recompile on each match (tiny-regex-c uses static storage)
struct CljRegex {
    CljObject header;
    char pattern[MAX_PATTERN_LEN];
    size_t pattern_len;
};

// Validate pattern for unsupported features
// Returns NULL on success, error message on failure
static const char *validate_pattern(const char *pattern) {
    const char *p = pattern;
    while (*p) {
        if (*p == '|') {
            return "Unsupported regex feature: alternation '|'";
        }
        if (*p == '{') {
            return "Unsupported regex feature: quantifier bounds '{n,m}'";
        }
        if (*p == '(' && p[1] == '?') {
            if (p[2] == '=' || p[2] == '!') {
                return "Unsupported regex feature: lookahead";
            }
            if (p[2] == '<') {
                return "Unsupported regex feature: lookbehind or named group";
            }
        }
        // Check for non-greedy quantifiers
        if ((*p == '*' || *p == '+' || *p == '?') && p[1] == '?') {
            return "Unsupported regex feature: non-greedy quantifier";
        }
        // Handle escape sequences
        if (*p == '\\' && p[1]) {
            // Check for backreferences \1 through \9
            if (p[1] >= '1' && p[1] <= '9') {
                return "Unsupported regex feature: backreference";
            }
            p++; // Skip escaped char
        }
        p++;
    }
    return NULL; // Pattern is valid
}

CljRegex *regex_compile(const char *pattern, char *error, size_t error_size) {
    if (!pattern) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Pattern cannot be null");
        }
        return NULL;
    }
    
    size_t len = strlen(pattern);
    if (len >= MAX_PATTERN_LEN) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Pattern too long (max %d chars)", MAX_PATTERN_LEN - 1);
        }
        return NULL;
    }
    
    // Validate pattern for unsupported features
    const char *validation_error = validate_pattern(pattern);
    if (validation_error) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "%s", validation_error);
        }
        return NULL;
    }
    
    // Test compile the pattern to check for syntax errors
    re_t compiled = re_compile(pattern);
    if (!compiled) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Invalid regex pattern");
        }
        return NULL;
    }
    
    // Allocate CljRegex object using alloc (handles type and profiling)
    CljRegex *re = (CljRegex *)alloc(sizeof(CljRegex), 1, CLJ_REGEX);
    // alloc() throws on OOM, so no NULL check needed
    
    // Copy pattern
    memcpy(re->pattern, pattern, len + 1);
    re->pattern_len = len;
    
    return re;
}

void regex_free(CljRegex *re) {
    if (re) {
        DEALLOC(re);
    }
}

const char *regex_find(CljRegex *re, const char *str, 
                       const char **match_start, const char **match_end) {
    if (!re || !str) {
        if (match_start) *match_start = NULL;
        if (match_end) *match_end = NULL;
        return NULL;
    }
    
    // Recompile pattern (tiny-regex-c uses static storage)
    re_t compiled = re_compile(re->pattern);
    if (!compiled) {
        if (match_start) *match_start = NULL;
        if (match_end) *match_end = NULL;
        return NULL;
    }
    
    int match_len = 0;
    int match_idx = re_matchp(compiled, str, &match_len);
    
    if (match_idx < 0) {
        if (match_start) *match_start = NULL;
        if (match_end) *match_end = NULL;
        return NULL;
    }
    
    const char *start = str + match_idx;
    if (match_start) *match_start = start;
    if (match_end) *match_end = start + match_len;
    
    return start;
}

bool regex_matches(CljRegex *re, const char *str) {
    if (!re || !str) return false;
    
    // For re-matches, the entire string must match
    // We need to check if pattern matches from start to end
    
    // Recompile pattern
    re_t compiled = re_compile(re->pattern);
    if (!compiled) return false;
    
    int match_len = 0;
    int match_idx = re_matchp(compiled, str, &match_len);
    
    // Match must start at index 0 and cover entire string
    if (match_idx != 0) return false;
    
    size_t str_len = strlen(str);
    return (size_t)match_len == str_len;
}

const char *regex_pattern_string(CljRegex *re) {
    if (!re) return NULL;
    return re->pattern;
}
