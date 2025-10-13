/*
 * Memory Management Implementation for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#include "memory.h"
#include "object.h"
#include "vector.h"
#include "memory_profiler.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// AUTORELEASE POOL IMPLEMENTATION
// ============================================================================

// Autorelease pool structure backed by weak vector for efficiency
struct CljObjectPool { 
    CljObject *backing; 
    struct CljObjectPool *prev; 
};

static CljObjectPool *g_cv_pool_top = NULL;
static int g_pool_push_count = 0;  // Track push/pop balance for error detection

// Forward declaration
static void release_object_deep(CljObject *v);

// ============================================================================
// REFERENCE COUNTING IMPLEMENTATION
// ============================================================================

/** @brief Increment reference count if applicable
 * 
 * @param v Pointer to CljObject to retain (NULL parameters are safely ignored)
 * 
 * Safely handles NULL parameters and singletons. Objects that don't track
 * references (singletons) are ignored. Empty vector/map singletons are
 * also ignored to prevent reference counting issues.
 */
void retain(CljObject *v) {
    if (!v) return;
    
    // Skip singletons (they don't use reference counting)
    if (!TRACKS_REFERENCES(v)) return;
    
    // Skip empty vector/map singletons
    if (is_type(v, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector(v);
        if (vec && vec->base.rc == 0 && vec->data == NULL) return;
    }
    if (is_type(v, CLJ_MAP)) {
        CljMap *map = as_map(v);
        if (map && map->base.rc == 0 && map->data == NULL) return;
    }
    v->rc++;
}

/** @brief Decrement reference count and free if zero
 * 
 * @param v Pointer to CljObject to release (NULL parameters are safely ignored)
 * 
 * Safely handles NULL parameters, singletons, and native functions. Objects
 * that don't track references are ignored. When reference count reaches zero,
 * the object is freed and its deep cleanup is performed.
 */
void release(CljObject *v) {
    if (!v) return;
    
    // Skip singletons (they don't use reference counting)
    if (!TRACKS_REFERENCES(v)) {
        return;
    }
    
    // Skip native functions (they are static)
    if (is_type(v, CLJ_FUNC)) {
        return;
    }
    
    // Skip empty vector/map singletons
    if (is_type(v, CLJ_VECTOR)) {
        CljPersistentVector *vec = as_vector(v);
        if (vec && vec->base.rc == 0 && vec->data == NULL) return;
    }
    if (is_type(v, CLJ_MAP)) {
        CljMap *map = as_map(v);
        if (map && map->base.rc == 0 && map->data == NULL) return;
    }
    v->rc--;
    
    // Check for double-free after decrement
    if (v->rc < 0) {
        throw_exception_formatted("DoubleFreeError", __FILE__, __LINE__, 0,
                "Double free detected! Object %p (type=%d, rc=%d) was freed twice. "
                "Object type: %s. This indicates a memory management bug.", 
                v, v->type, v->rc, clj_type_name(v->type));
    }
    
    if (v->rc == 0) { 
        DEALLOC(v); // Hook for memory profiling
        release_object_deep(v); 
        free(v); 
    }
}

/** @brief Add object to autorelease pool for deferred cleanup
 * 
 * @param v Pointer to CljObject to autorelease (NULL parameters are safely ignored)
 * @return The same object pointer, or NULL if input was NULL
 * 
 * Adds object to the current autorelease pool for deferred cleanup. Requires
 * an active autorelease pool. The object is not retained when added to the pool.
 */
CljObject *autorelease(CljObject *v) {
    if (!v) return NULL;
    
    // Require active autorelease pool
    if (!g_cv_pool_top) {
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "autorelease() called without active autorelease pool! Object %p (type=%d) will not be automatically freed. "
                "This indicates missing autorelease_pool_push() or premature autorelease_pool_pop().", 
                v, v ? v->type : -1);
    }
    
    // Add to pool (weak reference, no retain)
    vector_push_inplace(g_cv_pool_top->backing, v);
    
    // Track for memory profiling
    MEMORY_PROFILER_TRACK_AUTORELEASE(v);
    
    return v;
}

// ============================================================================
// AUTORELEASE POOL IMPLEMENTATION
// ============================================================================

/** @brief Push a new autorelease pool
 * 
 * @return Pool handle for later use with autorelease_pool_pop_specific()
 * 
 * Creates a new autorelease pool and makes it the current pool. Objects
 * added via autorelease() will be added to this pool and released when
 * the pool is popped.
 */
CljObjectPool *autorelease_pool_push() {
    CljObjectPool *p = ALLOC(CljObjectPool, 1);
    if (!p) return NULL;
    p->backing = make_weak_vector(16);
    p->prev = g_cv_pool_top;
    g_cv_pool_top = p;
    g_pool_push_count++;  // Increment push counter
    return p;
}

// Internal implementation
static void autorelease_pool_pop_internal(CljObjectPool *pool) {
    // Check for push/pop imbalance
    if (g_pool_push_count <= 0) {
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "autorelease_pool_pop() called more times than autorelease_pool_push()! "
                "Push count: %d, attempting to pop pool %p. "
                "This indicates unbalanced pool operations.", 
                g_pool_push_count, pool);
    }
    
    // Use current pool if none specified
    if (!pool) {
        pool = g_cv_pool_top;
    }
    if (!pool || g_cv_pool_top != pool) return;
    
    CljPersistentVector *vec = as_vector(pool->backing);
    if (vec) {
        for (int i = vec->count - 1; i >= 0; --i) {
            CljObject *obj = vec->data[i];
            vec->data[i] = NULL;  // Prevent double-free
            RELEASE(obj);         // Release object
        }
        vec->count = 0;
    }
    g_cv_pool_top = pool->prev;
    g_pool_push_count--;
    
    // Check for negative push count (imbalance)
    if (g_pool_push_count < 0) {
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "Pool push/pop imbalance! Push count: %d. "
                "This indicates more pop() calls than push() calls.", g_pool_push_count);
    }
    
    // Release the weak vector backing
    if (pool->backing) {
        RELEASE(pool->backing);
    }
    
    free(pool);
}

