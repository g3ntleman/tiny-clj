#ifndef TINY_CLJ_FILE_UTILS_H
#define TINY_CLJ_FILE_UTILS_H

#include <subjective-c/strings.h>

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

#endif // TINY_CLJ_FILE_UTILS_H

