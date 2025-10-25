/*
 * Memory Management for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#ifndef TINY_CLJ_MEMORY_H
#define TINY_CLJ_MEMORY_H

// Forward declaration for IS_IMMEDIATE macro
// IS_IMMEDIATE is defined in value.h but we can't include it here due to circular dependency

#include "object.h"
#include "memory_profiler.h"
#include <stdbool.h>

// ============================================================================
// REFERENCE COUNTING FUNCTIONS
// ============================================================================

/** @brief Increase reference count if applicable; ignored for singletons/primitives.
 *  @param v Object to retain
 *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
 */
void retain(CljObject *v);

/** @brief Decrease reference count and free at rc==0; ignored for singletons/primitives.
 *  @param v Object to release
 *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
 */
void release(CljObject *v);

/** @brief Enable memory debug output after initialization
 * 
 * Call this after memory profiling is initialized to enable debug output.
 * This prevents debug output during the initialization phase.
 */
void enable_memory_debug_output(void);

/** @brief Add object to autorelease pool for deferred cleanup.
 *  @param v Object to autorelease
 *  @return The same object (for chaining)
 *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
 */
CljObject *autorelease(CljObject *v);

/** @brief Check if a pointer points to stack memory.
 *  @param ptr Pointer to check
 *  @return true if pointer is on stack, false otherwise
 */
bool is_pointer_on_stack(const void *ptr);

/**
 * @brief Throw OutOfMemoryError with type tag and abort
 * @param type The CljType being allocated (e.g., CLJ_BYTE_ARRAY)
 * @note This function never returns
 */
void throw_oom(CljType type) __attribute__((noreturn));

// ============================================================================
// AUTORELEASE POOL MANAGEMENT
// ============================================================================

// Forward declaration for autorelease pool structure
typedef struct CljObjectPool CljObjectPool;

/** @brief Push a new autorelease pool; returns pool handle.
 *  @return Pool handle for later use with autorelease_pool_pop()
 *  @note Use WITH_AUTORELEASE_POOL macro for automatic cleanup
 */
CljObjectPool *autorelease_pool_push();


/** @brief Exception-safe cleanup after longjmp/setjmp.
 *  @note Called automatically by exception handling system
 */
void autorelease_pool_cleanup_after_exception();

/** @brief Drain all autorelease pools (global cleanup).
 *  @note Use only for emergency cleanup or shutdown
 */
void autorelease_pool_cleanup_all();

/** @brief Check if autorelease pool is active.
 *  @return true if at least one pool is active
 */
bool is_autorelease_pool_active(void);

/** @brief Internal function to pop autorelease pool.
 *  @param pool Pool to pop
 *  @note Used by WITH_AUTORELEASE_POOL macro
 */
void autorelease_pool_pop(CljObjectPool *pool);

/** @brief Get reference count of object (0 for singletons, actual rc for others).
 *  @param obj Object to check
 *  @return Reference count (0 for immediate values and singletons)
 */
int get_retain_count(CljObject *obj);

// AUTORELEASE_POOL_SCOPE removed - use WITH_AUTORELEASE_POOL instead
// The for-loop implementation was incompatible with setjmp/longjmp

// ============================================================================
// MEMORY ALLOCATION MACROS
// ============================================================================

/** @brief Allocate `count` objects of type `type` on the heap.
 *  @param type Type to allocate (must be CljObject subtype)
 *  @param count Number of objects to allocate
 *  @return Pointer to allocated memory
 *  @note Only use for CljObject subtypes per MEMORY_POLICY
 */
#define ALLOC(type, count) ((type*) alloc(sizeof(type), (count), TYPE_OF(type)))

/** @brief Allocate and zero-initialize `count` objects of type `type` on the heap.
 *  @param type Type to allocate (must be CljObject subtype)
 *  @param count Number of objects to allocate
 *  @return Pointer to zero-initialized allocated memory
 *  @note Only use for CljObject subtypes per MEMORY_POLICY
 */
