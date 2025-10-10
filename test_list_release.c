#include "src/object.h"
#include "src/object.c"
#include "src/seq.c"
#include "src/vector.c"
#include "src/string.c"
#include "src/list_operations.c"
#include "src/clj_symbols.c"
#include "src/memory_profiler.c"
#include "src/memory_hooks.c"
#include "src/types.c"
#include "src/namespace.c"
#include "src/clj_parser.c"
#include "src/reader.c"
#include "src/function_call.c"
#include "src/builtins.c"
#include "src/runtime.c"
#include "src/exception.c"
#include "src/map.c"
#include <stdio.h>

int main() {
    printf("Testing list release...\n");
    
    // Test 1: Empty list
    printf("Test 1: Empty list\n");
    CljList *empty_list = make_list(NULL, NULL);
    printf("Empty list created: %p\n", empty_list);
    printf("Empty list type: %d\n", empty_list->base.type);
    printf("Empty list rc: %d\n", empty_list->base.rc);
    printf("Empty list head: %p\n", empty_list->head);
    printf("Empty list tail: %p\n", empty_list->tail);
    
    printf("Releasing empty list...\n");
    release((CljObject*)empty_list);
    printf("Empty list released successfully\n");
    
    // Test 2: List with one element
    printf("\nTest 2: List with one element\n");
    CljObject *int_obj = make_int(42);
    CljList *single_list = make_list(int_obj, NULL);
    printf("Single list created: %p\n", single_list);
    printf("Single list head: %p\n", single_list->head);
    printf("Single list tail: %p\n", single_list->tail);
    
    printf("Releasing single list...\n");
    release((CljObject*)single_list);
    printf("Single list released successfully\n");
    
    printf("All tests passed!\n");
    return 0;
}
