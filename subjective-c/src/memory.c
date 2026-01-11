/*
 * Memory Management Implementation for Tiny-CLJ
 * 
 * Centralized memory management with reference counting and autorelease pools.
 * Provides retain/release semantics similar to Objective-C ARC.
 */

#include "memory.h"
#include "runtime.h"
#include "object.h"
#include "vector.h"
#include "value.h"  // For IS_IMMEDIATE macro used in memory.h
#include "memory_profiler.h"
#include "types.h"
#include "exception.h"
#include "map.h"
#include "list.h"
#include "ast.h"
#include "byte_array.h"
#include "atom.h"
#include "function.h"  // For CljFunction
#include "namespace.h"  // For CljNamespace
#include "hashmap.h"  // For CljHashMap
#include "seq.h"  // For CljLazySeq
#include "thread_local.h"
#include <string.h>
#include <execinfo.h>
#include <stdlib.h>

// ============================================================================
// Closure environment promotion (Stack → Heap)
// ============================================================================
//
// NOTE: tiny-clj closure environments (`CljFunction.env_stack`) are now backed by
// persistent COW vectors (heap-managed). The previous "stack-backed list prefix"
// promotion mechanism is no longer used.

// ============================================================================
// CHECKPOINT-BASED AUTORELEASE POOL
// ============================================================================
//
// Design: Single array for all autoreleased objects + checkpoint stack
// - items[]: Array of autoreleased object pointers
// - checkpoints[]: Stack of indices marking pool boundaries
// - push() adds a checkpoint (current count)
// - pop() releases all items from last checkpoint, removes checkpoint
// - autorelease() appends object to items[]
//
// Advantages over vector-based approach:
// - Zero allocations during push/pop/autorelease (after initial setup)
// - Better cache locality
// - COW-friendly: AUTORELEASE never increases reference count

#define POOL_INITIAL_CAPACITY 1024
#define POOL_CHECKPOINT_CAPACITY 32

typedef struct {
    CljObject **items;       // Array for autoreleased objects
    uint32_t count;          // Current number of items
    uint32_t capacity;       // Capacity of items array
    
    uint32_t *checkpoints;   // Stack of checkpoint indices
    uint32_t cp_count;       // Number of active pools (checkpoint stack depth)
    uint32_t cp_capacity;    // Capacity of checkpoints array
} AutoreleasePoolState;

// Thread-local pool state
static THREAD_LOCAL AutoreleasePoolState g_pool = {0};

#ifdef DEBUG
static THREAD_LOCAL uint32_t g_pool_peak_count = 0;
#endif

// External reference to verbose mode
extern bool g_memory_verbose_mode;

// Global flag to control debug output during initialization
static bool g_debug_output_enabled = false;

// Cached flag to avoid repeated checks - updated when any of the flags change
static bool g_debug_output_active = false;

// Forward declaration for update function
static inline void update_debug_output_active(void);

// Update cached debug output flag (call when flags change)
static inline void update_debug_output_active(void) {
    g_debug_output_active = g_memory_profiling_enabled && g_memory_verbose_mode && g_debug_output_enabled;
}

#ifdef DEBUG
// Zombie mode is controlled by ZOMBIE_ENABLED macro at compile time
// No runtime variable needed - if ZOMBIE_ENABLED is defined, zombie mode is active
#ifdef ZOMBIE_ENABLED
// Intentionally no compile-time warning: this project reports zombie mode via runtime build info.
#endif
#else
// Release builds: zombie mode not available
#endif

// Function to enable debug output after initialization
void enable_memory_debug_output(void) {
    g_debug_output_enabled = true;
    update_debug_output_active();
}

// Function to disable debug output
void disable_memory_debug_output(void) {
    g_debug_output_enabled = false;
    update_debug_output_active();
}

