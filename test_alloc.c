int main() { 
    #include "memory_profiler.h"
    #include "object.h"
    #include "memory.h"
    
    MEMORY_PROFILER_INIT();
    CljObject *obj = make_int(42);
    MEMORY_PROFILER_PRINT_STATS("Test");
    RELEASE(obj);
    return 0;
}
