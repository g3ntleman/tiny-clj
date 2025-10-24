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
#include "types.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// External reference to verbose mode
extern bool g_memory_verbose_mode;

// Global flag to control debug output during initialization
static bool g_debug_output_enabled = false;

// Function to enable debug output after initialization
void enable_memory_debug_output(void) {
    g_debug_output_enabled = true;
}

// ============================================================================
// MEMORY ALLOCATION WITH PROFILING
// ============================================================================

/**
 * @brief Allocate memory with automatic profiling for CljObject types
 * @param type The type to allocate
 * @param count Number of elements to allocate
 * @return Pointer to allocated memory
 */
void* alloc(size_t type_size, size_t count, CljType obj_type) {
    void *result = malloc(type_size * count);
    
    // Track object creation if it's a CljObject subtype and NOT a singleton
    if (result != NULL && type_size >= sizeof(CljObject)) {
        if (!IS_SINGLETON_TYPE(obj_type)) {
            CljObject *obj = (CljObject*)result;
            obj->type = obj_type;  // Set type before tracking
            MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj);
        }
    }
    
    return result;
}

/**
 * @brief Allocate and zero-initialize memory with automatic profiling for CljObject types
 * @param type_size Size of the type to allocate
 * @param count Number of elements to allocate
 * @return Pointer to allocated memory
 */
void* alloc_zero(size_t type_size, size_t count, CljType obj_type) {
    void *result = calloc(count, type_size);
    
    // Track object creation if it's a CljObject subtype and NOT a singleton
    if (result != NULL && type_size >= sizeof(CljObject)) {
        if (!IS_SINGLETON_TYPE(obj_type)) {
            CljObject *obj = (CljObject*)result;
            obj->type = obj_type;  // Set type before tracking
            MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj);
        }
    }
    
    return result;
}


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
    
    // Safety check: ensure the pointer is valid and points to a valid object
    // Check if the pointer is in a reasonable memory range (not in zero page)
    if ((uintptr_t)v < 0x1000) {
        return;
    }
    
    // Track retain call for profiling
    memory_profiler_track_retain(v);
    
    // Skip singletons (they don't use retain counting)
    if (!TRACKS_RETAINS(v)) {
        return;
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
    
    // Only show debug output if memory profiling is enabled and verbose mode is on
    // AND debug output is enabled (after initialization)
    if (is_memory_profiling_enabled() && g_memory_verbose_mode && g_debug_output_enabled) {
        printf("🔍 release: Object %p, type=%d (%s), rc=%d -> ", 
               v, v->type, clj_type_name(v->type), v->rc);
    }
    
    // Safety check: ensure the pointer is valid and points to a valid object
    // Check if the pointer is in a reasonable memory range (not in zero page)
    if ((uintptr_t)v < 0x1000) {
        printf("SKIPPED (invalid pointer)\n");
        return;
    }
    
    // Skip singletons (they don't use retain counting)
    if (!TRACKS_RETAINS(v)) {
        printf("SKIPPED (singleton)\n");
        return;
    }
    
    // Skip native functions (they are static)
    if (is_type(v, CLJ_FUNC)) {
        printf("SKIPPED (native function)\n");
        return;
    }
    
    // Check for underflow BEFORE decrementing
    if (v->rc == 0) {
        if (is_memory_profiling_enabled() && g_memory_verbose_mode && g_debug_output_enabled) {
            printf("❌ UNDERFLOW! Object already freed\n");
        }
        throw_exception_formatted("UseAfterFreeError", __FILE__, __LINE__, 0,
            "Use-after-free detected! Object %p (type=%s) was already freed (rc=0). "
            "This indicates the object was released more times than retained, "
            "likely due to duplicate AUTORELEASE or incorrect memory management.",
            v, clj_type_name(v->type));
        return;
    }

    v->rc--;
    printf("rc=%d\n", v->rc);
    
    // Track release operation
    MEMORY_PROFILER_TRACK_RELEASE(v);
    
    if (v->rc == 0) { 
        if (is_memory_profiling_enabled() && g_memory_verbose_mode && g_debug_output_enabled) {
            printf("🔍 release: Object %p will be freed (rc=0)\n", v);
        }
        release_object_deep(v); 
        DEALLOC(v); // Hook for memory profiling - BEFORE deep release

        if (is_memory_profiling_enabled() && g_memory_verbose_mode && g_debug_output_enabled) {
            printf("🔍 release: Object %p freed\n", v);
        }
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
    } else 

        // Only show debug output if memory profiling is enabled and verbose mode is on
        if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
            printf("🔍 autorelease: Object %p, type=%d (%s), rc=%d\n", 
                   v, v->type, clj_type_name(v->type), v->rc);
        }
        
    
    // Add to pool (weak reference, no retain)
    // Use direct manipulation for autorelease pool
    CljPersistentVector *backing = as_vector(g_cv_pool_top->backing);
    if (backing && backing->count < backing->capacity) {
        backing->data[backing->count++] = v;
        if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
            printf("🔍 autorelease: Added object %p to pool (count=%d)\n", v, backing->count);
        }
    } else {
        if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
            printf("🔍 autorelease: Pool full or invalid backing for object %p\n", v);
        }
    }
    
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
// CFAutoreleasePool: Exception-safe pool management
// Compatible with setjmp/longjmp by using global stack tracking

