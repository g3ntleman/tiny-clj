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
// CljObject is already defined in CljObject.h
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
    #define RETAIN(obj) ({ \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_RETAIN, _tmp, 0); \
        retain(_tmp); \
        _tmp; \
    })
    #define RELEASE(obj) ({ \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_RELEASE, _tmp, 0); \
        release(_tmp); \
        _tmp; \
    })
    #define AUTORELEASE(obj) ({ \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_AUTORELEASE, _tmp, 0); \
        autorelease(_tmp); \
        _tmp; \
    })
    /** @brief Safe object assignment with automatic retain/release management.
     *  @param var Variable to assign to
     *  @param new_obj New object to assign (can be NULL)
     *  Follows classic Objective-C pattern: retains new object, releases old one.
     */
    #define ASSIGN(var, new_obj) do { \
        typeof(var) _tmp = (new_obj); \
        if (_tmp != (var)) { \
            if (_tmp != NULL) { \
                memory_hook_trigger(MEMORY_HOOK_RETAIN, _tmp, 0); \
                retain(_tmp); \
            } \
            if ((var) != NULL) { \
                memory_hook_trigger(MEMORY_HOOK_RELEASE, (var), 0); \
                release(var); \
            } \
            (var) = _tmp; \
        } \
    } while(0)
    
    // Test macros for backward compatibility
    // MEMORY_TEST_START/END are defined in memory_profiler.h
    
    // Fluent memory profiling macro
    #define WITH_MEMORY_PROFILING(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        cljvalue_pool_push(); \
        code; \
        cljvalue_pool_cleanup_all(); \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
    
    // Fluent memory profiling macro with EvalState management
    #define WITH_MEMORY_PROFILING_EVAL(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        cljvalue_pool_push(); \
        EvalState *eval_state = evalstate_new(); \
        code; \
        evalstate_free(eval_state); \
        cljvalue_pool_cleanup_all(); \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
    
    // Fluent time profiling macro (for benchmarks)
    #define WITH_TIME_PROFILING(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        code; \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
#else
    // No-op macros for release builds
    #define CREATE(obj) ((void)0)
    #define DEALLOC(obj) ((void)0)
    #define RETAIN(obj) ({ typeof(obj) _tmp = (obj); retain(_tmp); _tmp; })
    #define RELEASE(obj) ({ typeof(obj) _tmp = (obj); release(_tmp); _tmp; })
    #define AUTORELEASE(obj) (obj)
    #define ASSIGN(var, new_obj) do { \
        typeof(var) _tmp = (new_obj); \
        if (_tmp != (var)) { \
            if (_tmp != NULL) retain(_tmp); \
            if ((var) != NULL) release(var); \
            (var) = _tmp; \
        } \
    } while(0)
    
    // No-op test macros for release builds
    // MEMORY_TEST_START/END are defined in memory_profiler.h
    #define WITH_MEMORY_PROFILING(code) do { code } while(0)
    #define WITH_TIME_PROFILING(code) do { code } while(0)
#endif

#endif // TINY_CLJ_MEMORY_HOOKS_H
