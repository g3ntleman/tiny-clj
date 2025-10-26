#include "args_utils.h"
#include "memory.h"
#include <stdlib.h>

/**
 * @brief Allocates and manages argument arrays with automatic cleanup.
 *
 * This function provides a safe way to allocate and manage argument arrays
 * with automatic cleanup on error or completion.
 *
 * @param argc Number of arguments
 * @return Allocated argument array or NULL on failure
 */
CljObject **allocate_args_array(int argc) {
    if (argc <= 0) return NULL;
    
    CljObject **args = (CljObject**)malloc(sizeof(CljObject*) * argc);
    if (!args) return NULL;
    
    // Initialize all pointers to NULL
    for (int i = 0; i < argc; i++) {
        args[i] = NULL;
    }
    
    return args;
}

/**
 * @brief Cleans up argument array and frees memory.
 *
 * This function releases all arguments and frees the array memory.
 *
 * @param args Argument array to clean up
 * @param argc Number of arguments
 */
void cleanup_args_array(CljObject **args, int argc) {
    if (!args) return;
    
    for (int i = 0; i < argc; i++) {
        if (args[i]) {
            RELEASE(args[i]);
        }
    }
    
    free(args);
}
