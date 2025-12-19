#include "list.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include "object.h"
#include "exception.h"
#include "types.h"  // For SINGLETON_RC
#include <stdio.h>   // For snprintf
#ifdef __GNUC__
#include <execinfo.h> // For backtrace and backtrace_symbols
#include <stdlib.h>  // For free
#endif

// Forward declaration
extern const char* clj_type_name(CljType type);

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

#ifdef DEBUG
// Debug: Typ-Check mit Fehlerbehandlung
CljList* as_list_checked(ID obj) {
    // Happy path: obj is not NULL and has correct type
    if (obj && list_type_matches(TAG(obj))) {
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
#endif

// List operations for try/catch
ID list_nth(CljList *list, int n) {
    if (!list || n < 0) {
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for list", n);
    }
    
    // Check if list is empty (both first and rest are NULL)
    if (list_empty(list)) {
        // Empty list - index is out of bounds
        return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for list", n);
    }
    
    CljObject *current = (CljObject*)list;
    
    // Traverse the list properly
    for (int i = 0; i <= n && current && list_type_matches(TAG(current)); i++) {
        if (i == n) {
            CljList *current_list = as_list(current);
            // Element found - return it (may be NULL if element is nil)
            return LIST_FIRST(current_list);  // Return directly - no additional memory management
        }
        CljList *current_list = as_list(current);
        current = LIST_REST(current_list);
        if (current && !list_type_matches(TAG(current))) {
            current = NULL; // Stop if rest is not a list
        }
    }
    
    // Index not found - out of bounds
    return throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
            "nth index %d is out of bounds for list", n);
}

int list_count(CljList *list) {
    if (!list) return 0;
    
    // Programmierfehler: list muss CLJ_LIST sein, wenn es nicht NULL ist
    CLJ_ASSERT(list_type_matches(TAG(list)));
    
    // Empty list has first = NULL and rest = NULL
    // A list with nil as element has first = NULL but rest != NULL
    // Check if this is the empty list singleton (both first and rest are NULL)
    if (list_empty(list)) {
        return 0;
    }
    
    int count = 0;
    LIST_FOR_EACH(list, elem) {
        (void)elem;  // unused - we count all elements, even nil
        count++;
    }
    return count;
}

