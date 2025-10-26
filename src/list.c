#include "list.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include <stdarg.h>

// Empty-list singleton: CLJ_LIST with rc=0, statically initialized
static struct {
    CljList list;
} clj_empty_list_singleton_data = {
    .list = {
        .base = { .type = CLJ_LIST, .rc = 0 },
        .first = NULL,
        .rest = NULL
    }
};
static CljList *clj_empty_list_singleton = &clj_empty_list_singleton_data.list;

/** Return empty-list singleton (rc=0, do not retain/release). */
CljObject* empty_list(void) {
    return (CljObject*)clj_empty_list_singleton;
}

// List-Operationen für try/catch
ID list_nth(CljList *list, int n) {
    if (!list || n < 0) return NULL;
    
    CljObject *current = (CljObject*)list;
    
    // Traverse the list properly
    for (int i = 0; i <= n && current && is_type(current, CLJ_LIST); i++) {
        if (i == n) {
            CljList *current_list = as_list(current);
            return (ID)LIST_FIRST(current_list);  // Return directly - no additional memory management
        }
        CljList *current_list = as_list(current);
        current = LIST_REST(current_list);
        if (current && !is_type(current, CLJ_LIST)) {
            current = NULL; // Stop if rest is not a list
        }
    }
    
    return NULL;
}

int list_count(CljList *list) {
    if (!list) return 0;
    
    int count = 0;
    CljObject *current = (CljObject*)list;
    while (current && is_type(current, CLJ_LIST)) {
        CljList *current_list = as_list(current);
        // Count the element (first) of this list node
        // Even if LIST_FIRST is NULL (nil), it's still an element
        count++;
        current = LIST_REST(current_list);
        if (current && !is_type(current, CLJ_LIST)) {
            current = NULL; // Stop if rest is not a list
        }
    }
    return count;
}

/** Create a list from stack items. Returns new object with RC=1. */

/** Create a list from CljValue stack items. Returns new CljValue. */
CljValue make_list_from_stack(CljValue *stack, int count) {
    if (count == 0) return NULL;
    
    // Build list from end to start using make_list
    CljObject *result = NULL;
    for (int i = count - 1; i >= 0; i--) {
        CljObject *element = stack[i];
        CljObject *new_node = make_list(element, (CljList*)result);
        if (element) RETAIN(element);
        result = new_node;
    }
    return (CljValue)result;
}

bool is_list(ID v) {
    return v && is_type((CljObject*)v, CLJ_LIST);
}

bool is_symbol(ID v, const char *name) {
    if (!v || !is_type(v, CLJ_SYMBOL) || !name) return false;
    
    // Erstelle Symbol für Vergleich (wird interniert)
    CljObject *compare_symbol = intern_symbol_global(name);
    if (!compare_symbol) return false;
    
    // Pointer-Vergleich statt String-Vergleich!
    return (CljObject*)v == compare_symbol;
}

// ============================================================================
// CONVENIENCE FUNCTIONS
// ============================================================================

CljObject* list_from_ints(int count, ...) {
    if (count <= 0) return NULL;
    
    va_list args;
    va_start(args, count);
    
    // Build list from end to start using make_list
    CljObject *result = NULL;
    for (int i = count - 1; i >= 0; i--) {
        int value = va_arg(args, int);
        CljObject *new_node = make_list(fixnum(value), (CljList*)result);
        result = new_node;
    }
    
    va_end(args);
    return result ? result : NULL;
}
