#include "list_operations.h"
#include "memory.h"
#include "value.h"
#include <stdarg.h>

// List-Operationen für try/catch
CljObject* list_first(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return NULL;
    CljList *list_data = as_list(list);
    if (!list_data) return NULL;
    
    // For empty lists, LIST_FIRST returns NULL, but we should return NULL singleton
    CljObject *first = LIST_FIRST(list_data);
    return first ? first : NULL;
}

CljObject* list_nth(CljObject *list, int n) {
    if (!list || list->type != CLJ_LIST || n < 0) return NULL;
    
    CljList *ld = as_list(list);
    if (!ld) return NULL;
    
    CljObject *current = (CljObject*)ld;
    
    // Traverse the list properly
    for (int i = 0; i <= n && current && is_type(current, CLJ_LIST); i++) {
        if (i == n) {
            CljList *current_list = as_list(current);
            return LIST_FIRST(current_list);
        }
        CljList *current_list = as_list(current);
        current = LIST_REST(current_list);
        if (current && !is_type(current, CLJ_LIST)) {
            current = NULL; // Stop if rest is not a list
        }
    }
    
    return NULL;
}

int list_count(CljObject *list) {
    if (!list) return 0;
    
    // Check if it's an immediate value (not a heap object)
    if (IS_IMMEDIATE(list)) {
        return 0;  // Immediates are not lists
    }
    
    if (list->type != CLJ_LIST) return 0;
    
    int count = 0;
    // Don't use as_list here since we already checked the type
    CljObject *current = list;
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
        CljList *new_node = make_list(element, result);
        if (element) RETAIN(element);
        result = (CljObject*)new_node;
    }
    return (CljValue)result;
}

bool is_list(CljObject *v) {
    return v && is_type(v, CLJ_LIST);
}

bool is_symbol(CljObject *v, const char *name) {
    if (!v || v->type != CLJ_SYMBOL || !name) return false;
    
    // Erstelle Symbol für Vergleich (wird interniert)
    CljObject *compare_symbol = intern_symbol_global(name);
    if (!compare_symbol) return false;
    
    // Pointer-Vergleich statt String-Vergleich!
    return v == compare_symbol;
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
        CljList *new_node = make_list(fixnum(value), result);
        result = (CljObject*)new_node;
    }
    
    va_end(args);
    return result ? (CljObject*)result : NULL;
}
