#ifndef TINY_CLJ_LIST_H
#define TINY_CLJ_LIST_H

#include "object.h"
#include "value.h"
#include "exception.h"
#include "ast.h"
#include <stdbool.h>

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

// Iterate over all elements in a list (O(n) traversal)
// Safe with NULL lists, works with CljList and CljASTNode
// Usage: LIST_FOR_EACH(list, elem) { process(elem); }
// For REST: LIST_FOR_EACH(LIST_REST(list), elem) { ... }
// Note: break and continue work correctly
#define LIST_FOR_EACH(list, elem_var) \
    for (struct { CljList *cur; ID elem; int once; } _lfe = { is_list_like(list) ? as_list(list) : NULL, NULL, 0 }; \
         _lfe.cur && (_lfe.elem = LIST_FIRST(_lfe.cur), _lfe.once = 1); \
         _lfe.cur = as_list(LIST_REST(_lfe.cur))) \
        for (ID elem_var = _lfe.elem; _lfe.once; _lfe.once = 0)

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
