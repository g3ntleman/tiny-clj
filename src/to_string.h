#ifndef TINY_CLJ_TO_STRING_H
#define TINY_CLJ_TO_STRING_H

#include "subjective-c/strings.h"
#include "subjective-c/value.h"
#include "symbol.h"

/**
 * @brief Convert a Clojure value to its string representation
 * @param v Value to convert
 * @return CljString with the string representation (caller must release)
 */
CljString* to_string(ID v);

/**
 * @brief Convert a Clojure value to its string representation with escape control
 * @param v Value to convert
 * @param escape_strings If true, strings are quoted and escaped
 * @return CljString with the string representation (caller must release)
 */
CljString* to_string_with_escape(ID v, bool escape_strings);

/**
 * @brief Convert a value to readable string representation (like Clojure's pr-str)
 * 
 * Strings are quoted and escaped for readability (can be read back).
 * @param v Value to convert
 * @return CljString with the readable representation (caller must release)
 */
CljString* pr_str(ID v);

/**
 * @brief Convert a value to print string representation (like Clojure's print-str)
 * 
 * Strings are not quoted or escaped (human-readable output).
 * @param v Value to convert
 * @return CljString with the print representation (caller must release)
 */
CljString* print_str(ID v);

/**
 * @brief Set whether special forms are rendered as tags
 * @param as_tags If true, special forms render as #<special-form name>
 * @return Previous value of the setting
 */
bool strings_set_special_form_rendering(bool as_tags);

/**
 * @brief Get current special form rendering mode
 * @return true if special forms are rendered as tags
 */
bool strings_get_special_form_rendering(void);

/**
 * @brief Register a symbol name as a special form
 * @param name Name of the special form to register
 */
void strings_register_special_form(const char *name);

/**
 * @brief Clear all registered special forms
 */
void strings_clear_special_forms(void);

// is_special_symbol() moved to symbol.h (inline function)

#endif // TINY_CLJ_TO_STRING_H

