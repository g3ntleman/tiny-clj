#ifndef LIST_OPERATIONS_H
#define LIST_OPERATIONS_H

#include "CljObject.h"

// List operation functions
CljObject* list_first(CljObject *list);
CljObject* list_nth(CljObject *list, int n);
int list_count(CljObject *list);
int is_list(CljObject *v);
int is_symbol(CljObject *v, const char *name);

#endif // LIST_OPERATIONS_H
