/**
 * @file file_utils.c
 * @brief File utility functions for reading files
 */

#include "file_utils.h"
#include <subjective-c/strings.h>
#include "format_utils.h"
#include "memory.h"
#include "exception.h"
#include <string.h>
#include <errno.h>

/**
 * @brief Read entire file content as CljString
 * @param path File path to read
 * @return CljString with file content, or NULL on error (caller must release)
 * @note Returns NULL if file doesn't exist or cannot be read
 * @note Caller is responsible for releasing the returned string
 * @warning This function loads the entire file into memory. Only use for small files
 *          that fit in RAM. For large files, use a streaming approach instead.
 */
CljString* file_slurp(const char *path) {
    if (!path) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                       "file_slurp: path cannot be NULL",
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Open file
    FILE *fp = fopen(path, "r");
    if (!fp) {
        // File doesn't exist or cannot be opened - throw exception
        char error_msg[256];
        const char *err = strerror(errno);
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg), "Cannot open file '");
        pos = format_append(error_msg, pos, sizeof(error_msg), path);
        pos = format_append(error_msg, pos, sizeof(error_msg), "': ");
        format_append(error_msg, pos, sizeof(error_msg), err);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Get file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        char error_msg[256];
        const char *err = strerror(errno);
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg), "Cannot seek in file '");
        pos = format_append(error_msg, pos, sizeof(error_msg), path);
        pos = format_append(error_msg, pos, sizeof(error_msg), "': ");
        format_append(error_msg, pos, sizeof(error_msg), err);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    long file_size = ftell(fp);
    if (file_size < 0) {
        char error_msg[256];
        const char *err = strerror(errno);
        size_t pos = 0;
        pos = format_append(error_msg, pos, sizeof(error_msg), "Cannot determine size of file '");
        pos = format_append(error_msg, pos, sizeof(error_msg), path);
        pos = format_append(error_msg, pos, sizeof(error_msg), "': ");
        format_append(error_msg, pos, sizeof(error_msg), err);
        fclose(fp);
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, error_msg,
                       __FILE__, __LINE__, 0);
        return NULL;
    }
    
    // Reset to beginning
    rewind(fp);
    
    // Read file content
    // Allocate buffer for file content + null terminator
    char *buffer = (char*)malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(fp);
        throw_oom();
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, fp);
    buffer[bytes_read] = '\0';  // Null-terminate
    
    fclose(fp);
    
    // Create CljString from buffer
    CljString *result = make_string(buffer);
    free(buffer);
    
    return result;
}

