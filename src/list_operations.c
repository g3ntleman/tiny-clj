#include "list_operations.h"
#include "clj_symbols.h"
#include "memory_hooks.h"

// List-Operationen für try/catch
CljObject* list_first(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return clj_nil();
    if (!list->as.data) return clj_nil();
    CljList *list_data = as_list(list);
    if (!list_data) return clj_nil();
    if (!list_data->head) return clj_nil();
    return list_data->head;
}

CljObject* list_nth(CljObject *list, int n) {
    if (!list || list->type != CLJ_LIST || n < 0) return clj_nil();
    
    CljList *ld = as_list(list);
    CljObject *current = ld ? ld->head : clj_nil();
    for (int i = 0; i < n && current; i++) {
        current = ((CljList*)current->as.data)->tail;
    }
    return current ? current : clj_nil();
}

int list_count(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return 0;
    
    int count = 0;
    CljList *current = as_list(list);
    while (current && current->head) {
        count++;
        current = as_list(current->tail);
    }
    return count;
}

CljObject* list_from_stack(CljObject **stack, int count) {
    if (count == 0) return clj_nil();
    CljObject *prev = NULL;
    for (int i = count - 1; i >= 0; i--) {
        CljObject *node = make_list();
        CljList *node_list = as_list(node);
        if (!node_list) return clj_nil();
        node_list->head = stack[i];
        node_list->tail = prev;
        prev = node;
        if (stack[i]) RETAIN(stack[i]);
    }
    return prev ? prev : clj_nil();
}

bool is_list(CljObject *v) {
    return v && v->type == CLJ_LIST;
}

bool is_symbol(CljObject *v, const char *name) {
    if (!v || v->type != CLJ_SYMBOL || !name) return false;
    
    // Erstelle Symbol für Vergleich (wird interniert)
    CljObject *compare_symbol = intern_symbol_global(name);
    if (!compare_symbol) return false;
    
    // Pointer-Vergleich statt String-Vergleich!
    return v == compare_symbol;
}
