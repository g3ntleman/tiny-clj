#ifndef ARGS_UTILS_H
#define ARGS_UTILS_H

#include "object.h"
#include "value.h"
#include <stdbool.h>

/**
 * @brief Allocates and manages argument arrays with automatic cleanup.
 *
 * This function provides a safe way to allocate and manage argument arrays
 * with automatic cleanup on error or completion.
 *
 * @param argc Number of arguments
 * @return Allocated argument array or NULL on failure
 */
CljObject **allocate_args_array(int argc);

/**
 * @brief Cleans up argument array and frees memory.
 *
 * This function releases all arguments and frees the array memory.
 *
 * @param args Argument array to clean up
 * @param argc Number of arguments
 */
void cleanup_args_array(CljObject **args, int argc);

/**
 * @brief Macro for safe argument array allocation and cleanup.
 *
 * This macro provides a safe way to allocate and manage argument arrays
 * with automatic cleanup on error or completion.
 *
 * Usage:
 *   WITH_ARGS_ARRAY(args, argc, {
 *       // Use args[i] here
 *       // Automatic cleanup on scope exit
 *   });
 */
#define WITH_ARGS_ARRAY(args_var, argc_var, code_block) \
    do { \
        CljObject **args_var = allocate_args_array(argc_var); \
        if (args_var) { \
            code_block \
            cleanup_args_array(args_var, argc_var); \
        } \
    } while (0)

#endif // ARGS_UTILS_H
