#ifndef TINY_CLJ_LIST_H
#define TINY_CLJ_LIST_H

#include "object.h"
#include "value.h"
#include "exception.h" // For throw_exception
#include <stdbool.h>
#include <stdio.h> // For snprintf

// CljList represents a Clojure-style linked list
typedef struct CljList {
    CljObject base;
    CljObject *first;
    CljObject *rest;
} CljList;

// Safe accessor macros with proper memory management
// These return autoreleased objects following MEMORY_POLICY
#define LIST_FIRST(list) ((list) && (list)->first ? AUTORELEASE(RETAIN((list)->first)) : NULL)
#define LIST_REST(list) ((list) && (list)->rest ? AUTORELEASE(RETAIN((list)->rest)) : NULL)

// List creation and operations
CljObject* make_list(ID first, CljList *rest);

// Type-safe casting
static inline CljList* as_list(ID obj) {
    if (!is_type((CljObject*)obj, CLJ_LIST)) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected List, got %s", 
                "List");
        throw_exception("TypeError", error_msg, __FILE__, __LINE__, 0);
    }
    return (CljList*)obj;
}
ID list_nth(CljList *list, int n);
int list_count(CljList *list);
CljValue make_list_from_stack(CljValue *stack, int count);
bool is_list(ID v);
bool is_symbol(ID v, const char *name);
CljObject* list_from_ints(int count, ...);

#endif
