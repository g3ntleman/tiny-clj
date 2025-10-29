#ifndef TINY_CLJ_STRING_H
#define TINY_CLJ_STRING_H

#include "object.h"

// String representation of CljObject
/** Return newly allocated C-string representation (caller frees). */
char* pr_str(CljObject *v);
char* print_str(CljObject *v);
char* to_string(CljObject *v);

#endif
