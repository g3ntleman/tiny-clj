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

#include "ast.h"

// CljList represents a Clojure-style linked list
typedef struct CljList {
    CljObject base;
    CljObject *first;
    CljObject *rest;
} CljList;

// Safe accessor macros. They do not do memory-management. They return the object directly.
#define LIST_FIRST(list) ((list) ? (ID)(list)->first : NULL)
#define LIST_REST(list) ((list) ? (ID)(list)->rest : NULL)
// Convenience macros for common list access patterns
#define LIST_SECOND(list) LIST_FIRST(as_list(LIST_REST(list)))
#define LIST_THIRD(list)  LIST_FIRST(as_list(LIST_REST(as_list(LIST_REST(list)))))

static inline bool list_type_matches(CljType type) {
    return type == CLJ_LIST || type == CLJ_AST_NODE;
}

static inline bool is_list_like(ID obj) {
    return obj && list_type_matches(TAG(obj));
}

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
#ifdef DEBUG
// Debug: Typ-Check mit Fehlerbehandlung
CljList* as_list_checked(ID obj);
#define as_list(obj) as_list_checked(obj)
#else
// Release: Reiner Cast, vom Compiler eliminierbar
static inline CljList* as_list(ID obj) {
    return (CljList*)obj;  // NULL bleibt NULL
}
// No declaration needed in Release - as_list is inline
#endif
ID list_nth(CljList *list, int n);
int list_count(CljList *list);
static inline bool is_list(ID v) {
    return is_list_like(v);
}

#endif
