#ifndef STRING_H
#define STRING_H

// Forward declaration to avoid circular dependencies
struct CljObject;

/**
 * @brief Create a string object from C string
 * @param s C string to copy (can be NULL for empty string)
 * @return New CljObject with RC=1 or empty string singleton
 */
struct CljObject* make_string(const char *s);

#endif
