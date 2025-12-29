/*
 * Regex support for Tiny-CLJ
 * 
 * Thin wrapper around tiny-regex-c (Public Domain)
 * Provides Clojure-compatible regex functions:
 *   - re-pattern, re-find, re-seq, re-matches, regex?
 */

#ifndef TINY_CLJ_REGEX_H
#define TINY_CLJ_REGEX_H

#include <subjective-c/object.h>
#include "types.h"
#include <stdbool.h>

// Forward declaration for compiled regex pattern
typedef struct CljRegex CljRegex;

// Compile a regex pattern from string
// Returns NULL and sets error message on failure
CljRegex *regex_compile(const char *pattern, char *error, size_t error_size);

// Free a compiled regex pattern
void regex_free(CljRegex *re);

// Find first match in string, returns matched substring or NULL
// If match_start/match_end are non-NULL, they are set to the match boundaries
const char *regex_find(CljRegex *re, const char *str, 
                       const char **match_start, const char **match_end);

// Check if entire string matches the pattern
bool regex_matches(CljRegex *re, const char *str);

// Get the pattern string from a compiled regex
const char *regex_pattern_string(CljRegex *re);

#endif // TINY_CLJ_REGEX_H

