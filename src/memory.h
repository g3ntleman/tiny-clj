/*
 * Memory Management for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#ifndef TINY_CLJ_MEMORY_H
#define TINY_CLJ_MEMORY_H

#include "object.h"
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

/** Check if a pointer points to stack memory. */
bool is_pointer_on_stack(const void *ptr);

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


/** Drain all autorelease pools (global cleanup). */
void autorelease_pool_cleanup_all();

/** Check if autorelease pool is active. */
bool is_autorelease_pool_active(void);

/** Get reference count of object (0 for singletons, actual rc for others). */
int get_retain_count(CljObject *obj);

// Convenience macro for scoped autorelease pool usage
#define AUTORELEASE_POOL_SCOPE(name) for (CljObjectPool *(name) = autorelease_pool_push(); (name) != NULL; autorelease_pool_pop_specific(name), (name) = NULL)

// ============================================================================
// MEMORY ALLOCATION MACROS
// ============================================================================

// Allocate `count` objects of type `type` on the heap
// Note: ALLOC should only be used for CljObject subtypes per MEMORY_POLICY
#ifdef DEBUG
    #define ALLOC(type, count) ((type*) alloc(sizeof(type), (count), TYPE_OF(type)))
#else
    #define ALLOC(type, count) ((type*) malloc(sizeof(type) * (count)))
#endif

// Allocate and zero-initialize `count` objects of type `type` on the heap
// Note: ALLOC_ZERO should only be used for CljObject subtypes per MEMORY_POLICY
#ifdef DEBUG
    #define ALLOC_ZERO(type, count) ((type*) alloc_zero(sizeof(type), (count), TYPE_OF(type)))
#else
    #define ALLOC_ZERO(type, count) ((type*) calloc(count, sizeof(type)))
#endif

// Special allocation for CljObject with dynamic type
#ifdef DEBUG
    #define ALLOC_OBJECT(obj_type) ((CljObject*) alloc(sizeof(CljObject), 1, obj_type))
#else
    #define ALLOC_OBJECT(obj_type) ((CljObject*) malloc(sizeof(CljObject)))
#endif


// ============================================================================
// MEMORY MANAGEMENT MACROS
// ============================================================================

// Clean, simple macros for memory operations
#ifdef DEBUG
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        memory_profiler_track_object_destruction(_tmp); \
    } while(0)
    #define RETAIN(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        retain(_tmp); \
        _tmp; \
    })
    #define RELEASE(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        release(_tmp); \
        _tmp; \
    })
    #define AUTORELEASE(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        if (_tmp != NULL) { \
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
                retain(_tmp); \
            } \
            if ((var) != NULL) { \
                release(var); \
            } \
            (var) = _tmp; \
        } \
    } while(0)
    
    // Fluent autorelease pool macro
    #define WITH_AUTORELEASE_POOL(code) do { \
        autorelease_pool_push(); \
        code; \
        autorelease_pool_pop(); \
    } while(0)
    
    // Retain count macro for testing
    #define REFERENCE_COUNT(obj) get_retain_count(obj)
    
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
    #define REFERENCE_COUNT(obj) get_retain_count(obj)
    
#endif

// ============================================================================
// MEMORY ALLOCATION FUNCTIONS
// ============================================================================

/**
 * @brief Allocate memory with automatic profiling for CljObject types
 * @param type_size Size of the type to allocate
 * @param count Number of elements to allocate
 * @param obj_type Type of the CljObject (for singleton filtering)
 * @return Pointer to allocated memory
 */
void* alloc(size_t type_size, size_t count, CljType obj_type);

/**
 * @brief Allocate and zero-initialize memory with automatic profiling for CljObject types
 * @param type_size Size of the type to allocate
 * @param count Number of elements to allocate
 * @param obj_type Type of the CljObject (for singleton filtering)
 * @return Pointer to allocated memory
 */
void* alloc_zero(size_t type_size, size_t count, CljType obj_type);


#ifdef __cplusplus
}
#endif

#endif // TINY_CLJ_MEMORY_H