#define ALLOC_ZERO(type, count) ((type*) alloc_zero(sizeof(type), (count), TYPE_OF(type)))

/** @brief Special allocation for CljObject with dynamic type.
 *  @param obj_type Type of CljObject to allocate
 *  @return Pointer to allocated CljObject
 *  @note Used for dynamic object creation
 */
#define ALLOC_SIMPLE(obj_type) ((CljObject*) alloc(sizeof(CljObject), 1, obj_type))

// ============================================================================
// MEMORY MANAGEMENT MACROS
// ============================================================================

// Clean, simple macros for memory operations
#ifdef DEBUG
    /** @brief Deallocate object with memory profiling (DEBUG builds).
     *  @param obj Object to deallocate
     *  @note Tracks object destruction for memory profiling
     */
    #define DEALLOC(obj) do { \
        typeof(obj) _tmp = (obj); \
        if (_tmp && (void*)_tmp != (void*)0x1 && !IS_IMMEDIATE(_tmp)) { \
            memory_profiler_track_object_destruction((CljObject*)_tmp); \
        } \
    } while(0)
    
    /** @brief Retain object (safe for immediate values).
     *  @param obj Object to retain
     *  @return Same object (for chaining)
     *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
     */
    #define RETAIN(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            CljObject* _tmp = (CljObject*)_id; \
            retain(_tmp); \
        } \
        (CljObject*)_id; \
    })
    
    /** @brief Release object (safe for immediate values).
     *  @param obj Object to release
     *  @return Same object (for chaining)
     *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
     */
    #define RELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            CljObject* _tmp = (CljObject*)_id; \
            release(_tmp); \
        } \
        (CljObject*)_id; \
    })
    
    /** @brief Autorelease object (safe for immediate values).
     *  @param obj Object to autorelease
     *  @return Same object (for chaining)
     *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
     */
    #define AUTORELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            CljObject* _tmp = (CljObject*)_id; \
            autorelease(_tmp); \
        } \
        (CljObject*)_id; \
    })
    
    /** @brief Foundation-style autorelease pool - compatible with setjmp/longjmp.
     *  @param code Code block to execute within autorelease pool
     *  @note Like NSAutoreleasePool in pre-ARC Objective-C
     */
    #define WITH_AUTORELEASE_POOL(code) do { \
        CljObjectPool *_pool = autorelease_pool_push(); \
        code; \
        autorelease_pool_pop(_pool); \
    } while(0)
    
    /** @brief Exception-safe autorelease pool macro for TRY/CATCH blocks.
     *  @param code Code block to execute within autorelease pool
     *  @param catch_code Exception handler code block
     *  @note Catches exceptions, pops pool, then re-throws exception
     *  @note Requires exception.h to be included BEFORE this header for TRY/CATCH macros
     *  @note Usage: Include exception.h first, then use this macro in tests
     */
    #define WITH_AUTORELEASE_POOL_TRY_CATCH(code, catch_code) do { \
        CljObjectPool *_pool = autorelease_pool_push(); \
        TRY { \
            code; \
            autorelease_pool_pop(_pool); \
        } CATCH(ex) { \
            autorelease_pool_pop(_pool); \
            catch_code; \
        } END_TRY; \
    } while(0)
    
    /** @brief Simple autorelease pool management for TRY/CATCH.
     *  @note Use pattern: AUTORELEASE_POOL_BEGIN(); ... code ...; AUTORELEASE_POOL_END();
     */
    #define AUTORELEASE_POOL_BEGIN() CljObjectPool *_pool = autorelease_pool_push()
    #define AUTORELEASE_POOL_END() autorelease_pool_pop(_pool)
    
    /** @brief Retain count macro for testing.
     *  @param obj Object to check
     *  @return Reference count
     */
    #define REFERENCE_COUNT(obj) get_retain_count(obj)
    
    
    /** @brief Memory test wrapper macro (recommended).
     *  @param code Code block to profile
     *  @note Tracks memory usage for the current function
     */
    #define WITH_MEMORY_PROFILING(code) do { \
        MEMORY_TEST_START(__FUNCTION__); \
        code; \
        MEMORY_TEST_END(__FUNCTION__); \
    } while(0)
    
    /** @brief New name for time/memory test wrapper (alias).
     *  @param code Code block to profile
     */
    #define WITH_MEMORY_TEST(code) WITH_MEMORY_PROFILING(code)
    
    /** @brief Legacy alias maintained for compatibility.
     *  @param code Code block to profile
     */
    #define WITH_TIME_PROFILING(code) WITH_MEMORY_PROFILING(code)