// Public function to update cached debug output flag (called from memory_profiler.c)
void memory_update_debug_output_active(void) {
    update_debug_output_active();
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
    if (!result) {
        throw_oom();  // Never returns
    }
    
    // Track object creation if it's a CljObject subtype
    // Note: Singletons (rc == SINGLETON_RC) are set up later and never released
    if (type_size >= sizeof(CljObject)) {
        CljObject *obj = (CljObject*)result;
        obj->type = obj_type;  // Set type before tracking
        obj->flags = 0;  // Initialize flags
        MEMORY_PROFILER_TRACK_OBJECT_CREATION(obj);
    }
    
    return result;
}

// ============================================================================
// AUTORELEASE POOL IMPLEMENTATION
// ============================================================================

// MAX_POOL_DEPTH is defined in runtime.h

// Forward declarations
static void release_object_deep(CljObject *v);
static void release_object_default(CljObject *v);
static void init_release_dispatch(void);
static SubjectiveCReleaseFn g_release_dispatch[CLJ_TYPE_COUNT];
static bool g_release_dispatch_initialized = false;

/** @brief Initialize the autorelease pool (call once at startup)
 * 
 * Must be called before any autorelease operations. Typically called
 * from runtime_init().
 */
void autorelease_pool_init(void) {
    if (g_pool.items) return;  // Already initialized
    
    g_pool.capacity = POOL_INITIAL_CAPACITY;
    g_pool.items = (CljObject**)malloc(sizeof(CljObject*) * g_pool.capacity);
    g_pool.count = 0;
    
    g_pool.cp_capacity = POOL_CHECKPOINT_CAPACITY;
    g_pool.checkpoints = (uint32_t*)malloc(sizeof(uint32_t) * g_pool.cp_capacity);
    g_pool.cp_count = 0;

#ifdef DEBUG
    g_pool_peak_count = 0;
#endif
}

static void init_release_dispatch(void) {
    if (g_release_dispatch_initialized) return;
    for (int i = 0; i < CLJ_TYPE_COUNT; i++) {
        g_release_dispatch[i] = release_object_default;
    }
    g_release_dispatch_initialized = true;
}

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

#ifdef DEBUG
    // Safety check: ensure the pointer is valid
    if ((uintptr_t)v < 0x1000) {
        return;
    }
#endif

#ifdef DEBUG
    // Check for zombie object
    if (v->rc == 0) {
        // Zombie detected: throw exception with stacktrace and zombie object
        // Don't try to print object representation (may fail if object is corrupted)
        char message[512];
        snprintf(message, sizeof(message),
            "Attempted to retain zombie object %p (type=%s). "
            "This object was already freed but marked as zombie for debugging.",
            v, clj_type_name(v->type));
        CLJException *ex = make_exception(EXCEPTION_ZOMBIE_ACCESS, message, __FILE__, __LINE__, 0);
        if (ex) {
            ex->object = (uintptr_t)v;  // Address-only: store without retaining
            throw_exception_object(AUTORELEASE(ex));
        }
        return;
    }
#endif
    
    // Note: closure environments are heap-managed (vector). No stack promotion needed.

    // Happy path: object that tracks retains
    if (TRACKS_RETAINS(v)) {
        // Track retain call for profiling (compile-time no-op in release builds)
        MEMORY_PROFILER_TRACK_RETAIN(v);
        v->rc++;
    }
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

#ifndef DEBUG
    // Max-performance release build: no pointer-safety checks.
    if (v->rc == SINGLETON_RC) return;
    v->rc--;
    MEMORY_PROFILER_TRACK_RELEASE(v);
    if (v->rc == 0) {
        release_object_deep(v);
#ifdef ZOMBIE_ENABLED
        // Zombie mode not supported in release builds, but keep compile-time guard.
#else
        DEALLOC(v);
#endif
    }
    return;
#else
    // Safety check: ensure the pointer is valid and points to a valid object
    // Check if the pointer is in a reasonable memory range (not in zero page)
    if ((uintptr_t)v < 0x1000) {
        return;
    }
    
    // Additional safety check: use is_singleton which has better pointer validation
    // This avoids accessing v->type if v is an invalid pointer
    if (is_singleton(v)) {
        return;
    }
