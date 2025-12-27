/*
 * Debug utilities for tiny-clj
 * 
 * Debug functions for printing internal data structures.
 */

#include "debug.h"
#include "list.h"
#include "map.h"
#include "object.h"
#include "to_string.h"
#include "subjective-c/strings.h"
#include <stdio.h>

// Debug function to print env_stack
void debug_print_env_stack(const char *label, CljList *env_stack) {
    if (!env_stack) {
        fprintf(stderr, "[DEBUG] %s: env_stack is NULL\n", label);
        return;
    }
    fprintf(stderr, "[DEBUG] %s: env_stack chain:\n", label);
    CljList *current = env_stack;
    int depth = 0;
    while (current && depth < 10) {  // Limit depth to avoid infinite loops
        ID first = LIST_FIRST(current);
        CljString *first_str = to_string(first);
        fprintf(stderr, "  [%d] %s\n", depth, first_str ? string_data(first_str) : "<null>");
        RELEASE(first_str);
        ID rest = LIST_REST(current);
        if (rest && list_type_matches(TAG(rest))) {
            current = (CljList*)rest;
            depth++;
        } else {
            break;
        }
    }
    if (depth >= 10) {
        fprintf(stderr, "  ... (truncated at depth 10)\n");
    }
}
