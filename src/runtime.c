#include <stdlib.h>
#include "runtime.h"
// Note: symbol.h is included indirectly via namespace.h's forward declaration
// It's not directly used in runtime.c, but namespace.h needs it for CljSymbol definition
#include "namespace.h"
#include "meta.h"
#include "vector.h"
#include "memory.h"

// Statisch alloziertes globales Runtime-Struct (alle Zeiger mit NULL vorbelegt)
TinyClJRuntime g_runtime = {
    .ns_registry = NULL,
    .clojure_core_cache = NULL,
    .symbol_table = NULL,
    .meta_registry = NULL,
    .pool_stack = NULL,
    .builtins_registered = false,
    .task_queue = NULL,
    .timer_queue = NULL,
    .timer_id_counter = 0
};

void runtime_init(TinyClJRuntime *runtime) {
    if (!runtime) return;
    
    // Initialize all fields individually using ASSIGN (allows multiple calls with same pointer)
    // ASSIGN automatically handles releasing old values, so multiple calls are safe
    // Caches are automatically rebuilt when needed, so no need to preserve them
    
    // Reset namespace registry (transient Map)
    // Note: ns_registry is a transient Map: Symbol (namespace name) → CljNamespace*
    // Use consolidated reset function (DRY principle)
    ns_reset_registry();
    // CRITICAL: Don't reset symbol_table - it preserves SYM_CLOJURE_CORE and other special symbols
    // The symbol table is cleaned up by symbol_table_cleanup() if needed
    // If we reset it here, intern_symbol will create new symbols that don't match SYM_CLOJURE_CORE
    // ASSIGN(runtime->symbol_table, NULL);  // DON'T reset - preserves SYM_CLOJURE_CORE
    
    // Reset meta registry
    ASSIGN(runtime->meta_registry, NULL);
    
    // Reset pool stack (transient vector)
    if (runtime->pool_stack) {
        RELEASE(runtime->pool_stack);
        runtime->pool_stack = NULL;
    }
    // Initialize pool_stack as transient vector
    CljVector* pool_vec = make_vector(0, CLJ_VECTOR);
    if (pool_vec) {
        CljVector* transient_pool = vector_transient(pool_vec);
        RELEASE(pool_vec);
        runtime->pool_stack = transient_pool;
    }
    
    // Reset builtins flag
    runtime->builtins_registered = false;
    
    // Reset timer counter
    runtime->timer_id_counter = 0;
    
    // Namespace registry is already initialized by ns_reset_registry() above
    
    // Initialize event loop queues as transient vectors using ASSIGN
    // Only create if not already set (allows multiple calls)
    if (!runtime->task_queue) {
        CljVector* task_vec = make_vector(8, CLJ_VECTOR);
        if (task_vec) {
            CljVector* transient_task = vector_transient(task_vec);
            RELEASE(task_vec); // vector_transient() retains the result
            ASSIGN(runtime->task_queue, transient_task);
        }
    }
    if (!runtime->timer_queue) {
        CljVector* timer_vec = make_vector(8, CLJ_VECTOR);
        if (timer_vec) {
            CljVector* transient_timer = vector_transient(timer_vec);
            RELEASE(timer_vec); // vector_transient() retains the result
            ASSIGN(runtime->timer_queue, transient_timer);
        }
    }
}

void runtime_free(TinyClJRuntime *runtime) {
    if (!runtime) return;
    
    // Cleanup in korrekter Reihenfolge
    // Pools werden automatisch beim nächsten Test geleert
    
    // Cleanup symbol table and meta registry
    // CRITICAL: Don't cleanup symbol table - it contains SYM_CLOJURE_CORE and other symbols
    // that are used by namespaces. If we clean it up, intern_symbol will create new symbols
    // that don't match the ones stored in namespace mappings, causing lookup failures.
    // The symbol table will persist across tests, which is fine since symbols are interned.
    meta_registry_cleanup();
    
    // Cleanup namespaces (caches will be automatically rebuilt when needed)
    // Note: ns_cleanup() will release all namespaces from the map and then release the map
    ns_cleanup();
    
    // Cleanup event loop queues
    if (runtime->task_queue) {
        CljVector *tvec = runtime->task_queue;
        if (TAG(tvec) == CLJ_VECTOR_TRANSIENT) {
            // Release all elements in transient vector
            int count = vector_count(tvec);
            for (int i = 0; i < count; i++) {
                ID elem = vector_nth(tvec, i);
                if (elem) {
                    RELEASE(elem);
                }
            }
            RELEASE(tvec);
        }
        runtime->task_queue = NULL;
    }
    if (runtime->timer_queue) {
        CljVector *tvec = runtime->timer_queue;
        if (TAG(tvec) == CLJ_VECTOR_TRANSIENT) {
            // Release all elements in transient vector (timer tasks as maps)
            int count = vector_count(tvec);
            for (int i = 0; i < count; i++) {
                ID elem = vector_nth(tvec, i);
                if (elem) {
                    RELEASE(elem);
                }
            }
            RELEASE(tvec);
        }
        runtime->timer_queue = NULL;
    }
    
    // Drain all autorelease pools before resetting runtime
    if (runtime->pool_stack) {
        while (vector_count(runtime->pool_stack) > 0) {
            unsigned int stack_depth = vector_count(runtime->pool_stack);
            CljVector *pool = (CljVector*)vector_nth(runtime->pool_stack, stack_depth - 1);
            if (pool) {
                autorelease_pool_pop(pool);
            } else {
                ASSIGN(runtime->pool_stack, vector_pop(runtime->pool_stack));
            }
        }
    }
    
    // Reset all fields individually using ASSIGN
    // Caches will be automatically rebuilt when needed
    // Reset namespace registry (transient Map)
    // Note: ns_registry is already cleaned up by ns_cleanup() above
    // Just set to NULL here
    runtime->ns_registry = NULL;
    
    // Reset caches (will be automatically rebuilt when needed)
    // Note: clojure_core_cache is just a pointer to a CljNamespace in the registry
    // We don't use ASSIGN here because we're just resetting the pointer, not releasing the object
    runtime->clojure_core_cache = NULL;
    // CRITICAL: Don't reset symbol_table - it preserves SYM_CLOJURE_CORE and other special symbols
    // If we reset it here, intern_symbol will create new symbols that don't match SYM_CLOJURE_CORE
    
    // Reset meta registry
    ASSIGN(runtime->meta_registry, NULL);
    
    // Reset pool stack (pools were already released above)
    if (runtime->pool_stack) {
        RELEASE(runtime->pool_stack);
        runtime->pool_stack = NULL;
    }
    
    // Reset builtins flag
    runtime->builtins_registered = false;
    
    // Reset timer counter
    runtime->timer_id_counter = 0;
    
}

// Legacy functions removed - all builtins now use namespace registration
