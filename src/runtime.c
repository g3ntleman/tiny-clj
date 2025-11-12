#include <string.h>
#include <stdlib.h>
#include "runtime.h"
#include "namespace.h"
#include "symbol.h"
#include "meta.h"
#include "vector.h"
#include "value.h"

// Statisch alloziertes globales Runtime-Struct
TinyClJRuntime g_runtime = {
    .ns_registry = NULL,
    .clojure_core_cache = NULL,
    .symbol_table = NULL,
    .meta_registry = NULL,
    .pool_stack = {NULL},
    .pool_stack_top = -1,
    .builtins_registered = false,
    .timer_id_counter = 0
};

void runtime_init(void) {
    // Preserve clojure_core_cache and symbol_table across init calls (important for tests)
    // Symbol table should ALWAYS be preserved if set (SYM_DEF, SYM_FN, etc. are needed
    // even before clojure.core is loaded, to parse and evaluate def expressions)
    // clojure.core cache should also be preserved if set
    void *preserved_cache = g_runtime.clojure_core_cache;
    void *preserved_symbol_table = g_runtime.symbol_table;  // Always preserve if set
    
    
    // Preserve clojure.core namespace in registry for test isolation
    // Only clojure.core should persist across tests, all other namespaces should be reset
    CljNamespace *clojure_core = (CljNamespace*)preserved_cache;
    
    memset(&g_runtime, 0, sizeof(TinyClJRuntime));
    g_runtime.pool_stack_top = -1;
    g_runtime.builtins_registered = false;
    g_runtime.timer_id_counter = 0;
    
    // Restore cache and symbol table if they were set
    // Symbol table is needed for symbol interning to work correctly
    // clojure.core should persist across test runs
    g_runtime.clojure_core_cache = preserved_cache;
    g_runtime.symbol_table = preserved_symbol_table;
    
    
    // CRITICAL: Reset namespace registry, but keep clojure.core for test isolation
    // This ensures that user namespaces and other test-specific namespaces don't leak
    // between tests. Only clojure.core should persist.
    if (clojure_core) {
        // Re-register clojure.core in registry (it's the only namespace that should persist)
        clojure_core->next = NULL;
        g_runtime.ns_registry = (void*)clojure_core;
    } else {
        g_runtime.ns_registry = NULL;
    }
    
    // Initialize event loop queues as transient vectors
    if (!g_runtime.task_queue) {
        CljPersistentVector* task_vec = make_vector(8, false);
        if (task_vec) {
            g_runtime.task_queue = (CljPersistentVector*)transient((ID)task_vec);
            RELEASE((ID)task_vec); // transient() retains the result
        }
    }
    if (!g_runtime.timer_queue) {
        CljPersistentVector* timer_vec = make_vector(8, false);
        if (timer_vec) {
            g_runtime.timer_queue = (CljPersistentVector*)transient((ID)timer_vec);
            RELEASE((ID)timer_vec); // transient() retains the result
        }
    }
}

void runtime_free(void) {
    // Cleanup in korrekter Reihenfolge
    // Pools werden automatisch beim nächsten Test geleert
    
    // Preserve clojure_core_cache across free calls (important for tests)
    // clojure.core should persist across test runs
    // Note: We preserve the cache pointer, but ns_cleanup() will free the namespace
    // So we need to preserve it BEFORE ns_cleanup() and restore it AFTER
    void *preserved_cache = g_runtime.clojure_core_cache;
    
    // Preserve symbol table ALWAYS if set (important for tests)
    // Symbol table is needed for symbol interning to work correctly
    // If we clean it up, new symbols will have different pointers than stored symbols
    // CRITICAL: Symbol table should ALWAYS be preserved if set, not just when cache is set
    // This ensures that SYM_TIME, SYM_DEF, etc. remain consistent across tests
    void *preserved_symbol_table = g_runtime.symbol_table;
    
    
    // CRITICAL: Never cleanup symbol table in tests - it must persist across test runs
    // Only cleanup if we're actually shutting down (preserved_cache is NULL)
    // In tests, preserved_cache is always set, so symbol table is never cleaned up
    if (!preserved_cache && !preserved_symbol_table) {
        symbol_table_cleanup();
    }
    meta_registry_cleanup();
    
    // Don't cleanup namespaces if clojure.core cache is set (preserve for tests)
    // ns_cleanup() would free the namespace, making the cache pointer invalid
    // For tests, we want clojure.core to persist across test runs
    if (!preserved_cache) {
        ns_cleanup();
    }
    
    // Cleanup event loop queues
    if (g_runtime.task_queue) {
        CljPersistentVector *tvec = g_runtime.task_queue;
        if (TAG((ID)tvec) == CLJ_TRANSIENT_VECTOR) {
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
        g_runtime.task_queue = NULL;
    }
    if (g_runtime.timer_queue) {
        CljPersistentVector *tvec = g_runtime.timer_queue;
        if (TAG((ID)tvec) == CLJ_TRANSIENT_VECTOR) {
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
        g_runtime.timer_queue = NULL;
    }
    
    // Reset Runtime (statisch alloziert, bleibt bestehen)
    memset(&g_runtime, 0, sizeof(TinyClJRuntime));
    g_runtime.pool_stack_top = -1;
    g_runtime.builtins_registered = false; // Reset builtins_registered flag
    
    // Restore cache and symbol table if they were set (clojure.core should persist)
    g_runtime.clojure_core_cache = preserved_cache;
    g_runtime.symbol_table = preserved_symbol_table;
    
}

// Legacy functions removed - all builtins now use namespace registration
