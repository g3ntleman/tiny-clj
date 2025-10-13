/*
 * Memory Management for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#ifndef TINY_CLJ_MEMORY_H
#define TINY_CLJ_MEMORY_H

#include "object.h"
#include "memory_hooks.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// REFERENCE COUNTING FUNCTIONS
// ============================================================================

/** Increase reference count if applicable; ignored for singletons/primitives. */
void retain(CljObject *v);

/** Decrease reference count and free at rc==0; ignored for singletons/primitives. */
void release(CljObject *v);

/** Add object to autorelease pool for deferred cleanup. */
CljObject *autorelease(CljObject *v);

// ============================================================================
// AUTORELEASE POOL MANAGEMENT
// ============================================================================

// Forward declaration for autorelease pool structure
typedef struct CljObjectPool CljObjectPool;

/** Push a new autorelease pool; returns pool handle. */
CljObjectPool *autorelease_pool_push();

/** Pop and drain current autorelease pool (most common usage). */
void autorelease_pool_pop(void);

/** Pop and drain specific autorelease pool (advanced usage). */
void autorelease_pool_pop_specific(CljObjectPool *pool);

/** Legacy API: Pop and drain given autorelease pool (backward compatibility). */
void autorelease_pool_pop_legacy(CljObjectPool *pool);

/** Drain all autorelease pools (global cleanup). */
void autorelease_pool_cleanup_all();

/** Check if autorelease pool is active. */
bool is_autorelease_pool_active(void);

/** Get reference count of object (0 for singletons, actual rc for others). */
int get_reference_count(CljObject *obj);

// Convenience macro for scoped autorelease pool usage
#define AUTORELEASE_POOL_SCOPE(name) for (CljObjectPool *(name) = autorelease_pool_push(); (name) != NULL; autorelease_pool_pop_specific(name), (name) = NULL)

// ============================================================================
// MEMORY ALLOCATION MACROS
// ============================================================================

// Allocate `count` objects of type `type` on the heap
#ifdef DEBUG
    #define ALLOC(type, count) ({ \
        type *_alloc_result = (type*) malloc(sizeof(type) * (count)); \
        if (_alloc_result) { \
            memory_hook_trigger(MEMORY_HOOK_OBJECT_CREATION, _alloc_result, sizeof(type) * (count)); \
        } \
        _alloc_result; \
    })
#else
    #define ALLOC(type, count) ((type*) malloc(sizeof(type) * (count)))
#endif

// Allocate and zero-initialize `count` objects of type `type` on the heap
#ifdef DEBUG
    #define ALLOC_ZERO(type, count) ({ \
        type *_alloc_result = (type*) calloc(count, sizeof(type)); \
        if (_alloc_result) { \
            memory_hook_trigger(MEMORY_HOOK_OBJECT_CREATION, _alloc_result, sizeof(type) * (count)); \
        } \
        _alloc_result; \
    })
#else
    #define ALLOC_ZERO(type, count) ((type*) calloc(count, sizeof(type)))
#endif

// ============================================================================
// MEMORY MANAGEMENT MACROS
// ============================================================================

// Clean, simple macros for memory operations
#ifdef DEBUG
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        memory_hook_trigger(MEMORY_HOOK_OBJECT_DESTRUCTION, _tmp, sizeof(CljObject)); \
    } while(0)
    #define RETAIN(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        memory_hook_trigger(MEMORY_HOOK_RETAIN, _tmp, 0); \
        retain(_tmp); \
        _tmp; \
    })
    #define RELEASE(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        memory_hook_trigger(MEMORY_HOOK_RELEASE, _tmp, 0); \
        release(_tmp); \
        _tmp; \
    })
    #define AUTORELEASE(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        if (_tmp != NULL) { \
            memory_hook_trigger(MEMORY_HOOK_AUTORELEASE, _tmp, 0); \
            autorelease(_tmp); \
        } \
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
    
    // Fluent autorelease pool macro
    #define WITH_AUTORELEASE_POOL(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        autorelease_pool_push(); \
        code; \
        autorelease_pool_pop(); \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
    
    // Reference count macro for testing
    #define REFERENCE_COUNT(obj) get_reference_count(obj)
    
    // Fluent autorelease pool macro with EvalState management
    #define WITH_AUTORELEASE_POOL_EVAL(code) do { \
        EvalState *eval_state = evalstate_new(); \
        code; \
        evalstate_free(eval_state); \
    } while(0)
    
    // Fluent time profiling macro (for benchmarks)
    #define WITH_TIME_PROFILING(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        code; \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
#else
    // No-op macros for release builds
    #define DEALLOC(obj) ((void)0)
    #define RETAIN(obj) ({ CljObject* _tmp = (CljObject*)(obj); retain(_tmp); _tmp; })
    #define RELEASE(obj) ({ CljObject* _tmp = (CljObject*)(obj); release(_tmp); _tmp; })
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
    #define WITH_AUTORELEASE_POOL(code) do { code } while(0)
    #define WITH_AUTORELEASE_POOL_EVAL(code) do { code } while(0)
    #define WITH_TIME_PROFILING(code) do { code } while(0)
    #define REFERENCE_COUNT(obj) get_reference_count(obj)
#endif

#ifdef __cplusplus
}
#endif

#endif // TINY_CLJ_MEMORY_H
