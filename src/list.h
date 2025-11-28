#ifndef TINY_CLJ_LIST_H
#define TINY_CLJ_LIST_H

#include "object.h"
#include "value.h"
#include "exception.h" // For throw_exception
#include <stdbool.h>
#include <stdio.h> // For snprintf
#ifdef __GNUC__
#include <execinfo.h> // For backtrace and backtrace_symbols
#include <stdlib.h> // For free
#endif

// CljList represents a Clojure-style linked list
typedef struct CljList {
    CljObject base;
    CljObject *first;
    CljObject *rest;
} CljList;

// Safe accessor macros. They do not do memory-management. They return the object directly.
#define LIST_FIRST(list) ((list) ? (ID)(list)->first : NULL)
#define LIST_REST(list) ((list) ? (ID)(list)->rest : NULL)

// Check if a list is empty (both first and rest are NULL)
static inline bool list_empty(CljList *list) {
    return list == NULL || (LIST_FIRST(list) == NULL && LIST_REST(list) == NULL);
}

// List creation and operations
CljList* make_list(ID first, CljList *rest);
CljList* empty_list(void);

// Type-safe casting
// Returns NULL if obj is NULL (valid for list->rest)
// Throws exception if obj is not NULL and not a CLJ_LIST
static inline CljList* as_list(ID obj) {
    // Happy path: obj is not NULL and has correct type
    if (obj && TAG(obj) == CLJ_LIST) {
        return (CljList*)obj;  // Direct return, no jumps
    }
    // NULL is valid (e.g., end of list) - return NULL
    if (!obj) {
        return NULL;
    }
    // Error case: wrong type
    char error_msg[128];
    const char *type_name = clj_type_name(((CljObject*)obj)->type);
    snprintf(error_msg, sizeof(error_msg), 
            "Type mismatch: expected List, got %s", 
            type_name);
    printf("[STACKTRACE] as_list failed at %s:%d - obj=%p, type=%d (%s)\n", __FILE__, __LINE__, obj, ((CljObject*)obj)->type, type_name);
    // Print stacktrace
    #ifdef __GNUC__
    void *array[10];
    size_t size = backtrace(array, 10);
    char **strings = backtrace_symbols(array, size);
    printf("[STACKTRACE] Backtrace:\n");
    for (size_t i = 0; i < size; i++) {
        printf("  %s\n", strings[i]);
    }
    free(strings);
    #endif
    throw_exception(EXCEPTION_TYPE, error_msg, __FILE__, __LINE__, 0);
    return NULL;
}
ID list_nth(CljList *list, int n);
int list_count(CljList *list);
CljList* make_list_from_stack(CljValue *stack, int count);
bool is_list(ID v);
bool is_symbol(ID v, const char *name);

#endif
