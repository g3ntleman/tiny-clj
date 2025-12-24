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
#include <subjective-c/thread_local.h>
#include <string.h>

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
// Public flag to enable zombie mode (NSZombieEnabled)
bool g_zombie_enabled = false;

/** @brief Enable zombie mode for debugging */
void enable_zombie_mode(void) {
    g_zombie_enabled = true;
}
#else
bool g_zombie_enabled = false;
void enable_zombie_mode(void) {
    // No-op in non-DEBUG builds
    g_zombie_enabled = false;
}
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
    
    // Safety check: ensure the pointer is valid
    if ((uintptr_t)v < 0x1000) {
        return;
    }
    
#ifdef DEBUG
    // Check for zombie object
    if (v->rc == ZOMBIE_RC) {
        // Zombie detected: throw exception with stacktrace and zombie object
        // Don't try to print object representation (may fail if object is corrupted)
        char message[512];
        snprintf(message, sizeof(message),
            "Attempted to retain zombie object %p (type=%s). "
            "This object was already freed but marked as zombie for debugging.",
            v, clj_type_name(v->type));
        CLJException *ex = make_exception(EXCEPTION_ZOMBIE_ACCESS, message, __FILE__, __LINE__, 0);
        if (ex) {
            ex->object = (CljObject*)v;  // Store zombie object in exception
            throw_exception_object(ex);
        }
        return;
    }
