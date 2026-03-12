#ifndef TINY_CLJ_FILE_UTILS_H
#define TINY_CLJ_FILE_UTILS_H

#include "strings.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Read entire file content as CljString
 * @param path File path to read
 * @return CljString with file content, or NULL on error (caller must release)
 * @note Returns NULL if file doesn't exist or cannot be read
 * @note Caller is responsible for releasing the returned string
 * @warning This function loads the entire file into memory. Only use for small files
 *          that fit in RAM. For large files, use a streaming approach instead.
 */
CljString* file_slurp(const char *path);

/**
 * @brief Write bytes to a host file path.
 * @param path File path to write.
 * @param data Byte buffer to write.
 * @param len Number of bytes to write.
 * @return true on success, false when the host file path cannot be written.
 */
bool file_spit_bytes(const char *path, const uint8_t *data, size_t len);

#endif // TINY_CLJ_FILE_UTILS_H

