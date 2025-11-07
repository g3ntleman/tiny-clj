#ifndef TINY_CLJ_DEBUG_H
#define TINY_CLJ_DEBUG_H

#include "object.h"
#include <stdio.h>

/**
 * @brief Print AST structure for debugging
 * @param v CljObject to print
 * @return Newly allocated C-string representation (caller must free)
 */
const char* print_ast(CljObject *v);

// Debug print macro
#ifdef DEBUG
    #define DEBUG_PRINT(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

#endif // TINY_CLJ_DEBUG_H
