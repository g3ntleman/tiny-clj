#include "src/object.h"
#include "src/memory.h"
#include "src/list_operations.h"
#include <stdio.h>

int main() {
    printf("Testing simple list operations...\n");
    
    // Create a simple list manually
    CljList *list = make_list(make_int(1), make_list(make_int(2), make_list(make_int(3), NULL)));
    
    printf("List created, testing list_count...\n");
    int count = list_count((CljObject*)list);
    printf("List count: %d\n", count);
    
    return 0;
}
