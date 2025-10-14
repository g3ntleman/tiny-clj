#include <stdio.h>
#include "memory.h"

int main() {
    printf("sizeof(CljObject) = %zu
", sizeof(CljObject));
    CljObject *obj = ALLOC(CljObject, 1);
    printf("obj = %p
", obj);
    return 0;
}
