#include "src/common.h"
#include <stdio.h>

void test_function() {
    printf("Testing assertion with stack trace...\n");
    CLJ_ASSERT(1 == 0);  // This will fail and show stack trace
}

int main() {
    printf("Starting assertion test...\n");
    test_function();
    return 0;
}


