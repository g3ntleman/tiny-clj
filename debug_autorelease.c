#include "memory.h"
#include "string.h"
#include "object.h"
#include <stdio.h>

int main() { 
    CljObject *obj = make_string("test");
    printf("Before AUTORELEASE: rc=%d
", obj->rc);
    AUTORELEASE(obj);
    printf("After AUTORELEASE: rc=%d
", obj->rc);
    printf("Pool active: %s
", g_cv_pool_top ? "YES" : "NO");
    return 0;
}
