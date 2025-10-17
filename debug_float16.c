#include "src/value.h"
#include <stdio.h>

int main() {
    CljValue val = make_float16(3.14f);
    printf("val = %p\n", (void*)val);
    printf("get_tag(val) = %d\n", get_tag(val));
    printf("TAG_FLOAT16 = %d\n", TAG_FLOAT16);
    printf("is_float16(val) = %d\n", is_float16(val));
    return 0;
}
