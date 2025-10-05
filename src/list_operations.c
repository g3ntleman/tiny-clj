#include "list_operations.h"
#include "clj_symbols.h"

// List-Operationen für try/catch
CljObject* list_first(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return NULL;
    if (!list->as.data) return NULL;
    CljList *list_data = as_list(list);
    if (!list_data) return NULL;
    if (!list_data->head) return NULL;
    return list_data->head;
}

CljObject* list_nth(CljObject *list, int n) {
    if (!list || list->type != CLJ_LIST || n < 0) return NULL;
    
    CljList *ld = as_list(list);
    CljObject *current = ld ? ld->head : NULL;
    for (int i = 0; i < n && current; i++) {
        current = ((CljList*)current->as.data)->tail;
    }
    return current;
}

int list_count(CljObject *list) {
    if (!list || list->type != CLJ_LIST) return 0;
    
    int count = 0;
    CljList *ld2 = as_list(list);
    CljObject *current = ld2 ? ld2->head : NULL;
    while (current) {
        count++;
        CljList *cn = as_list(current);
        current = cn ? cn->tail : NULL;
    }
    return count;
}

int is_list(CljObject *v) {
    return v && v->type == CLJ_LIST;
}

int is_symbol(CljObject *v, const char *name) {
    if (!v || v->type != CLJ_SYMBOL || !name) return 0;
    
    // Erstelle Symbol für Vergleich (wird interniert)
    CljObject *compare_symbol = intern_symbol_global(name);
    if (!compare_symbol) return 0;
    
    // Pointer-Vergleich statt String-Vergleich!
    return v == compare_symbol;
}
