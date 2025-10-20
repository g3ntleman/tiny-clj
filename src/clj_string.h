#ifndef CLJ_STRING_H
#define CLJ_STRING_H

// Forward declaration to avoid circular dependencies
struct CljObject;

// === Legacy API (deprecated - use CljValue API) ===
/**
 * @deprecated Use make_string() instead. Create a string object from C string
 * @param s C string to copy (can be NULL for empty string)
 * @return New CljObject with RC=1 or empty string singleton
 */
struct CljObject* make_string_old(const char *s);

#endif