#endif
    
    // Only show debug output if memory profiling is enabled and verbose mode is on
    // AND debug output is enabled (after initialization)
    // Move this after the safety checks to avoid accessing v->type on invalid pointers
    // Use cached flag to avoid repeated function calls
    if (g_debug_output_active) {
        printf("🔍 release: Object %p, type=%d (%s), rc=%d -> ", 
               v, v->type, clj_type_name(v->type), v->rc);
    }
    
    // Note: CLJ_FUNC (native functions) are static and don't need release
    // CLJ_CLOSURE (interpreted functions) need to be released and will be handled by release_object_deep
    
#ifdef DEBUG
    // Check for double-free BEFORE decrementing (DEBUG only)
    if (v->rc == 0) {
        fprintf(stderr, "❌ DOUBLE-FREE! Object %p (type=%s) already freed\n", v, clj_type_name(v->type));
        fprintf(stderr, "🔍 Backtrace (most recent call first):\n");
        void *trace[64];
        int trace_count = backtrace(trace, (int)(sizeof(trace) / sizeof(trace[0])));
        char **symbols = backtrace_symbols(trace, trace_count);
        if (symbols) {
            for (int i = 0; i < trace_count; i++) {
                fprintf(stderr, "  %s\n", symbols[i]);
            }
            free(symbols);
        } else {
            fprintf(stderr, "  <backtrace_symbols failed>\n");
        }
        fflush(stderr);
        throw_exception_formatted("UseAfterFreeError", __FILE__, __LINE__, 0,
            "Double-free detected! Object %p (type=%s) was already freed (rc=0). "
            "This indicates the object was released more times than retained, "
            "likely due to duplicate AUTORELEASE or incorrect memory management.",
            v, clj_type_name(v->type));
        return;
    }
#endif

    v->rc--;
    
    // Track release operation
    MEMORY_PROFILER_TRACK_RELEASE(v);
    
    if (v->rc == 0) { 
        if (g_debug_output_active) {
            printf("🔍 release: Object %p will be freed (rc=0)\n", v);
        }

        // Release contained values (for containers)
        // Note: rc is already 0 at this point (after decrement).
        // In zombie mode, rc=0 means the object is a zombie (freed but not DEALLOCed).
        // release_object_deep() uses direct casts in release_object_default() (not as_*() functions),
        // so it's safe to call even when rc=0 (zombie mode).
        release_object_deep(v);
        
#ifdef ZOMBIE_ENABLED
        // In zombie mode: DON'T free the object, keep it at rc=0 for inspection
        // The object remains in memory so we can examine it later
        // rc is already 0, so no need to set it again
        if (g_debug_output_active) {
            printf("🔍 release: Object %p marked as zombie (rc=0, not DEALLOCed)\n", v);
        }
#else
        // Normal mode: free the object
        DEALLOC(v);
        if (g_debug_output_active) {
            printf("🔍 release: Object %p freed\n", v);
        }
#endif
    }
}

/** @brief Add object to autorelease pool for deferred cleanup
 * 
 * @param v Pointer to CljObject to autorelease (NULL parameters are safely ignored)
 * @return The same object pointer, or NULL if input was NULL
 * 
 * Adds object to the current autorelease pool for deferred cleanup. Requires
 * an active autorelease pool. The object is not retained when added to the pool.
 * COW-friendly: Does NOT increase reference count.
 */
