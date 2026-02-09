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
#include "hashmap.h"        // For hashmap_register_release_fn()
#include "hashset.h"        // For make_hashset(), hashset_register_release_fn()
#include "seq.h"            // For seq_register_release_fn()
#include "hash.h"           // For clj_hash_full()
#include "symbol.h"         // For init_special_symbols()
#include "builtins.h"       // For builtins_reset_cached_funcs()
#include "eval_special_forms.h" // For eval_special_forms_reset_caches()
// clj_equal_full is defined in equality.c
extern bool clj_equal_full(ID a, ID b);
#include "to_string.h"      // For to_string(), pr_str; strings.h for string_data
#include "callbacks.h"  // For clj_set_callbacks
#include "embedded_sources.h"
#include <stdint.h>
#include <stdbool.h>

// Statically allocated global runtime struct (all pointers initialized to NULL).
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
    .timer_id_counter = 0,
    .embedded_source_map = NULL
};

// Monotonic epoch for callsite + resolve cache invalidation.
// Must never be reset to avoid re-validating stale cached pointers across runtime_reset().
static uint64_t g_resolve_cache_epoch_counter = 1;

#if defined(ZOMBIE_ENABLED) && ZOMBIE_ENABLED
static void zombie_log_fn(CljObject *v, bool is_double_free) {
    WITH_AUTORELEASE_POOL({
        CljString *s = pr_str((ID)v);
        if (s) {
            fputs(is_double_free ? "DOUBLE-FREE pr_str: " : "ZOMBIE pr_str: ", stderr);
            fputs(string_data((CljObject *)s), stderr);
            fputc('\n', stderr);
        }
    });
}
#endif

void embedded_source_map_init(void) {
    /* No embedded sources in this build; embedded_source_map remains NULL. */
}

uint64_t runtime_next_resolve_epoch(void) {
    uint64_t next = ++g_resolve_cache_epoch_counter;
    // Protect against overflow to 0 (epoch 0 means disabled)
    if (next == 0) {
        next = ++g_resolve_cache_epoch_counter;
    }
    return next;
}

void runtime_ensure_resolve_cache(TinyClJRuntime *runtime) {
    // Resolve cache is disabled; stub retained for API compatibility.
    (void)runtime;
}

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
        runtime->symbol_table = make_hashset(512);  // HashSet for O(1) symbol lookup
    }
    
    // Disable resolve_cache; keep epoch non-zero for callsite caches.
    ASSIGN(runtime->resolve_cache, NULL);
    runtime->resolve_cache_epoch = runtime_next_resolve_epoch();
    
    // Initialize event loop queues as transient vectors (only if not already set)
    if (!runtime->task_queue) {
        CljPersistentVector* task_vec = make_vector(8, false);
        if (task_vec) {
            CljTransientVector* transient_task = vector_transient(task_vec);
            RELEASE(task_vec); // vector_transient() retains the result
            ASSIGN(runtime->task_queue, transient_task);
        }
    }
    if (!runtime->timer_queue) {
        CljPersistentVector* timer_vec = make_vector(8, false);
        if (timer_vec) {
            CljTransientVector* transient_timer = vector_transient(timer_vec);
            RELEASE(timer_vec); // vector_transient() retains the result
            ASSIGN(runtime->timer_queue, transient_timer);
        }
    }
    
    // Reset primitive fields
    runtime->builtins_registered = false;
    runtime->timer_id_counter = 0;
    runtime->embedded_source_map = NULL;
    
    // Register hashmap release function with memory system
    hashmap_register_release_fn();
    hashset_register_release_fn();
    
    // Register seq release function (CljLazySeq) with memory system
    seq_register_release_fn();
    
    // Register callbacks for subjective-c (HashMap, exceptions, etc.)
    clj_set_callbacks((CljCallbacks){
        .hash = clj_hash_full,
        .equal = clj_equal_full,
        .to_string = to_string
    });

#if defined(ZOMBIE_ENABLED) && ZOMBIE_ENABLED
    subjective_c_set_zombie_log_fn(zombie_log_fn);
#endif

    // Initialize special symbols/keywords early, before any code can intern the same names.
    // This must happen AFTER callbacks are set, because the symbol table is a HashMap that
    // depends on clj_hash()/clj_equal() for correct behavior.
    init_special_symbols();
}

void runtime_reset(TinyClJRuntime *runtime) {
    if (!runtime) return;
    
    reset_eval_state_current_ns();
    ns_cleanup();
    meta_registry_cleanup();
    macro_cache_reset();
    builtins_reset_cached_funcs();
    eval_special_forms_reset_caches();
    reset_eval_arg_depth();
    
    ASSIGN(runtime->task_queue, NULL);
    ASSIGN(runtime->timer_queue, NULL);
    ASSIGN(runtime->resolve_cache, NULL);
    // Invalidate callsite caches; resolve cache remains disabled.
    runtime->resolve_cache_epoch = runtime_next_resolve_epoch();
    ASSIGN(runtime->pool_stack, NULL);
    ASSIGN(runtime->meta_registry, NULL);
    
    runtime->builtins_registered = false;
    runtime->timer_id_counter = 0;
    ASSIGN(runtime->embedded_source_map, NULL);
    event_loop_clear();
}
