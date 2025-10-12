#include "list_operations.h"
#include "memory.h"
#include <stdarg.h>

// List-Operationen für try/catch
CljObject* list_first(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return clj_nil();
    CljList *list_data = as_list(list);
    if (!list_data) return clj_nil();
    if (!LIST_FIRST(list_data)) return clj_nil();
    return LIST_FIRST(list_data);
}

CljObject* list_nth(CljObject *list, int n) {
    if (!list || list->type != CLJ_LIST || n < 0) return clj_nil();
    
    CljList *ld = as_list(list);
    CljObject *current = ld ? LIST_FIRST(ld) : clj_nil();
    for (int i = 0; i < n && current; i++) {
        current = (CljObject*)LIST_REST((CljList*)current->as.data);
    }
    return current ? current : clj_nil();
}

int list_count(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return 0;
    
    int count = 0;
    CljList *current = as_list(list);
    while (current && LIST_FIRST(current)) {
        count++;
        current = LIST_REST(current);
    }
    return count;
}

/** Create a list from stack items. Returns new object with RC=1. */
CljObject* make_list_from_stack(CljObject **stack, int count) {
    if (count == 0) return clj_nil();
    
    // Build list from end to start using make_list
    CljList *result = NULL;
    for (int i = count - 1; i >= 0; i--) {
        CljList *new_node = make_list(stack[i], result);
        if (stack[i]) RETAIN(stack[i]);
        result = new_node;
    }
    return (CljObject*)result;
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
    if (count <= 0) return clj_nil();
    
    va_list args;
    va_start(args, count);
    
    // Build list from end to start using make_list
    CljList *result = NULL;
    for (int i = count - 1; i >= 0; i--) {
        int value = va_arg(args, int);
        CljList *new_node = make_list(make_int(value), result);
        result = new_node;
    }
    
    va_end(args);
    return result ? (CljObject*)result : clj_nil();
}