CljObject *autorelease(CljObject *v) {
    if (!v) return NULL;
    
    CLJ_ASSERT(g_pool.items && "autorelease_pool_init() not called");

    // Require active autorelease pool
    if (g_pool.cp_count == 0) {
        // In DEBUG builds, throw exception to catch programming errors
        // In release builds, silently ignore (object will be leaked, but program continues)
    #ifdef DEBUG
        // Safety: check if v is valid before accessing v->type
        CljType type_val = CLJ_NIL;
        if (v && (uintptr_t)v >= 0x1000) {
            type_val = v->type;
        }
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "autorelease() called without active autorelease pool! Object %p (type=%s) will not be automatically freed. "
                "This indicates missing autorelease_pool_push() or premature autorelease_pool_pop().", 
                v, clj_type_name(type_val));
#else
        // In release builds, just return the object (it will be leaked)
        // This prevents crashes in production code
        return v;
#endif
        return v;
    }
    
    // Grow items array if needed
    if (g_pool.count >= g_pool.capacity) {
        uint32_t new_capacity = g_pool.capacity * 2;
        CljObject **new_items = (CljObject**)realloc(g_pool.items, sizeof(CljObject*) * new_capacity);
        if (!new_items) {
            // Out of memory: stop tracking rather than crashing.
            // NOTE: pool has weak semantics (debug/profiling only), so leaking tracking is acceptable.
            return v;
        }
        g_pool.items = new_items;
        g_pool.capacity = new_capacity;
#ifdef DEBUG
        fprintf(stderr, "⚠️  AutoreleasePool: items grew %u -> %u\n", new_capacity / 2, new_capacity);
#endif
    }
    
    // Append object (no RETAIN - COW friendly!)
    g_pool.items[g_pool.count++] = v;

#ifdef DEBUG
    if (g_pool.count > g_pool_peak_count) {
        g_pool_peak_count = g_pool.count;
    }
#endif
    
    // Track for memory profiling
    MEMORY_PROFILER_TRACK_AUTORELEASE(v);
    
    return v;
}

uint32_t autorelease_pool_peak_count(void) {
#ifdef DEBUG
    return g_pool_peak_count;
#else
    return 0;
#endif
}

void autorelease_pool_peak_reset(void) {
#ifdef DEBUG
    if (!g_pool.items) {
        g_pool_peak_count = 0;
        return;
    }
    g_pool_peak_count = g_pool.count;
#else
    // no-op in non-debug builds
#endif
}

#ifdef DEBUG
/** @brief Check if an object is in the autorelease pool (O(n) search)
 * 
 * @param obj Object to check
 * @return true if object is in the current autorelease pool, false otherwise
 * 
 * Debug-only function that searches through the autorelease pool items array
 * to determine if the given object is currently autoreleased.
 * This is O(n) where n is the number of objects in the pool.
 */
bool is_autoreleased(CljObject *obj) {
    if (!obj || !g_pool.items) {
        return false;
    }
    
    // Search through all items in the pool
    for (uint32_t i = 0; i < g_pool.count; i++) {
        if (g_pool.items[i] == obj) {
            return true;
        }
    }
    
    return false;
}
#endif // DEBUG

// ============================================================================
// CHECKPOINT-BASED AUTORELEASE POOL IMPLEMENTATION
// ============================================================================


/** @brief Grow the checkpoints array if needed
 * 
 * Doubles the capacity of the checkpoints array when it's full.
 * Inline for performance (growth is rare but in hot path).
 */
static inline void autorelease_pool_grow(void) {
#ifdef DEBUG
    uint32_t old_capacity = g_pool.cp_capacity;
#endif
    uint32_t new_capacity = g_pool.cp_capacity * 2;
    uint32_t *new_cps = (uint32_t*)realloc(g_pool.checkpoints, sizeof(uint32_t) * new_capacity);
    if (!new_cps) {
        // Out of memory: keep existing checkpoints (best-effort).
        return;
    }
    g_pool.checkpoints = new_cps;
    g_pool.cp_capacity = new_capacity;
#ifdef DEBUG
    fprintf(stderr, "⚠️  AutoreleasePool: checkpoints grew %u -> %u\n", old_capacity, new_capacity);
#endif
}

/** @brief Push a new autorelease pool (checkpoint)
 * 
 * @return void (no return value)
 * 
 * Creates a new checkpoint at the current item count. Objects added via 
 * autorelease() will be tracked until pop() clears them.
 */
