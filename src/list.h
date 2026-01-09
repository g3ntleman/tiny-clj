#ifndef TINY_CLJ_LIST_H
#define TINY_CLJ_LIST_H

#include <subjective-c/object.h>
#include <subjective-c/value.h>
#include <subjective-c/exception.h>
#include "ast.h"
#include <stdbool.h>

// CljList represents a Clojure-style linked list
typedef struct CljList {
    CljObject base;
    CljObject *first;
    CljObject *rest;
} CljList;

// Safe accessor macros. They do not do memory-management. They return the object directly.
#define LIST_FIRST(list) ((list) ? (list)->first : NULL)
#define LIST_REST(list) ((list) ? (list)->rest : NULL)
// Convenience macros for common list access patterns
#define LIST_SECOND(list) LIST_FIRST(as_list(LIST_REST(list)))
#define LIST_THIRD(list)  LIST_FIRST(as_list(LIST_REST(as_list(LIST_REST(list)))))

// Iterate over all elements in a list (O(n) traversal)
// Safe with NULL lists, works with CljList and CljASTNode
// Usage: LIST_FOR_EACH(list, elem) { process(elem); }
// For REST: LIST_FOR_EACH(LIST_REST(list), elem) { ... }
// Note: break and continue work correctly
#define LIST_FOR_EACH(list, elem_var) \
    for (struct { CljList *cur; ID elem; int once; } _lfe = { list_normalize_empty_to_null(list_like_as_list_or_null(list)), NULL, 0 }; \
         _lfe.cur && (_lfe.elem = LIST_FIRST(_lfe.cur), _lfe.once = 1); \
         _lfe.cur = list_normalize_empty_to_null(as_list(LIST_REST(_lfe.cur)))) \
        for (ID elem_var = _lfe.elem; _lfe.once; _lfe.once = 0)

static inline bool is_list_type(CljType type) {
    return type == CLJ_LIST || type == CLJ_AST_NODE;
}

// Backwards-compatible alias.
// Prefer `is_list_type()` in new/modified code.
#define list_type_matches is_list_type

static inline bool is_list_like(ID obj) {
    return obj && is_list_type(TAG(obj));
}

static inline CljList* list_like_as_list_or_null(ID obj) {
    return is_list_like(obj) ? (CljList*)obj : NULL;
}

// Check if a list is empty (only the empty list singleton is truly empty)
// A list with (nil . nil) is NOT empty - it has one nil element
// Use is_singleton() to distinguish between empty list singleton and list with nil element
static inline bool list_empty(CljList *list) {
    if (list == NULL) return true;
    // Only the empty list singleton is empty (has SINGLETON_RC)
    // A newly created list with (nil . NULL) is NOT empty - it has one nil element
    return LIST_FIRST(list) == NULL && LIST_REST(list) == NULL && is_singleton((CljObject*)list);
}

static inline CljList* list_normalize_empty_to_null(CljList *list) {
    // Legacy call sites sometimes used a small local helper that normalized the
    // empty-list singleton to NULL.
    // Prefer using this shared helper (or `LIST_FOR_EACH`) instead.
    return list_empty(list) ? NULL : list;
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

// Safe rest accessor - returns NULL if list is NULL or has no rest
static inline CljList* list_rest_safe(CljList *l) {
    return l && l->rest ? as_list(l->rest) : NULL;
}

ID list_nth(CljList *list, int n);
// NOTE: `list_count` is O(n) for linked lists.
// Avoid calling it just to drive iteration; prefer `LIST_FOR_EACH` / `LIST_REST` traversal when possible.
int list_count(CljList *list);
// NOTE: `list_get_element` is discouraged.
// It returns NULL both for a valid nil element *and* for out-of-bounds / malformed lists,
// so callers cannot distinguish these cases.
// Prefer `LIST_FIRST`/`LIST_REST`/`LIST_FOR_EACH` for traversal, or `list_nth` when the index
// is provably in-bounds (and you want an exception on invalid indices).
CljObject* list_get_element(CljList *list, int index);
static inline bool is_list(ID v) {
    return is_list_like(v);
}

#endif