#endif
    
    // Happy path: valid object that tracks retains
    if ((uintptr_t)v >= 0x1000 && TRACKS_RETAINS(v)) {
        // Track retain call for profiling
        memory_profiler_track_retain(v);
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
    // Check for zombie object BEFORE checking for underflow
    if (v->rc == ZOMBIE_RC) {
        // Zombie detected: throw exception with stacktrace and zombie object
        // This indicates a double-free problem - the object was already freed
        // Don't try to print object representation (may fail if object is corrupted)
        char message[512];
        snprintf(message, sizeof(message),
            "Attempted to release zombie object %p (type=%s). "
            "This object was already freed but marked as zombie for debugging.",
            v, clj_type_name(v->type));
        CLJException *ex = make_exception(EXCEPTION_ZOMBIE_ACCESS, message, __FILE__, __LINE__, 0);
        if (ex) {
            ex->object = (CljObject*)v;  // Store zombie object in exception
            throw_exception_object(ex);
        }
        return;
    }
#endif
    
    // Check for underflow BEFORE decrementing
    if (v->rc == 0) {
        printf("❌ UNDERFLOW! Object %p (type=%s) already freed\n", v, clj_type_name(v->type));
        printf("🔍 Stack trace for object %p:\n", v);
        // Print stack trace or more debugging info
        throw_exception_formatted("UseAfterFreeError", __FILE__, __LINE__, 0,
            "Use-after-free detected! Object %p (type=%s) was already freed (rc=0). "
            "This indicates the object was released more times than retained, "
            "likely due to duplicate AUTORELEASE or incorrect memory management.",
            v, clj_type_name(v->type));
        return;
    }

    v->rc--;
    
    // Track release operation
    MEMORY_PROFILER_TRACK_RELEASE(v);
    
    if (v->rc == 0) { 
        if (g_debug_output_active) {
            printf("🔍 release: Object %p will be freed (rc=0)\n", v);
        }
#ifdef DEBUG
        // In zombie mode, mark object as zombie BEFORE deep release
        // This allows release_object_deep to access the object safely
        if (g_zombie_enabled) {
            v->rc = ZOMBIE_RC;  // Mark as zombie before deep release
        }
#endif
        release_object_deep(v); 
        DEALLOC(v); // Hook for memory profiling - marks as zombie if enabled

        if (g_debug_output_active) {
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
 * COW-friendly: Does NOT increase reference count.
 */
CljObject *autorelease(CljObject *v) {
    if (!v) return NULL;
    
    CLJ_ASSERT(g_pool.items && "autorelease_pool_init() not called");

    // Require active autorelease pool
    if (g_pool.cp_count == 0) {
        throw_exception_formatted("AutoreleasePoolError", __FILE__, __LINE__, 0,
                "autorelease() called without active autorelease pool! Object %p (type=%d) will not be automatically freed. "
                "This indicates missing autorelease_pool_push() or premature autorelease_pool_pop().", 
                v, v ? v->type : -1);
        return v;
    }
    
    // Grow items array if needed
    if (g_pool.count >= g_pool.capacity) {
        uint32_t old_capacity = g_pool.capacity;
        uint32_t new_capacity = g_pool.capacity * 2;
        g_pool.items = (CljObject**)realloc(g_pool.items, sizeof(CljObject*) * new_capacity);
        g_pool.capacity = new_capacity;
        fprintf(stderr, "⚠️  AutoreleasePool: items grew %u -> %u\n", old_capacity, new_capacity);
    }
    
    // Append object (no RETAIN - COW friendly!)
    g_pool.items[g_pool.count++] = v;
    
    // Track for memory profiling
    MEMORY_PROFILER_TRACK_AUTORELEASE(v);
    
    return v;
}

// ============================================================================
// CHECKPOINT-BASED AUTORELEASE POOL IMPLEMENTATION
// ============================================================================

// Dummy pool pointer for API compatibility (non-NULL sentinel)
// Non-NULL sentinel for API compatibility
static int g_pool_sentinel_value = 1;

/** @brief Push a new autorelease pool (checkpoint)
 * 
 * @return Non-NULL sentinel (for API compatibility, value is meaningless)
 * 
 * Creates a new checkpoint at the current item count. Objects added via 
 * autorelease() will be tracked until pop() clears them.
 */
void *autorelease_pool_push(void) {
    CLJ_ASSERT(g_pool.items && "autorelease_pool_init() not called");
    
    // Grow checkpoints array if needed
    if (g_pool.cp_count >= g_pool.cp_capacity) {
        uint32_t old_capacity = g_pool.cp_capacity;
        uint32_t new_capacity = g_pool.cp_capacity * 2;
        g_pool.checkpoints = (uint32_t*)realloc(g_pool.checkpoints, sizeof(uint32_t) * new_capacity);
        g_pool.cp_capacity = new_capacity;
        fprintf(stderr, "⚠️  AutoreleasePool: checkpoints grew %u -> %u\n", old_capacity, new_capacity);
    }
    
    // Push checkpoint (current item count)
    g_pool.checkpoints[g_pool.cp_count++] = g_pool.count;
    
    if (g_debug_output_active) {
        printf("🔍 autorelease_pool_push: checkpoint at %u (depth=%u)\n", 
               g_pool.count, g_pool.cp_count);
    }
    
    return &g_pool_sentinel_value;  // Non-NULL sentinel for API compatibility
}


/** @brief Pop and drain the current autorelease pool
 * 
 * @param pool Ignored (for API compatibility)
 * 
 * Removes the checkpoint. Objects are NOT released (weak reference semantics).
 * This matches the original CLJ_VECTOR_TRANSIENT_WEAK behavior where the pool
 * only tracks objects but doesn't own them.
 */
void autorelease_pool_pop(void *pool) {
    (void)pool;  // Unused, for API compatibility
    
    // Check for stack underflow
    if (g_pool.cp_count == 0) {
        printf("WARNING: autorelease_pool_pop() called on empty stack! "
               "This indicates more pop() calls than push() calls.\n");
        return;
    }
    
    // Get checkpoint (start index for this pool)
    uint32_t checkpoint = g_pool.checkpoints[--g_pool.cp_count];
    
    if (g_debug_output_active) {
        printf("🔍 autorelease_pool_pop: clearing %u objects (checkpoint=%u, count=%u)\n", 
               g_pool.count - checkpoint, checkpoint, g_pool.count);
    }
    
    // NOTE: We do NOT release objects here (weak reference semantics)
    // Objects are tracked for debugging/profiling but not owned by the pool.
    // This matches the original CLJ_VECTOR_TRANSIENT_WEAK behavior.
    
    // Reset count to checkpoint (objects are forgotten, not released)
    g_pool.count = checkpoint;
}

// Exception-safe cleanup function (called from CATCH blocks)
void autorelease_pool_cleanup_after_exception(void) {
    // Drain all pools
    while (g_pool.cp_count > 0) {
        autorelease_pool_pop(NULL);
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
        autorelease_pool_pop(NULL);
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
    
    // Free backing arrays
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
    
#ifdef DEBUG
    // For zombie objects, only cleanup CLJ_ATOM
    // For all other types, skip cleanup to avoid accessing freed memory
    if (v->rc == ZOMBIE_RC) {
        CljType type = v->type;
        if (type != CLJ_ATOM) {
            if (g_debug_output_active) {
                printf("🔍 release_object_deep: Skipping zombie object %p (type=%s)\n", v, clj_type_name(type));
            }
            return;
        }
        // For CLJ_ATOM, continue with cleanup to release the atom's value
        if (g_debug_output_active) {
            printf("🔍 release_object_deep: Cleaning up zombie CLJ_ATOM %p\n", v);
        }
    }
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
                CljVector *vec = as_vector(v);
                // Release all vector elements
                VECTOR_FOR_EACH(vec, elem) {
                    RELEASE(elem);
                }
                // Note: data array wird automatisch freigegeben
            }
            break;
            
        case CLJ_VECTOR_TRANSIENT_WEAK:
            {
                // For CLJ_VECTOR_TRANSIENT_WEAK, elements are not retained or released (weak references)
                // Note: data array wird automatisch freigegeben
            }
            break;
            
        case CLJ_MAP:
            {
                CljMap *map = as_map(v);
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
                CljList *list = as_list(v);
                if (g_debug_output_active) {
                    printf("🔍 release_object_deep: Freeing LIST object %p, first=%p, rest=%p\n", v, list->first, list->rest);
                }
                // Release head and tail elements - RELEASE handles NULL
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
            break;

        case CLJ_AST_NODE:
            {
                CljASTNode *node = as_ast_node(v);
                if (!node) {
                    break;
                }
                if (g_debug_output_active) {
                    printf("🔍 release_object_deep: Freeing AST node %p, first=%p, rest=%p, meta=%p, cache=%p\n",
                           v, node->first, node->rest, node->metadata, node->callsite_cache);
                }
                RELEASE(node->first);
                RELEASE(node->rest);
                RELEASE(node->metadata);
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
                    // Release closure environment - RELEASE handles NULL
                    RELEASE(func->env_stack);
                    // Release captured namespace reference
                    RELEASE(func->ns);
                    // Free function name (strdup'd in make_function)
                    if (func->name) {
                        free((void*)func->name);
                    }
                }
            }
            break;
            
        case CLJ_BYTE_ARRAY:
            {
                CljByteArray *ba = as_byte_array(v);
                if (ba && ba->data) {
                    free(ba->data);
                }
            }
            break;
            
        case CLJ_ATOM:
            {
                CljAtom *atom = as_atom(v);
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
                    if (ns->filename) {
                        free((void*)ns->filename);
                    }
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