void autorelease_pool_push() {
    // Safety: initialize pool if not already initialized
    if (!g_pool.items) {
        autorelease_pool_init();
    }
    CLJ_ASSERT(g_pool.items && "autorelease_pool_init() failed");
    
    // Grow checkpoints array if needed
    if (g_pool.cp_count >= g_pool.cp_capacity) {
        autorelease_pool_grow();
    }
    
    // Push checkpoint (current item count)
    g_pool.checkpoints[g_pool.cp_count++] = g_pool.count;
    
    if (g_debug_output_active) {
        printf("🔍 autorelease_pool_push: checkpoint at %u (depth=%u)\n", 
               g_pool.count, g_pool.cp_count);
    }
}


/** @brief Pop and drain the current autorelease pool
 * 
 * @return void (no parameters)
 * 
 * Removes the checkpoint and releases all objects added since that checkpoint.
 *
 * Classic autorelease semantics:
 * - AUTORELEASE() adds to the pool without retaining
 * - pop() performs the matching release() calls
 */
void autorelease_pool_pop(void) {
    
    // Check for stack underflow
    if (g_pool.cp_count == 0) {
        printf("WARNING: autorelease_pool_pop() called on empty stack! "
               "This indicates more pop() calls than push() calls.\n");
#ifdef DEBUG
        // Print stack trace for debugging
        void *trace[16];
        int trace_count = backtrace(trace, 16);
        char **symbols = backtrace_symbols(trace, trace_count);
        if (symbols) {
            fprintf(stderr, "Stack trace:\n");
            for (int i = 0; i < trace_count; i++) {
                fprintf(stderr, "  %s\n", symbols[i]);
            }
            free(symbols);
        }
#endif
        return;
    }
    
    // Get checkpoint (start index for this pool)
    uint32_t checkpoint = g_pool.checkpoints[--g_pool.cp_count];
    
    if (g_debug_output_active) {
        printf("🔍 autorelease_pool_pop: draining %u objects (checkpoint=%u, count=%u)\n",
               g_pool.count - checkpoint, checkpoint, g_pool.count);
    }
    
    // Drain: release everything added since checkpoint.
    // Reverse order better matches nested container lifetimes.
    for (uint32_t i = g_pool.count; i > checkpoint; --i) {
        CljObject *obj = g_pool.items[i - 1];
        release(obj);
    }

    // Forget drained items.
    g_pool.count = checkpoint;
}

