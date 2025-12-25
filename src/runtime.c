#include <stdlib.h>
#include "runtime.h"
// Note: symbol.h is included indirectly via namespace.h's forward declaration
// It's not directly used in runtime.c, but namespace.h needs it for CljSymbol definition
#include "namespace.h"
#include "meta.h"
#include "vector.h"
#include "memory.h"
#include "eval.h"  // For reset_eval_arg_depth()
#include "event_loop.h"     // For event_loop_clear()
#include "macro.h"          // For macro_cache_reset()
#include "map.h"            // For make_map()
#include "subjective-c/hashmap.h"        // For hashmap_register_release_fn()
#include "hash.h"           // For clj_hash_full()
// clj_equal_full is defined in equality.c
extern bool clj_equal_full(ID a, ID b);
#include "to_string.h"      // For to_string()
#include "subjective-c/callbacks.h"  // For clj_set_callbacks

// Statisch alloziertes globales Runtime-Struct (alle Zeiger mit NULL vorbelegt)
TinyClJRuntime g_runtime = {
    .ns_registry = NULL,
    .resolve_cache = NULL,
    .resolve_cache_epoch = 0,
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
    
    // Initialize autorelease pool first (needed by make_* functions)
    autorelease_pool_init();
    
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
        runtime->symbol_table = make_hashmap(512);  // HashMap for O(1) symbol lookup
    }
    
    // Initialize/reset CljObject* fields
    // NOTE: meta_registry is managed by meta_registry_init/meta_registry_cleanup
    // and should not be cleared here to avoid losing metadata across runtime_init()
    if (!runtime->resolve_cache) {
        ASSIGN(runtime->resolve_cache, make_map(RESOLVE_CACHE_SIZE));
    }
    if (runtime->resolve_cache_epoch == 0) {
        runtime->resolve_cache_epoch = 1;
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
    
    // Register hashmap release function with memory system
    hashmap_register_release_fn();
    
    // Register callbacks for subjective-c (HashMap, exceptions, etc.)
    clj_set_callbacks((CljCallbacks){
        .hash = clj_hash_full,
        .equal = clj_equal_full,
        .to_string = to_string
    });
}

void runtime_reset(TinyClJRuntime *runtime) {
    if (!runtime) return;
    
    reset_eval_state_current_ns();
    ns_cleanup();
    meta_registry_cleanup();
    macro_cache_reset();
    reset_eval_arg_depth();
    
    ASSIGN(runtime->task_queue, NULL);
    ASSIGN(runtime->timer_queue, NULL);
    ASSIGN(runtime->resolve_cache, NULL);
    runtime->resolve_cache_epoch = 0;
    ASSIGN(runtime->pool_stack, NULL);
    ASSIGN(runtime->meta_registry, NULL);
    
    runtime->builtins_registered = false;
    runtime->timer_id_counter = 0;
    event_loop_clear();
}

