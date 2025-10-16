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

// CFAutoreleasePool: Exception-safe cleanup
void autorelease_pool_cleanup_after_exception();

/** Pop and drain specific autorelease pool (advanced usage). */
void autorelease_pool_pop_specific(CljObjectPool *pool);


/** Drain all autorelease pools (global cleanup). */
void autorelease_pool_cleanup_all();

/** Check if autorelease pool is active. */
bool is_autorelease_pool_active(void);

/** Get reference count of object (0 for singletons, actual rc for others). */
int get_retain_count(CljObject *obj);

// AUTORELEASE_POOL_SCOPE removed - use WITH_AUTORELEASE_POOL instead
// The for-loop implementation was incompatible with setjmp/longjmp

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
    
    // CFAutoreleasePool: Exception-safe autorelease pool macro
    // Compatible with setjmp/longjmp by using stack-based management
    #define WITH_AUTORELEASE_POOL(code) do { \
        CljObjectPool *_pool = autorelease_pool_push(); \
        bool _pool_cleaned_up = false; \
        do { \
            code; \
        } while(0); \
        if (!_pool_cleaned_up) { \
            autorelease_pool_pop_specific(_pool); \
        } \
    } while(0)
    
    // Exception-safe autorelease pool macro for TRY/CATCH blocks
    // This macro is compatible with longjmp by not using block scopes
    #define WITH_AUTORELEASE_POOL_TRY_CATCH(code) do { \
        CljObjectPool *_pool = autorelease_pool_push(); \
        code; \
        autorelease_pool_pop_specific(_pool); \
    } while(0)
    
    // Simple autorelease pool management for TRY/CATCH
    // Use this pattern: AUTORELEASE_POOL_BEGIN(); ... code ...; AUTORELEASE_POOL_END();
    #define AUTORELEASE_POOL_BEGIN() CljObjectPool *_pool = autorelease_pool_push()
    #define AUTORELEASE_POOL_END() autorelease_pool_pop_specific(_pool)
    
    // CFAutoreleasePool: Exception-safe pool for TRY/CATCH
    #define CFAUTORELEASE_POOL_SCOPE(var, code) do { \
        CljObjectPool *var = autorelease_pool_push(); \
        do { \
            code; \
        } while(0); \
        autorelease_pool_pop_specific(var); \
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
    #define AUTORELEASE(obj) ({ \
        CljObject* _tmp = (CljObject*)(obj); \
        if (_tmp != NULL) { \
            autorelease(_tmp); \
        } \
        _tmp; \
    })
    #define ASSIGN(var, new_obj) do { \
        typeof(var) _tmp = (new_obj); \
        if (_tmp != (var)) { \
            if (_tmp != NULL) retain(_tmp); \
            if ((var) != NULL) release(var); \
            (var) = _tmp; \
        } \
    } while(0)
    
    // Autorelease pool macros for release builds
    #define WITH_AUTORELEASE_POOL(code) do { \
        CljObjectPool *_pool = autorelease_pool_push(); \
        bool _pool_cleaned_up = false; \
        do { \
            code; \
        } while(0); \
        if (!_pool_cleaned_up) { \
            autorelease_pool_pop_specific(_pool); \
        } \
    } while(0)
    
    // Simple autorelease pool management for TRY/CATCH (release builds)
    #define AUTORELEASE_POOL_BEGIN() CljObjectPool *_pool = autorelease_pool_push()
    #define AUTORELEASE_POOL_END() autorelease_pool_pop_specific(_pool)
    
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
