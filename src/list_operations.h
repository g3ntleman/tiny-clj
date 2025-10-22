#ifndef LIST_OPERATIONS_H
#define LIST_OPERATIONS_H

#include "object.h"
#include "value.h"
#include <stdbool.h>

// List operation functions
ID list_first(CljList *list);
ID list_nth(CljList *list, int n);
int list_count(CljList *list);
CljValue make_list_from_stack(CljValue *stack, int count);
bool is_list(ID v);
bool is_symbol(ID v, const char *name);

// Convenience functions for creating lists
CljObject* list_from_ints(int count, ...);

#endif // LIST_OPERATIONS_H
