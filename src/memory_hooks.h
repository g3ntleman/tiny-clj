/*
 * Memory Hooks for Clean Profiling
 * 
 * This header provides a clean way to hook into memory operations
 * without polluting the main code with profiling calls.
 */

#ifndef TINY_CLJ_MEMORY_HOOKS_H
#define TINY_CLJ_MEMORY_HOOKS_H

#include <stddef.h>
#include <stdbool.h>

// Forward declaration for CljObject (avoid redefinition)
#ifndef CLJ_OBJECT_DEFINED
struct CljObject;
typedef struct CljObject CljObject;
#endif

// Memory operation types
typedef enum {
    MEMORY_HOOK_ALLOCATION,
    MEMORY_HOOK_DEALLOCATION,
    MEMORY_HOOK_OBJECT_CREATION,
    MEMORY_HOOK_OBJECT_DESTRUCTION,
    MEMORY_HOOK_RETAIN,
    MEMORY_HOOK_RELEASE,
    MEMORY_HOOK_AUTORELEASE
} MemoryHookType;

// Hook function type
typedef void (*MemoryHookFunc)(MemoryHookType type, void *ptr, size_t size);

// Hook management
void memory_hooks_init(void);
void memory_hooks_cleanup(void);
void memory_hooks_register(MemoryHookFunc hook);
void memory_hooks_unregister(void);

// Hook triggers (to be called from memory operations)
void memory_hook_trigger(MemoryHookType type, void *ptr, size_t size);

// Memory profiling integration
void memory_profiling_init_with_hooks(void);
void memory_profiling_cleanup_with_hooks(void);

// Test helpers (for backward compatibility)
void memory_test_start(const char *test_name);
void memory_test_end(const char *test_name);

// Clean, simple macros for memory operations
#ifdef DEBUG
    #define CREATE(obj) do { \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_OBJECT_CREATION, _tmp, sizeof(CljObject)); \
    } while(0)
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_OBJECT_DESTRUCTION, _tmp, sizeof(CljObject)); \
    } while(0)
    #define RETAIN(obj) do { \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_RETAIN, _tmp, 0); \
        retain(_tmp); \
    } while(0)
    #define RELEASE(obj) do { \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_RELEASE, _tmp, 0); \
        release(_tmp); \
    } while(0)
    #define AUTORELEASE(obj) ({ \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_AUTORELEASE, _tmp, 0); \
        autorelease(_tmp); \
        _tmp; \
    })
    
    // Test macros for backward compatibility
    #define MEMORY_TEST_START(name) memory_test_start(name)
    #define MEMORY_TEST_END(name) memory_test_end(name)
#else
    // No-op macros for release builds
    #define CREATE(obj) ((void)0)
    #define DEALLOC(obj) ((void)0)
    #define RETAIN(obj) ((void)0)
    #define RELEASE(obj) ((void)0)
    #define AUTORELEASE(obj) (obj)
    
    // No-op test macros for release builds
    #define MEMORY_TEST_START(name) ((void)0)
    #define MEMORY_TEST_END(name) ((void)0)
#endif

#endif // TINY_CLJ_MEMORY_HOOKS_H
