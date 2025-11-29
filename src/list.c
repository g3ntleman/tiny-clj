#include "list.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include "object.h"
#include "exception.h"
#include "types.h"  // For SINGLETON_RC

// Empty-list singleton: CLJ_LIST with rc=SINGLETON_RC, statically initialized
static struct {
    CljList list;
} clj_empty_list_singleton_data = {
    .list = {
        .base = { .type = CLJ_LIST, .rc = SINGLETON_RC },
        .first = NULL,
        .rest = NULL
    }
};
static CljList *clj_empty_list_singleton = &clj_empty_list_singleton_data.list;

/** Return empty-list singleton (rc=0, do not retain/release). */
CljList* empty_list(void) {
    return clj_empty_list_singleton;
}

CljList* make_list(ID first, CljList *rest) {
    CljList *list = ALLOC(CljList, 1);
    if (!list) throw_oom();
    
    list->base.type = CLJ_LIST;
    list->base.rc = 1;
    list->first = RETAIN((CljObject*)first);
    list->rest = RETAIN((CljObject*)rest);
    
    return list;
}

// List-Operationen für try/catch
ID list_nth(CljList *list, int n) {
    if (!list || n < 0) {
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for list", n);
        return NULL;
    }
    
    // Check if list is empty (both first and rest are NULL)
    if (list_empty(list)) {
        // Empty list - index is out of bounds
        throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for list", n);
        return NULL;
    }
    
    CljObject *current = (CljObject*)list;
    
    // Traverse the list properly
    for (int i = 0; i <= n && current && TAG(current) == CLJ_LIST; i++) {
        if (i == n) {
            CljList *current_list = as_list(current);
            // Element found - return it (may be NULL if element is nil)
            return (ID)LIST_FIRST(current_list);  // Return directly - no additional memory management
        }
        CljList *current_list = as_list(current);
        current = LIST_REST(current_list);
        if (current && TAG(current) != CLJ_LIST) {
            current = NULL; // Stop if rest is not a list
        }
    }
    
    // Index not found - out of bounds
    throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
            "nth index %d is out of bounds for list", n);
    return NULL;
}

int list_count(CljList *list) {
    if (!list) return 0;
    
    // Programmierfehler: list muss CLJ_LIST sein, wenn es nicht NULL ist
    CLJ_ASSERT(TAG(list) == CLJ_LIST);
    
    // Empty list has first = NULL and rest = NULL
    // A list with nil as element has first = NULL but rest != NULL
    // Check if this is the empty list singleton (both first and rest are NULL)
    if (list_empty(list)) {
        return 0;
    }
    
    int count = 0;
    CljObject *current = (CljObject*)list;
    while (current) {
        // Programmierfehler: current muss CLJ_LIST sein, wenn es nicht NULL ist
        CLJ_ASSERT(TAG(current) == CLJ_LIST);
        
        CljList *current_list = as_list(current);
        // Count the element (first) of this list node
        // Even if LIST_FIRST is NULL (nil), it's still an element
        count++;
        current = LIST_REST(current_list);
    }
    return count;
}

