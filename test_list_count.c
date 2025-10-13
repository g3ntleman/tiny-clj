#include "src/object.h"
#include "src/memory.h"
#include "src/list_operations.h"
#include "src/parser.h"
#include <stdio.h>

int main() {
    printf("Testing list_count with (+ 2 3)...\n");
    
    // Create a simple list (+ 2 3)
    CljObject *list = make_list(make_symbol("+"), make_list(make_int(2), make_list(make_int(3), clj_nil())));
    
    printf("List created, testing list_count...\n");
    int count = list_count(list);
    printf("List count: %d\n", count);
    
    return 0;
}
