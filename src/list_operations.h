#ifndef LIST_OPERATIONS_H
#define LIST_OPERATIONS_H

#include "object.h"
#include <stdbool.h>

// List operation functions
CljObject* list_first(CljObject *list);
CljObject* list_nth(CljObject *list, int n);
int list_count(CljObject *list);
CljObject* list_from_stack(CljObject **stack, int count);
bool is_list(CljObject *v);
bool is_symbol(CljObject *v, const char *name);

// Convenience functions for creating lists
CljObject* list_from_ints(int count, ...);

#endif // LIST_OPERATIONS_H
