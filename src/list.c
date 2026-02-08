/**
 * @file list.c
 * @brief Linked list implementation with debug tracing.
 */

#include "list.h"
#include "memory.h"
#include "value.h"
#include "symbol.h"
#include "object.h"
#include "exception.h"
#include "types.h"
#include "subjective-c/debug_trace.h"
#include "subjective-c/mini_format.h"
#include <stdio.h>

extern const char* clj_type_name(CljType type);

#ifdef DEBUG
static const DebugTraceConfig list_trace_cfg = {
    .env_var_name = "TINYCLJ_TRACE_LIST_ALLOC",
    .env_var_bt_name = "TINYCLJ_TRACE_LIST_ALLOC_BT",
    .prefix = "[list-alloc]",
    .max_traces = 200
};
#endif

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

/**
 * @brief Return empty list singleton.
 * @return Empty list singleton (rc=SINGLETON_RC, never freed)
 */
CljList* empty_list(void) {
    return clj_empty_list_singleton;
}

/**
 * @brief Allocate list node.
 * @param first First element (retained)
 * @param rest Rest of list (retained)
 * @return List with rc=1, caller must release
 */
CljList* make_list(ID first, CljList *rest) {
    CljList *list = ALLOC(CljList, 1);
    if (!list) throw_oom();

    list->base.type = CLJ_LIST;
    list->first = RETAIN(first);
    list->rest = RETAIN(rest);

#ifdef DEBUG
    static int trace_count = 0;
    debug_trace_allocation(&list_trace_cfg, (void*)list, (void*)list->first, (void*)list->rest, &trace_count);
#endif

    return list;
}

#ifdef DEBUG
/**
 * @brief Cast object to list with type checking (debug only).
 * @param obj Object to cast
 * @return List pointer or NULL, throws exception on type mismatch
 */
CljList* as_list_checked(ID obj) {
    // Happy path: obj is not NULL and has correct type
    if (obj && is_list_type(TAG(obj))) {
        return (CljList*)obj;  // Direct return, no jumps
    }
    // NULL is valid (e.g., end of list) - return NULL
    if (!obj) {
        return NULL;
    }
    // Error case: wrong type
    char error_msg[128];
    const char *type_name = clj_type_name(((CljObject*)obj)->type);
    mini_snprintf(error_msg, sizeof(error_msg),
            "Type mismatch: expected List, got %s",
            type_name);
    printf("[STACKTRACE] as_list failed at %s:%d - obj=%p, type=%d (%s)\n", __FILE__, __LINE__, obj, ((CljObject*)obj)->type, type_name);
    // Print stacktrace
    #if defined(__GNUC__) && !defined(ESP32_BUILD)
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

/**
 * @brief Get nth element from list (0-indexed).
 * @param list List to traverse
 * @param n Index (must be >= 0)
 * @return Element at index, throws exception if out of bounds
 */
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
    for (int i = 0; i <= n && current && is_list_type(TAG(current)); i++) {
        if (i == n) {
            CljList *current_list = as_list(current);
            // Element found - return it (may be NULL if element is nil)
            return LIST_FIRST(current_list);  // Return directly - no additional memory management
        }
        CljList *current_list = as_list(current);
        current = LIST_REST(current_list);
        if (current && !is_list_type(TAG(current))) {
            current = NULL; // Stop if rest is not a list
        }
    }

    // Index not found - out of bounds
    throw_exception_formatted(EXCEPTION_INDEX_OUT_OF_BOUNDS, __FILE__, __LINE__, 0,
                              "nth index %d is out of bounds for list", n);
    return NULL;
}

/**
 * @brief Count elements in list (O(n) traversal).
 * @param list List to count
 * @return Number of elements, prefer LIST_FOR_EACH in hot paths
 */
int list_count(CljList *list) {
    if (!list) return 0;

    // Programmierfehler: list muss CLJ_LIST sein, wenn es nicht NULL ist
    CLJ_ASSERT(is_list_type(TAG(list)));

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

/**
 * @brief Get element at index (deprecated, use list_nth instead).
 * @param list List to traverse
 * @param index Index (0-based)
 * @return Element or NULL (ambiguous: nil element vs out-of-bounds)
 */
CljObject* list_get_element(CljList *list, int index) {
    if (!list || index < 0) return NULL;
    CljList *node = list;
    if (index == 0) return LIST_FIRST(node);
    int i = 0;
    while (i < index) {
        CljObject *rest = LIST_REST(node);
        if (!rest || !is_list_type(TAG(rest))) return NULL;
        node = as_list(rest);
        i++;
    }
    return LIST_FIRST(node);
}
