/*
 * sheredom/utf8.h - vendored single-header placeholder
 *
 * Note: This is a minimal placeholder for integration wiring.
 * Replace with the official sheredom/utf8.h to get the full API.
 * License: MIT (per upstream). Keep this header local in external/.
 */
#ifndef EXTERNAL_UTF8_H
#define EXTERNAL_UTF8_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal API surface we plan to use later. The real header
 * provides many more functions. These stubs enable compilation
 * even before we replace the file with the upstream version. */

/* Return non-zero if s is valid UTF-8, zero otherwise. */
static inline int utf8valid(const char *s) {
    /* Placeholder: accept all non-null strings for now. */
    if (!s) return 0;
    return 1;
}

/* Return the number of UTF-8 codepoints in s. */
static inline size_t utf8len(const char *s) {
    /* Placeholder: byte length; replace after upstream import. */
    size_t n = 0; if (!s) return 0; while (s[n]) n++; return n;
}

/* Advance over a single codepoint; write codepoint to *out_cp if not null.
 * Returns pointer to the start of the next codepoint. */
static inline const char *utf8codepoint(const char *s, int *out_cp) {
    if (!s || !*s) return s;
    /* Placeholder: ASCII-only step; multi-byte not handled here. */
    if (out_cp) *out_cp = (unsigned char)*s;
    return s + 1;
}

#ifdef __cplusplus
}
#endif

#endif /* EXTERNAL_UTF8_H */