#else
    /** @brief Deallocate object (RELEASE builds - no profiling).
     *  @param obj Object to deallocate
     *  @note Calls free() directly without memory profiling
     */
    #define DEALLOC(obj) do { \
        if ((obj) && !IS_IMMEDIATE(obj)) { \
            free((obj)); \
        } \
    } while(0)
    
    /** @brief Retain object (safe for immediate values).
     *  @param obj Object to retain
     *  @return Same object (for chaining)
     *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
     */
    #define RETAIN(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            CljObject* _tmp = (CljObject*)_id; \
            retain(_tmp); \
        } \
        (CljObject*)_id; \
    })
    
    /** @brief Release object (safe for immediate values).
     *  @param obj Object to release
     *  @return Same object (for chaining)
     *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
     */
    #define RELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            CljObject* _tmp = (CljObject*)_id; \
            release(_tmp); \
        } \
        (CljObject*)_id; \
    })
    
    /** @brief Autorelease object (safe for immediate values).
     *  @param obj Object to autorelease
     *  @return Same object (for chaining)
     *  @note Safe to call on immediate values (fixnums, chars, booleans, nil)
     */
    #define AUTORELEASE(obj) ({ \
        ID _id = (obj); \
        if (!IS_IMMEDIATE(_id)) { \
            CljObject* _tmp = (CljObject*)_id; \
            autorelease(_tmp); \
        } \
        (CljObject*)_id; \
    })
    
    /** @brief Foundation-style autorelease pool for release builds.
     *  @param code Code block to execute within autorelease pool
     */
    #define WITH_AUTORELEASE_POOL(code) do { \
        CljObjectPool *_pool = autorelease_pool_push(); \
        code; \
        autorelease_pool_pop(_pool); \
    } while(0)
    
    /** @brief Simple autorelease pool management for TRY/CATCH (release builds).
     *  @note Use pattern: AUTORELEASE_POOL_BEGIN(); ... code ...; AUTORELEASE_POOL_END();
     */
    #define AUTORELEASE_POOL_BEGIN() CljObjectPool *_pool = autorelease_pool_push()
    #define AUTORELEASE_POOL_END() autorelease_pool_pop(_pool)
    
    /** @brief No-op macros for release builds (no profiling).
     *  @param code Code block (ignored in release builds)
     */
    #define WITH_AUTORELEASE_POOL_EVAL(code) do { code } while(0)
    #define WITH_MEMORY_PROFILING(code) do { code } while(0)
    #define WITH_MEMORY_TEST(code) WITH_MEMORY_PROFILING(code)
    #define WITH_TIME_PROFILING(code) WITH_MEMORY_PROFILING(code)
    #define REFERENCE_COUNT(obj) get_retain_count(obj)
#endif

/** @brief Safe object assignment with automatic retain/release management.
 *  @param var Variable to assign to
 *  @param new_obj New object to assign (can be NULL)
 *  Follows classic Objective-C pattern: retains new object, releases old one.
 *  Works in both DEBUG and RELEASE builds using RETAIN/RELEASE macros.
 */
#define ASSIGN(var, new_obj) do { \
    CljObject *_tmp = (new_obj); \
    if (_tmp != (var)) { \
        RETAIN(_tmp); \
        RELEASE(var); \
        (var) = _tmp; \
    } \
} while(0)

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

#endif // TINY_CLJ_MEMORY_H