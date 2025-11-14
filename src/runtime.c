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
    .pool_stack = {NULL},
    .pool_stack_top = -1,
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
    
    // Reset namespace registry
    // Note: ns_registry is a CljNamespace* (plain C struct), not CljObject*, so direct assignment
    runtime->ns_registry = NULL;
    
    // Reset caches (will be automatically rebuilt when needed)
    // Note: clojure_core_cache is a CljNamespace* (plain C struct), not CljObject*, so direct assignment
    runtime->clojure_core_cache = NULL;
    // CRITICAL: Don't reset symbol_table - it preserves SYM_CLOJURE_CORE and other special symbols
    // The symbol table is cleaned up by symbol_table_cleanup() if needed
    // If we reset it here, intern_symbol will create new symbols that don't match SYM_CLOJURE_CORE
    // ASSIGN(runtime->symbol_table, NULL);  // DON'T reset - preserves SYM_CLOJURE_CORE
    
    // Reset meta registry
    ASSIGN(runtime->meta_registry, NULL);
    
    // Reset pool stack (array of pointers)
    // runtime_free() should have already freed all pools and set them to NULL
    for (int i = 0; i < MAX_POOL_DEPTH; i++) {
        runtime->pool_stack[i] = NULL;
    }
    runtime->pool_stack_top = -1;
    
    // Reset builtins flag
    runtime->builtins_registered = false;
    
    // Reset timer counter
    runtime->timer_id_counter = 0;
    
    // Initialize event loop queues as transient vectors using ASSIGN
    // Only create if not already set (allows multiple calls)
    if (!runtime->task_queue) {
        CljVector* task_vec = make_vector(8, CLJ_VECTOR);
        if (task_vec) {
            CljVector* transient_task = vector_transient(task_vec);
            RELEASE((ID)task_vec); // vector_transient() retains the result
            ASSIGN(runtime->task_queue, (ID)transient_task);
        }
    }
    if (!runtime->timer_queue) {
        CljVector* timer_vec = make_vector(8, CLJ_VECTOR);
        if (timer_vec) {
            CljVector* transient_timer = vector_transient(timer_vec);
            RELEASE((ID)timer_vec); // vector_transient() retains the result
            ASSIGN(runtime->timer_queue, (ID)transient_timer);
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
    ns_cleanup();
    
    // Cleanup event loop queues
    if (runtime->task_queue) {
        CljVector *tvec = runtime->task_queue;
        if (TAG((ID)tvec) == CLJ_VECTOR_TRANSIENT) {
            // Release all elements in transient vector
            int count = vector_count(tvec);
            for (int i = 0; i < count; i++) {
                ID elem = vector_nth(tvec, i);
                if (elem) {
                    RELEASE(elem);
                }
            }
            RELEASE((ID)tvec);
        }
        runtime->task_queue = NULL;
    }
    if (runtime->timer_queue) {
        CljVector *tvec = runtime->timer_queue;
        if (TAG((ID)tvec) == CLJ_VECTOR_TRANSIENT) {
            // Release all elements in transient vector (timer tasks as maps)
            int count = vector_count(tvec);
            for (int i = 0; i < count; i++) {
                ID elem = vector_nth(tvec, i);
                if (elem) {
                    RELEASE(elem);
                }
            }
            RELEASE((ID)tvec);
        }
        runtime->timer_queue = NULL;
    }
    
    // CRITICAL: Drain all autorelease pools before resetting runtime
    // This ensures that objects from previous tests don't leak into the next test
    while (runtime->pool_stack_top >= 0) {
        CljVector *pool = runtime->pool_stack[runtime->pool_stack_top];
        if (pool) {
            // Use autorelease_pool_pop to properly release all objects
            autorelease_pool_pop(pool);
        } else {
            // Pool pointer is NULL, just decrement stack
            runtime->pool_stack_top--;
        }
    }
    
    // Reset all fields individually using ASSIGN
    // Caches will be automatically rebuilt when needed
    // Reset namespace registry
    // Note: ns_registry is a CljNamespace* (plain C struct), not CljObject*, so direct assignment
    runtime->ns_registry = NULL;
    
    // Reset caches (will be automatically rebuilt when needed)
    // Note: clojure_core_cache is a CljNamespace* (plain C struct), not CljObject*, so direct assignment
    runtime->clojure_core_cache = NULL;
    // CRITICAL: Don't reset symbol_table - it preserves SYM_CLOJURE_CORE and other special symbols
    // If we reset it here, intern_symbol will create new symbols that don't match SYM_CLOJURE_CORE
    
    // Reset meta registry
    ASSIGN(runtime->meta_registry, NULL);
    
    // Reset pool stack (array of pointers)
    // Note: Pools were already released in the loop above (lines 128-137),
    // so just set to NULL without releasing again to avoid double-free
    for (int i = 0; i < MAX_POOL_DEPTH; i++) {
        runtime->pool_stack[i] = NULL;
    }
    runtime->pool_stack_top = -1;
    
    // Reset builtins flag
    runtime->builtins_registered = false;
    
    // Reset timer counter
    runtime->timer_id_counter = 0;
    
}

// Legacy functions removed - all builtins now use namespace registration