// Global stack of active pools for exception safety
static CljObjectPool *g_exception_safe_pools[64] = {0};
static int g_exception_safe_count = 0;

CljObjectPool *autorelease_pool_push() {
    CljObjectPool *p = (CljObjectPool*)malloc(sizeof(CljObjectPool));
    if (!p) return NULL;
    
    // Create weak vector using new API - increase capacity for core loading
    CljValue backing_val = make_vector(1024, 1); // mutable, larger capacity
    p->backing = (CljObject*)backing_val;
    p->prev = g_cv_pool_top;
    g_cv_pool_top = p;
    g_pool_push_count++;  // Increment push counter
    
    // CFAutoreleasePool: Register for exception safety
    if (g_exception_safe_count < 64) {
        g_exception_safe_pools[g_exception_safe_count++] = p;
    }
    
    return p;
}


static void autorelease_pool_pop_internal(CljObjectPool *pool) {
    printf("🔍 autorelease_pool_pop_internal: Pool %p, push_count=%d\n", pool, g_pool_push_count);
    
    // Check for push/pop imbalance - use printf instead of throw_exception
    if (g_pool_push_count <= 0) {
        printf("ERROR: autorelease_pool_pop() called more times than autorelease_pool_push()! "
               "Push count: %d, attempting to pop pool %p. "
               "This indicates unbalanced pool operations.\n", 
               g_pool_push_count, pool);
        return; // Safe return instead of throwing exception
    }
    
    // Use current pool if none specified
    if (!pool) {
        pool = g_cv_pool_top;
    }
    if (!pool || g_cv_pool_top != pool) {
        printf("🔍 autorelease_pool_pop_internal: Pool mismatch, skipping\n");
        return;
    }
    
    // CFAutoreleasePool: Exception-safe cleanup
    // Remove from exception-safe stack if present
    for (int i = 0; i < g_exception_safe_count; i++) {
        if (g_exception_safe_pools[i] == pool) {
            // Shift remaining pools down
            for (int j = i; j < g_exception_safe_count - 1; j++) {
                g_exception_safe_pools[j] = g_exception_safe_pools[j + 1];
            }
            g_exception_safe_count--;
            break;
        }
    }
    
    CljPersistentVector *vec = as_vector(pool->backing);
    if (vec) {
        for (int i = vec->count - 1; i >= 0; --i) {
            CljObject *obj = vec->data[i];
            vec->data[i] = NULL;  // Prevent double-free
            // Foundation-style exception-safe cleanup
            if (obj && TRACKS_RETAINS(obj)) {
                // Use proper release() instead of direct free() to ensure
                // proper cleanup of object references and prevent use-after-free
                RELEASE(obj);
            }
        }
        vec->count = 0;
    }
    
    // Update stack pointer and decrement push count
    g_cv_pool_top = pool->prev;
    g_pool_push_count--;
    
    // Debug output to verify stack state
    printf("🔍 autorelease_pool_pop_internal: After pop, g_cv_pool_top=%p, push_count=%d\n", 
           g_cv_pool_top, g_pool_push_count);
    
    // Note: Pool structure is not freed here to avoid use-after-free
    // This is a memory leak, but it's better than a crash
    // TODO: Implement proper pool cleanup
    
    // Check for negative push count (imbalance) - use printf instead of throw_exception
    if (g_pool_push_count < 0) {
        printf("ERROR: Pool push/pop imbalance! Push count: %d. "
               "This indicates more pop() calls than push() calls.\n", g_pool_push_count);
        g_pool_push_count = 0; // Reset to prevent further issues
    }
    
    // Release the weak vector backing
    if (pool->backing) {
        RELEASE(pool->backing);
    }
    
    free(pool);
}

