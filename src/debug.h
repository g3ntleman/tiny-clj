#ifndef TINY_CLJ_DEBUG_H
#define TINY_CLJ_DEBUG_H

#include "object.h"
#include <stdio.h>
#include <stdbool.h>

#ifdef DEBUG
/**
 * @brief Print AST structure for debugging
 * @param v CljObject to print
 * @return Newly allocated C-string representation (caller must free)
 */
const char* print_ast(CljObject *v);
#endif // DEBUG

/**
 * @brief Check if an object is a zombie (freed but not deallocated)
 * @param o Object to check (can be NULL or immediate)
 * @return true if object is a zombie, false otherwise
 * @note Zombie objects have rc == ZOMBIE_RC (-1) and are only present in DEBUG builds
 */
bool is_zombie(ID o);

// Debug print macro
#ifdef DEBUG
    #define DEBUG_PRINT(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

#endif // TINY_CLJ_DEBUG_H
