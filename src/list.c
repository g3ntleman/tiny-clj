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
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
                "nth index %d is out of bounds for list", n);
        return NULL;
    }
    
    // Check if list is empty (both first and rest are NULL)
    if (list_empty(list)) {
        // Empty list - index is out of bounds
        throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
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
    throw_exception_formatted("IndexOutOfBoundsException", __FILE__, __LINE__, 0,
            "nth index %d is out of bounds for list", n);
    return NULL;
}

int list_count(CljList *list) {
    if (!list) return 0;
    
    // Check if it's actually a list before calling as_list
    if (!list || TAG(list) != CLJ_LIST) {
        return 0;  // Not a list, return 0
    }
    
    // Empty list has first = NULL and rest = NULL
    // A list with nil as element has first = NULL but rest != NULL
    CljList *list_data = (CljList*)list;  // Safe cast after type check
    if (!list_data) return 0;
    
    // Check if this is the empty list singleton (both first and rest are NULL)
    if (list_empty(list_data)) {
        return 0;
    }
    
    int count = 0;
    CljObject *current = (CljObject*)list;
    while (current && TAG(current) == CLJ_LIST) {
        CljList *current_list = as_list(current);
        // Count the element (first) of this list node
        // Even if LIST_FIRST is NULL (nil), it's still an element
        count++;
        current = LIST_REST(current_list);
        if (current && TAG(current) != CLJ_LIST) {
            current = NULL; // Stop if rest is not a list
        }
    }
    return count;
}

/** Create a list from CljValue stack items. Returns new CljList*. */
CljList* make_list_from_stack(CljValue *stack, int count) {
    if (count == 0) return empty_list();
    
    // Build list from end to start using make_list
    CljList *result = NULL;
    for (int i = count - 1; i >= 0; i--) {
        CljObject *element = stack[i];
        CljList *new_node = make_list(element, result);
        // ✅ FIX: make_list already does RETAIN, no need to do it again
        result = new_node;
    }
    return result;
}

bool is_list(ID v) {
    return v && TAG(v) == CLJ_LIST;
}

bool is_symbol(ID v, const char *name) {
    if (!v || TAG(v) != CLJ_SYMBOL || !name) return false;
    
    // Erstelle Symbol für Vergleich (wird interniert)
    CljSymbol *compare_symbol = intern_symbol_global(name);
    if (!compare_symbol) return false;
    
    // Pointer-Vergleich statt String-Vergleich!
    return (CljObject*)v == (CljObject*)compare_symbol;
}
