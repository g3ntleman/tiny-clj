#ifndef TINY_CLJ_VECTOR_TO_LIST_H
#define TINY_CLJ_VECTOR_TO_LIST_H

#include "vector.h"
#include "list.h"

// Converts a CljVector to a CljList (linked list).
// Returns a new list (caller owns; list nodes retain elements).
static inline CljList* vector_to_list(CljPersistentVector* vec) {
    if (!vec) return empty_list();
    unsigned int count = vector_count(vec);
    CljList* result = empty_list();
    // Insert in reverse order to preserve vector order in list.
    for (int i = (int)count - 1; i >= 0; --i) {
        ID elem = vector_nth(vec, (unsigned int)i);
        result = make_list(elem ? RETAIN(elem) : NULL, result);
    }
    return result;
}

#endif // TINY_CLJ_VECTOR_TO_LIST_H
