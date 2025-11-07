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

// Safe accessor macros. They do not do memory-management. They return the object directly.
#define LIST_FIRST(list) ((list) ? (list)->first : NULL)
#define LIST_REST(list) ((list) ? (list)->rest : NULL)

// List creation and operations
CljList* make_list(ID first, CljList *rest);
CljList* empty_list(void);

// Type-safe casting
// Returns NULL if obj is NULL (valid for list->rest)
// Throws exception if obj is not NULL and not a CLJ_LIST
static inline CljList* as_list(ID obj) {
    if (!obj) {
        // NULL is valid (e.g., end of list) - return NULL
        return NULL;
    }
    if (!is_type((CljObject*)obj, CLJ_LIST)) {
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
    }
    return (CljList*)obj;
}
ID list_nth(CljList *list, int n);
int list_count(CljList *list);
CljList* make_list_from_stack(CljValue *stack, int count);
bool is_list(ID v);
bool is_symbol(ID v, const char *name);
CljObject* list_from_ints(int count, ...);

#endif
