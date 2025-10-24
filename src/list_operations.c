#include "list_operations.h"
#include "memory.h"
#include "value.h"
#include <stdarg.h>

// List-Operationen für try/catch
ID list_first(CljList *list) {
    if (!list) return NULL;
    
    // For empty lists, LIST_FIRST returns NULL, but we should return NULL singleton
    CljObject *first = LIST_FIRST(list);
    return (ID)(first ? first : NULL);
}

ID list_nth(CljList *list, int n) {
    if (!list || n < 0) return NULL;
    
    CljObject *current = (CljObject*)list;
    
    // Traverse the list properly
    for (int i = 0; i <= n && current && is_type(current, CLJ_LIST); i++) {
        if (i == n) {
            CljList *current_list = as_list(current);
            return (ID)LIST_FIRST(current_list);
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
    // Work directly with CljList*
    CljObject *current = (CljObject*)list;
    while (current && is_type(current, CLJ_LIST)) {
        CljList *current_list = as_list(current);
        // Only count if the list has a first element (not empty)
        if (LIST_FIRST(current_list)) {
            count++;
        }
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
        CljObject *element = ID_TO_OBJ(stack[i]);
        CljObject *new_node = make_list(element, (CljList*)result);
        if (element) RETAIN(element);
        result = (CljObject*)new_node;
    }
    return (CljValue)result;
}

bool is_list(ID v) {
    return v && is_type((CljObject*)v, CLJ_LIST);
}

bool is_symbol(ID v, const char *name) {
    if (!v || ((CljObject*)v)->type != CLJ_SYMBOL || !name) return false;
    
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
        result = (CljObject*)new_node;
    }
    
    va_end(args);
    return result ? (CljObject*)result : NULL;
}
