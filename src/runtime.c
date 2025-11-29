#include <stdlib.h>
#include "runtime.h"
// Note: symbol.h is included indirectly via namespace.h's forward declaration
// It's not directly used in runtime.c, but namespace.h needs it for CljSymbol definition
#include "namespace.h"
#include "meta.h"
#include "vector.h"
#include "memory.h"
#include "function_call.h"  // For reset_eval_arg_depth()
#include "event_loop.h"     // For event_loop_clear()
#include "map.h"            // For make_map()

// Symbol resolution cache size: 16 entries (good balance between hit rate and memory usage)
#define RESOLVE_CACHE_SIZE 16

// Statisch alloziertes globales Runtime-Struct (alle Zeiger mit NULL vorbelegt)
TinyClJRuntime g_runtime = {
    .ns_registry = NULL,
    .resolve_cache = NULL,
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
    
    // Initialize all fields (allows multiple calls with same pointer)
    // ASSIGN automatically handles releasing old values, so multiple calls are safe
    
    // Reset namespace registry (transient Map: Symbol → CljNamespace*)
    // Only reset if not already initialized (allows multiple calls without losing state)
    if (!runtime->ns_registry) {
        ns_reset_registry();
    }
    
    // CRITICAL: Don't reset symbol_table - it preserves SYM_CLOJURE_CORE and other special symbols
    // If we reset it here, intern_symbol will create new symbols that don't match SYM_CLOJURE_CORE
    if (!runtime->symbol_table) {
        runtime->symbol_table = make_vector(16, CLJ_VECTOR);
    }
    
    // Initialize/reset CljObject* fields
    // NOTE: meta_registry is managed by meta_registry_init/meta_registry_cleanup
    // and should not be cleared here to avoid losing metadata across runtime_init()
    if (!runtime->resolve_cache) {
        ASSIGN(runtime->resolve_cache, make_map(RESOLVE_CACHE_SIZE));
    }
    
    // Initialize event loop queues as transient vectors (only if not already set)
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
    
    // Reset primitive fields
    runtime->builtins_registered = false;
    runtime->timer_id_counter = 0;
}

void runtime_reset(TinyClJRuntime *runtime) {
    if (!runtime) return;
    
    // Cleanup in correct order
    // CRITICAL: Don't cleanup symbol_table - it preserves SYM_CLOJURE_CORE and other special symbols
    // that are used by namespaces. If we clean it up, intern_symbol will create new symbols
    // that don't match the ones stored in namespace mappings, causing lookup failures.
    // The symbol table will persist across tests, which is fine since symbols are interned.
    meta_registry_cleanup();
    ns_cleanup();
    
    // Reset static variables
    reset_eval_arg_depth();
    
    // Cleanup all CljObject* fields using ASSIGN (automatically frees via release_object_deep())
    ASSIGN(runtime->task_queue, NULL);
    ASSIGN(runtime->timer_queue, NULL);
    ASSIGN(runtime->resolve_cache, NULL);
    ASSIGN(runtime->pool_stack, NULL);
    ASSIGN(runtime->ns_registry, NULL);
    ASSIGN(runtime->meta_registry, NULL);
    
    // Reset primitive fields
    runtime->builtins_registered = false;
    runtime->timer_id_counter = 0;
    
    // Clear event loop queues (they will be reinitialized in runtime_init())
    event_loop_clear();
}