// Exception-safe cleanup function (called from CATCH blocks)
void autorelease_pool_cleanup_after_exception(void) {
    // Drain all pools
    while (g_pool.cp_count > 0) {
        autorelease_pool_pop();
    }
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
void autorelease_pool_cleanup_all(void) {
    while (g_pool.cp_count > 0) {
        autorelease_pool_pop();
    }
}

/** @brief Check if autorelease pool is active
 * 
 * @return true if there is an active autorelease pool, false otherwise
 * 
 * Useful for debugging and ensuring proper pool management.
 */
bool is_autorelease_pool_active(void) {
    return g_pool.cp_count > 0;
}

/** @brief Cleanup pool state (call at program exit or runtime reset)
 */
void autorelease_pool_destroy(void) {
    // Drain all remaining pools
    autorelease_pool_cleanup_all();
    
    // Free backing arrays (always free pool structures, even in zombie mode)
    // Pool structures are not objects, so they should be freed normally
    if (g_pool.items) {
        free(g_pool.items);
        g_pool.items = NULL;
    }
    if (g_pool.checkpoints) {
        free(g_pool.checkpoints);
        g_pool.checkpoints = NULL;
    }
    g_pool.count = 0;
    g_pool.capacity = 0;
    g_pool.cp_count = 0;
    g_pool.cp_capacity = 0;
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
int retain_count(ID obj) {
    if (!obj || IS_IMMEDIATE(obj)) return 0;
    
    CljObject *obj_ptr = (CljObject*)obj;
    
    // Singletons don't use retain counting
    if (obj_ptr->rc == SINGLETON_RC) {
        return 0;
    }
    
    // Return actual retain count for tracked objects
    return obj_ptr->rc;
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
        if (g_debug_output_active) {
            printf("🔍 release_object_deep: NULL object\n");
        }
        return;
    }
    
#ifdef ZOMBIE_ENABLED
    // In zombie mode: rc=0 is the zombie marker (object freed but not DEALLOCed)
    // We can safely access the object structure even if it's a zombie
    // (object remains in memory for inspection)
#endif
    
    if (g_debug_output_active) {
        printf("🔍 release_object_deep: Object %p, type=%d (%s), rc=%d\n", 
               v, v->type, clj_type_name(v->type), v->rc);
    }
    
    // Skip singletons (they don't need cleanup)
    if (!TRACKS_RETAINS(v)) {
        return;
    }
    
    
    init_release_dispatch();
    SubjectiveCReleaseFn fn = (v->type >= 0 && v->type < CLJ_TYPE_COUNT)
        ? g_release_dispatch[v->type]
        : NULL;
    if (fn) {
        fn(v);
    }
}

static void release_object_default(CljObject *v) {
    switch (v->type) {
        case CLJ_STRING:
            /* Strings store their data inline (flexible array member).
             * DEALLOC(v) frees both header and characters, so nothing
             * extra to do here. */
            break;
            
        // CLJ_SYMBOL: Release handler registered by tiny-clj via subjective_c_register_release_fn()
            
        case CLJ_VECTOR:
            {
                // Direct cast - we already know it's a Vector from the switch case
                // Using as_vector() would call TAG() which fails when rc=0 (zombie mode)
                CljVector *vec = (CljVector*)v;
                if (vec) {
                    // Release all vector elements
                    VECTOR_FOR_EACH(vec, elem) {
                        RELEASE(elem);
                    }
                    // Note: data array is automatically freed
                }
            }
            break;
            
        case CLJ_VECTOR_TRANSIENT_WEAK:
            {
                // For CLJ_VECTOR_TRANSIENT_WEAK, elements are not retained or released (weak references)
                // Note: data array is automatically freed
            }
            break;
            
        case CLJ_MAP:
            {
                // Direct cast - we already know it's a Map from the switch case
                // Using as_map() would call TAG() which fails when rc=0 (zombie mode)
                CljMap *map = (CljMap*)v;
                if (map) {
                    // Release all key-value pairs
                    MAP_FOR_EACH(map, key, value) {
                        RELEASE(key);
                        RELEASE(value);
                    }
                    // Note: map->data is a flexible array member, part of the struct
                    // It will be freed automatically when DEALLOC frees the struct
                }
            }
            break;
            
        case CLJ_HASHMAP:
            {
                CljHashMap *map = (CljHashMap*)v;
                ID hm_key;
                ID hm_val;
                HASHMAP_FOR_EACH(map, hm_key, hm_val) {
                    RELEASE(hm_key);
                    RELEASE(hm_val);
                }
            }
            break;
            
        case CLJ_LIST:
            {
                // Direct cast - we already know it's a List from the switch case
                // Using as_list() would call TAG() which fails when rc=0 (zombie mode)
                CljList *list = (CljList*)v;
                if (g_debug_output_active) {
                    printf("🔍 release_object_deep: Freeing LIST object %p, first=%p, rest=%p\n", v, list ? list->first : NULL, list ? list->rest : NULL);
                }
                // Release head and tail elements - RELEASE handles NULL
                if (list) {
                    if (g_debug_output_active) {
                        if (list->first) {
                            printf("🔍 release_object_deep: Releasing list first element %p\n", list->first);
                        }
                    }
                    RELEASE(list->first);
                    if (g_debug_output_active) {
                        if (list->rest) {
                            printf("🔍 release_object_deep: Releasing list rest element %p\n", list->rest);
                        }
                    }
                    RELEASE(list->rest);
                }
            }
            break;

        case CLJ_AST_NODE:
            {
                // Direct cast - we already know it's an AST node from the switch case
                // Using as_ast_node() would call TAG() which fails when rc=0 (zombie mode)
                CljASTNode *node = (CljASTNode*)v;
                if (!node) {
                    break;
                }
                if (g_debug_output_active) {
                    printf("🔍 release_object_deep: Freeing AST node %p, first=%p, rest=%p, cache=%p\n",
                           v, node->first, node->rest, node->callsite_cache);
                }
                RELEASE(node->first);
                RELEASE(node->rest);
                RELEASE(node->callsite_cache);
            }
            break;

            
        case CLJ_CALLSITE_CACHE:
            {
                CljCallsiteCache *cache = as_callsite_cache(v);
                if (!cache) {
                    break;
                }
                ASSIGN(cache->resolved, NULL);
            }
            break;

        case CLJ_FUNC:
            // Native functions are static - no cleanup needed
            break;
            
        case CLJ_CLOSURE:
            {
                CljFunction *func = (CljFunction*)v;
                if (func) {
                    // Release parameter vector (vector will release all elements) - RELEASE handles NULL
                    RELEASE(func->params);
                    // Release body - RELEASE handles NULL
                    RELEASE(func->body);
                    // Release closure environment.
                    // NOTE: env_stack can be stack-backed for lazy capture. Never RELEASE stack pointers.
                    if (func->env_stack && !is_pointer_on_stack(func->env_stack)) {
                        RELEASE(func->env_stack);
                    }
                    // Release captured CallFrame chain (vector-of-vectors, heap-managed).
                    RELEASE(func->captured_frames);
                    // Release captured namespace reference
                    RELEASE(func->ns);
                    // Free function name (strdup'd in make_function)
                    // Don't free in zombie mode - object must remain intact
#ifndef ZOMBIE_ENABLED
                    if (func->name) {
                        free((void*)func->name);
                    }
#endif
                }
            }
            break;
            
        case CLJ_BYTE_ARRAY:
            {
                // Don't free in zombie mode - object must remain intact
#ifndef ZOMBIE_ENABLED
                CljByteArray *ba = as_byte_array(v);
                if (ba && ba->data) {
                    free(ba->data);
                }
#else
                (void)v; // Suppress unused variable warning in zombie mode
#endif
            }
            break;
            
        case CLJ_ATOM:
            {
                // Direct cast - we already know it's an Atom from the switch case
                // Using as_atom() would call TAG() which fails on zombie objects
                CljAtom *atom = (CljAtom*)v;
                if (atom) {
                    // Release the atom's value - RELEASE handles NULL, nil, and immediates safely
                    RELEASE(atom->value);
                }
            }
            break;
            
        case CLJ_SEQ:
            // CljSeqIterator contains only stack-allocated iterator state
            // No heap-allocated data to release (container is a borrowed reference)
            break;

        case CLJ_LAZY_SEQ:
            {
                // Direct cast - avoid TAG() which may throw in zombie mode.
                CljLazySeq *lazy_seq = (CljLazySeq*)v;
                if (lazy_seq) {
                    RELEASE(lazy_seq->thunk);
                    RELEASE(lazy_seq->first);
                    RELEASE(lazy_seq->cached_rest);
                }
            }
            break;
            
        case CLJ_NAMESPACE:
            {
                // Note: namespace.h is included at the top of memory.c
                CljNamespace *ns = (CljNamespace*)v;
                if (ns) {
                    // Release mappings map (CljMap*) - RELEASE handles NULL
                    RELEASE(ns->mappings);
                    // Release aliases map (CljMap*) - RELEASE handles NULL
                    RELEASE(ns->aliases);
                    // Free filename (strdup'd in make_namespace/ns_get_or_create)
                    // Don't free in zombie mode - object must remain intact
#ifndef ZOMBIE_ENABLED
                    if (ns->filename) {
                        free((void*)ns->filename);
                    }
#endif
                    // Note: name (CljSymbol*) is an interned symbol managed by the symbol table,
                    // so it should NOT be released here. The symbol table owns the symbol's lifetime.
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
// MEMORY SEGMENT DETECTION UTILITIES
// ============================================================================

/** @brief Check if a pointer points to data segment (static/read-only memory)
 * 
 * @param ptr Pointer to check
 * @return true if pointer is in data segment, false otherwise
 * 
 * This function detects if a pointer points to data segment memory (string literals,
 * static variables) by checking if the address is in a typical data segment range.
 * This is useful for detecting static strings that should not be freed with free().
 * 
 * Implementation:
 * - On 64-bit systems, data segment is typically at low addresses (< 0x100000000)
 * - On 32-bit systems, data segment is typically at low addresses (< 0x08000000)
 * - Heap-allocated memory (malloc) is typically at higher addresses
 * 
 * Note: This is a heuristic and may not be 100% accurate, but works for
 * typical cases where string literals are in data segment and malloc'd
 * strings are on the heap.
 */
bool is_pointer_in_data_segment(const void *ptr) {
    if (!ptr) return false;
    
    uintptr_t addr = (uintptr_t)ptr;
    
    // On 64-bit systems, data segment is typically below 0x100000000 (4GB)
    // On 32-bit systems, data segment is typically below 0x08000000 (128MB)
    // Heap-allocated memory (malloc) is typically at higher addresses
    
    // Check if address is in typical data segment range
    // This is a heuristic - actual ranges may vary by platform
#if UINTPTR_MAX == UINT64_MAX
    // 64-bit system
    // Data segment: typically < 0x100000000 (4GB)
    // Heap: typically > 0x100000000
    return addr < 0x100000000ULL;
#else
    // 32-bit system
    // Data segment: typically < 0x08000000 (128MB)
    // Heap: typically > 0x08000000
    return addr < 0x08000000UL;
#endif
}

/** @brief Check if a pointer points to stack memory
 * 
 * @param ptr Pointer to check
 * @return true if pointer is on the stack, false otherwise
 * 
 * This function detects if a pointer points to stack memory by comparing
 * the pointer address with the current stack position. Used for lazy
 * closure environment promotion: stack-based env_stack is copied to heap
 * only when the closure escapes (RETAIN with rc > 1).
 * 
 * Implementation:
 * - Uses a local variable as stack position marker
 * - Stack grows downward: older frames have higher addresses
 * - Returns true if ptr is in valid stack range above current position
 */
bool is_pointer_on_stack(const void *ptr) {
    if (!ptr) return false;
    
    // Get current stack position using a local variable
    volatile char stack_marker;
    uintptr_t stack_pos = (uintptr_t)&stack_marker;
    uintptr_t ptr_pos = (uintptr_t)ptr;
    
    // Stack grows downward on x86/ARM: older frames have higher addresses
    // Valid stack pointers are between current position and stack top
    #define STACK_SIZE_MAX (8UL * 1024 * 1024)  // 8 MB typical max stack
    
    // Check if pointer is in reasonable stack range
    // Stack grows down: caller frames have higher addresses than us
    if (ptr_pos >= stack_pos && ptr_pos < stack_pos + STACK_SIZE_MAX) {
        return true;
    }
    
    return false;
}

void subjective_c_register_release_fn(CljType type, SubjectiveCReleaseFn fn) {
    if (type < 0 || type >= CLJ_TYPE_COUNT) return;
    init_release_dispatch();
    g_release_dispatch[type] = fn ? fn : release_object_default;
}

// ============================================================================
// OUT OF MEMORY HELPER
// ============================================================================

void throw_oom(void) {
    // Use static OOM exception - no allocation needed!
    // This is critical: when we're out of memory, we can't allocate more memory
    extern CLJException *clj_oom_exception;
    
    // Update message (using static buffer in exception)
    // Note: We can safely modify the static exception's message field
    // since it's a singleton and won't be freed
    // Type information is available from stack trace
    strncpy(clj_oom_exception->message, "Out of memory", sizeof(clj_oom_exception->message) - 1);
    clj_oom_exception->message[sizeof(clj_oom_exception->message) - 1] = '\0';
    
    // Update file and line info
    strncpy(clj_oom_exception->file, __FILE__, sizeof(clj_oom_exception->file) - 1);
    clj_oom_exception->file[sizeof(clj_oom_exception->file) - 1] = '\0';
    clj_oom_exception->line = __LINE__;
    clj_oom_exception->col = 0;
    
    // Throw the static exception (no allocation)
    throw_exception_object(clj_oom_exception);
    abort(); // Ensure no return
}