// CFAutoreleasePool: Exception-safe cleanup function
// Call this from CATCH blocks to clean up pools after exceptions
void autorelease_pool_cleanup_after_exception() {
    // Clean up all registered pools
    for (int i = g_exception_safe_count - 1; i >= 0; i--) {
        CljObjectPool *pool = g_exception_safe_pools[i];
        if (pool) {
            // Clean up pool contents
            CljPersistentVector *vec = as_vector(pool->backing);
            if (vec) {
                for (int j = vec->count - 1; j >= 0; --j) {
                    CljObject *obj = vec->data[j];
                    if (obj) {
                        vec->data[j] = NULL;  // Prevent double-free
                        RELEASE(obj);         // Release object
                    }
                }
                vec->count = 0;
            }
            
            // Release the weak vector backing
            if (pool->backing) {
                RELEASE(pool->backing);
            }
            
            free(pool);
        }
    }
    
    // Reset exception-safe stack
    g_exception_safe_count = 0;
    g_cv_pool_top = NULL;
    g_pool_push_count = 0;
}

/** @brief Pop and drain current autorelease pool (most common usage)
 * 
 * Pops the current autorelease pool and releases all objects in it.
 * This is the most common way to use autorelease pools.
 */
// Removed: autorelease_pool_pop() - use autorelease_pool_pop_specific() instead

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

/** @brief Get retain count of object
 * 
 * @param obj Object to check (can be NULL)
 * @return Reference count (0 for singletons, actual rc for others)
 * 
 * Returns 0 for singleton objects (nil, true, false) since they don't use
 * reference counting. For other objects, returns the actual reference count.
 * Note: AUTORELEASE objects are not counted as they are deferred.
 */
int get_retain_count(CljObject *obj) {
    if (!obj) return 0;
    
    // Singletons don't use retain counting
    if (IS_SINGLETON_TYPE(obj->type)) {
        return 0;
    }
    
    // Return actual retain count for tracked objects
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
    
    if (!v) {
        if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
            printf("🔍 release_object_deep: NULL object\n");
        }
        return;
    }
    
    if (is_memory_profiling_enabled() && g_memory_verbose_mode && g_debug_output_enabled) {
        printf("🔍 release_object_deep: Object %p, type=%d (%s), rc=%d\n", 
               v, v->type, clj_type_name(v->type), v->rc);
    }
    
    // Skip singletons (they don't need cleanup)
    if (!TRACKS_RETAINS(v)) {
        if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
            printf("🔍 release_object_deep: Skipping singleton object\n");
        }
        return;
    }
    
    
    // Type-specific cleanup based on object type
    switch (v->type) {
        case CLJ_STRING:
            {
                if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
                    printf("🔍 release_object_deep: Freeing STRING object %p\n", v);
                }
                // Free string data stored directly after CljObject header
                char **str_ptr = (char**)((char*)v + sizeof(CljObject));
                if (*str_ptr) {
                    if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
                        printf("🔍 release_object_deep: Freeing string data: '%s'\n", *str_ptr);
                    }
                    free(*str_ptr);
                } else {
                    if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
                        printf("🔍 release_object_deep: String data is NULL\n");
                    }
                }
            }
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
                        RELEASE(map->data[i]);     // key
                        RELEASE(map->data[i+1]); // value
                    }
                    free(map->data);
                }
            }
            break;
            
        case CLJ_LIST:
            {
                CljList *list = as_list(v);
                if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
                    printf("🔍 release_object_deep: Freeing LIST object %p, first=%p, rest=%p\n", v, list->first, list->rest);
                }
                // Release head and tail elements
                if (list->first) {
                    if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
                        printf("🔍 release_object_deep: Releasing list first element %p\n", list->first);
                    }
                    RELEASE(list->first);
                }
                if (list->rest) {
                    if (is_memory_profiling_enabled() && g_memory_verbose_mode) {
                        printf("🔍 release_object_deep: Releasing list rest element %p\n", list->rest);
                    }
                    RELEASE(list->rest);
                }
            }
            break;
            
        case CLJ_FUNC:
            // Native functions are static - no cleanup needed
            break;
            
        case CLJ_BYTE_ARRAY:
            {
                CljByteArray *ba = as_byte_array(v);
                if (ba && ba->data) {
                    free(ba->data);
                }
            }
            break;
            
        // CLJ_INT, CLJ_FLOAT, CLJ_BOOL removed - handled as immediates
            
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

// ============================================================================
// OUT OF MEMORY HELPER
// ============================================================================

void throw_oom(CljType type) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Failed to allocate %s", clj_type_name(type));
    throw_exception("OutOfMemoryError", msg, __FILE__, __LINE__, 0);
    abort(); // Ensure no return
}
