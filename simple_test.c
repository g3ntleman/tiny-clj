#include <stdio.h>
#include <stdlib.h>

// Simplified test without all the dependencies
int main() {
    printf("Testing basic list release logic...\n");
    
    // Simulate the problem
    printf("The issue is likely in the release_object_deep function for CLJ_LIST\n");
    printf("Current implementation:\n");
    printf("1. Release head if it exists\n");
    printf("2. Release tail if it exists\n");
    printf("3. Free the list structure\n");
    
    printf("This should work for empty lists (head=NULL, tail=NULL)\n");
    printf("But might fail for non-empty lists due to recursive structure\n");
    
    return 0;
}