/** @brief Pop and drain current autorelease pool (most common usage)
 * 
 * Pops the current autorelease pool and releases all objects in it.
 * This is the most common way to use autorelease pools.
 */
void autorelease_pool_pop(void) {
    autorelease_pool_pop_internal(NULL);
}

/** @brief Pop and drain specific autorelease pool (advanced usage)
 * 
 * @param pool Specific pool to pop (must be the current top or NULL)
 * 
 * Allows popping a specific pool, useful for advanced memory management
 * scenarios where you need fine-grained control over pool lifetimes.
 */
void autorelease_pool_pop_specific(CljObjectPool *pool) {
    autorelease_pool_pop_internal(pool);
}

/** @brief Legacy API: Pop and drain given autorelease pool (backward compatibility)
 * 
 * @param pool Pool to pop
 * 
 * Kept for backward compatibility with existing code.
 */
void autorelease_pool_pop_legacy(CljObjectPool *pool) {
    autorelease_pool_pop_internal(pool);
}

/** @brief Drain all autorelease pools (global cleanup)
 * 
 * Pops all autorelease pools in the stack. Useful for global cleanup
 * at program termination or when you need to ensure all pools are drained.
 */
void autorelease_pool_cleanup_all() {
    while (g_cv_pool_top) {
        autorelease_pool_pop_internal(g_cv_pool_top);
    }
}

/** @brief Check if autorelease pool is active
 * 
 * @return true if there is an active autorelease pool, false otherwise
 * 
 * Useful for debugging and ensuring proper pool management.
 */
bool is_autorelease_pool_active(void) {
    return g_cv_pool_top != NULL;
}

/** @brief Get reference count of object
 * 
 * @param obj Object to check (can be NULL)
 * @return Reference count (0 for singletons, actual rc for others)
 * 
 * Returns 0 for singleton objects (nil, true, false) since they don't use
 * reference counting. For other objects, returns the actual reference count.
 * Note: AUTORELEASE objects are not counted as they are deferred.
 */
int get_reference_count(CljObject *obj) {
    if (!obj) return 0;
    
    // Singletons don't use reference counting
    if (IS_SINGLETON_TYPE(obj->type)) {
        return 0;
    }
    
    // Return actual reference count for tracked objects
    return obj->rc;
}

// ============================================================================
// DEEP OBJECT RELEASE IMPLEMENTATION
// ============================================================================

/** @brief Central dispatcher for finalizers based on type tag
 * 
 * @param v Object to finalize
 * 
 * Handles deep cleanup of objects based on their type. Called when an object's
 * reference count reaches zero. Performs type-specific cleanup (freeing strings,
 * releasing vector elements, etc.).
 */
static void release_object_deep(CljObject *v) {
    if (!v) return;
    
    // Skip singletons (they don't need cleanup)
    if (!TRACKS_REFERENCES(v)) {
        return;
    }
    
    // Type-specific cleanup based on object type
    switch (v->type) {
        case CLJ_STRING:
            // Free string data
            if (v->as.data) free(v->as.data);
            break;
            
        case CLJ_SYMBOL:
            // Symbols are interned; no cleanup needed
            break;
            
        case CLJ_VECTOR:
        case CLJ_WEAK_VECTOR:
            {
                CljPersistentVector *vec = as_vector(v);
                if (vec && vec->data) {
                    // Release all vector elements
                    for (int i = 0; i < vec->count; i++) {
                        if (vec->data[i]) {
                            RELEASE(vec->data[i]);
                        }
                    }
                    free(vec->data);
                }
            }
            break;
            
        case CLJ_MAP:
            {
                CljMap *map = as_map(v);
                if (map && map->data) {
                    // Release all key-value pairs
                    for (int i = 0; i < map->count * 2; i += 2) {
                        if (map->data[i]) RELEASE(map->data[i]);     // key
                        if (map->data[i+1]) RELEASE(map->data[i+1]); // value
                    }
                    free(map->data);
                }
            }
            break;
            
        case CLJ_LIST:
            {
                CljList *list = as_list(v);
                // Release head and tail elements
                if (list->head) RELEASE(list->head);
                if (list->tail) RELEASE(list->tail);
            }
            break;
            
        case CLJ_FUNC:
            // Native functions are static - no cleanup needed
            break;
            
        default:
            // Unknown type - no specific finalizer needed
            break;
    }
}

// ============================================================================
// STACK DETECTION UTILITIES
// ============================================================================

/** @brief Check if a pointer points to stack memory
 * 
 * @param ptr Pointer to check
 * @return true if pointer is on the stack, false otherwise
 * 
 * This function detects if a pointer points to stack memory by comparing
 * the pointer address with the current stack pointer. This is useful for
 * detecting stack-based objects that should not be freed with free().
 * 
 * Implementation:
 * - Gets current stack pointer using __builtin_frame_address(0)
 * - Compares pointer address with stack bounds
 * - Returns true if pointer is within stack range
 */
bool is_pointer_on_stack(const void *ptr) {
    // TEMPORARILY DISABLED: Function causes hanging in tests
    // TODO: Implement proper stack detection without causing issues
    (void)ptr; // Suppress unused parameter warning
    return false;
}
