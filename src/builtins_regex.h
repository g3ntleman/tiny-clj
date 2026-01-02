/*
 * Native regex functions for Tiny-CLJ
 *
 * Extracted from builtins.c for better code organization.
 * Provides: regex?, re-pattern, re-find, re-matches, re-seq
 */

#ifndef TINY_CLJ_BUILTINS_REGEX_H
#define TINY_CLJ_BUILTINS_REGEX_H

#include <subjective-c/object.h>

ID native_regex_p(ID *args, unsigned int argc);
ID native_re_pattern(ID *args, unsigned int argc);
ID native_re_find(ID *args, unsigned int argc);
ID native_re_matches(ID *args, unsigned int argc);
ID native_re_seq(ID *args, unsigned int argc);

#endif // TINY_CLJ_BUILTINS_REGEX_H
