/*
 * Native string functions for Tiny-CLJ
 * 
 * Provides: str, subs, trim, upper-case, lower-case, pad-left,
 * index-of, last-index-of, reverse (string), format
 */

#ifndef TINY_CLJ_BUILTINS_STRINGS_H
#define TINY_CLJ_BUILTINS_STRINGS_H

#include "object.h"
#include "runtime.h"  // BuiltinFn

struct CljSymbol;

// String concatenation (variadic): (str & args)
ID native_str(ID *args, unsigned int argc);

// Substring: (subs s start) or (subs s start end)
ID native_subs(ID *args, unsigned int argc);

// Trim whitespace: (trim s)
ID native_trim(ID *args, unsigned int argc);

// Convert to upper-case: (upper-case s)
ID native_upper_case(ID *args, unsigned int argc);

// Convert to lower-case: (lower-case s)
ID native_lower_case(ID *args, unsigned int argc);

// Pad left: (pad-left s width pad-char)
ID native_pad_left(ID *args, unsigned int argc);

// Index of: (index-of s value) or (index-of s value from-index)
ID native_index_of(ID *args, unsigned int argc);

// Last index of: (last-index-of s value) or (last-index-of s value from-index)
ID native_last_index_of(ID *args, unsigned int argc);

// Reverse string: (reverse s)
ID native_string_reverse(ID *args, unsigned int argc);

// Format string: (format fmt & args)
ID native_format(ID *args, unsigned int argc);

// Native lookup hook for clojure.string :native stubs.
// (The main lookup lives in builtins.c, but clojure.string is split out.)
BuiltinFn builtins_strings_native_function_lookup(struct CljSymbol *symbol);

#endif // TINY_CLJ_BUILTINS_STRINGS_H

