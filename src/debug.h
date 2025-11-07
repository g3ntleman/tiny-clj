#ifndef TINY_CLJ_DEBUG_H
#define TINY_CLJ_DEBUG_H

#include "object.h"

/**
 * @brief Print AST structure for debugging
 * @param v CljObject to print
 * @return Newly allocated C-string representation (caller must free)
 */
const char* print_ast(CljObject *v);

#endif // TINY_CLJ_DEBUG_H
