/*
 * Regex support for Tiny-CLJ (subjective-c)
 *
 * Thin wrapper around tiny-regex-c (Public Domain)
 * Provides Clojure-compatible regex functions:
 *   - re-pattern, re-find, re-seq, re-matches, regex?
 */

#ifndef SUBJECTIVE_C_REGEX_H
#define SUBJECTIVE_C_REGEX_H

#include <subjective-c/object.h>

#include <stdbool.h>
#include <stddef.h>

// Forward declaration for compiled regex pattern
typedef struct CljRegex CljRegex;

/** @brief Compile regex pattern from string
 * @param pattern Regex pattern string
 * @param error Error buffer
 * @param error_size Error buffer size
 * @return Compiled regex or NULL on error
 */
CljRegex *regex_compile(const char *pattern, char *error, size_t error_size);

/** @brief Free compiled regex pattern
 * @param re Regex to free
 */
void regex_free(CljRegex *re);

/** @brief Find first match in string
 * @param re Compiled regex
 * @param str String to search
 * @param match_start Output for match start (can be NULL)
 * @param match_end Output for match end (can be NULL)
 * @return Matched substring or NULL
 */
const char *regex_find(CljRegex *re, const char *str,
                       const char **match_start, const char **match_end);

/** @brief Check if entire string matches pattern
 * @param re Compiled regex
 * @param str String to match
 * @return True if entire string matches
 */
bool regex_matches(CljRegex *re, const char *str);

/** @brief Get pattern string from compiled regex
 * @param re Compiled regex
 * @return Pattern string
 */
const char *regex_pattern_string(CljRegex *re);

#endif // SUBJECTIVE_C_REGEX_H
