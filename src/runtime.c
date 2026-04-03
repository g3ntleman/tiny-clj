#include <stdlib.h>
#include "runtime.h"
// Note: symbol.h is included indirectly via namespace.h's forward declaration
// It's not directly used in runtime.c, but namespace.h needs it for CljSymbol definition
#include "namespace.h"
#include "meta.h"
#include "vector.h"
#include "memory.h"
#include "exception.h"
#include "eval.h"  // For reset_eval_arg_depth()
#include "event_loop.h"     // For event_loop_clear()
#include "macro.h"          // For macro_cache_reset()
#include "hashmap.h"        // For hashmap_register_release_fn()
#include "hashset.h"        // For make_hashset(), hashset_register_release_fn()
#include "seq.h"            // For seq_register_release_fn()
#include "hash.h"           // For clj_hash_full()
#include "symbol.h"         // For init_special_symbols()
#include "builtins.h"       // For builtins_reset_cached_funcs()
#include "ast_canon.h"      // For ast_canon_reset_caches()
#include "eval_special_forms.h" // For eval_special_forms_reset_caches()
#include "embedded_sources.h"
#ifndef ESP32_BUILD
#include "gpio.h"
#endif
// clj_equal_full is defined in equality.c
extern bool clj_equal_full(ID a, ID b);
#include "to_string.h"      // For to_string(), make_string_description; strings.h for string_data
#include "callbacks.h"  // For clj_set_callbacks
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#define SYMBOL_TABLE_MAX_LOAD_PERCENT 90u

// Statically allocated global runtime struct (all pointers initialized to NULL).
TinyClJRuntime g_runtime = {
    .ns_registry = NULL,
    .resolve_cache_epoch = 0,
    .resolve_cache_generation = 0,
    .symbol_table = NULL,
    .meta_registry = NULL,
    .record_registry = NULL,
    .pool_stack = NULL,
    .builtins_registered = false,
    .task_queue = NULL,
    .timer_id_counter = 0
};

// Epoch for callsite-cache invalidation events (namespace mutations).
// Reset on runtime_reset() to start each runtime activation from a clean baseline.
static uint32_t g_resolve_cache_epoch_counter = 1;
// Activation generation paired with the epoch-1 bootstrap state in runtime_init().
static uint8_t g_resolve_cache_activation_generation = 0;
// Monotonic runtime activation id for caches tied to a runtime activation.
static uint32_t g_runtime_activation_counter = 0;
static uint32_t g_runtime_activation_id = 0;

#if !MEMORY_PROFILING_ENABLED
static inline uint8_t runtime_next_nonzero_u8(uint8_t *counter) {
    uint8_t next = (uint8_t)(*counter + 1u);
    if (next == 0u) {
        next = 1u;
    }
    *counter = next;
    return next;
}
#endif

static inline uint32_t runtime_next_nonzero_u32(uint32_t *counter) {
    uint32_t next = ++(*counter);
    if (next == 0u) {
        next = ++(*counter);
    }
    return next;
}

static bool runtime_bootstrap_builtins_present(void) {
    if (!SYM_PLUS) {
        return false;
    }

    CljNamespace *core_ns = ns_find("clojure.core");
    if (!core_ns || !core_ns->mappings) {
        return false;
    }

    return map_get_sentinel((ID)core_ns->mappings, (ID)SYM_PLUS, NULL) != NULL;
}

#if !MEMORY_PROFILING_ENABLED
static inline uint8_t runtime_next_resolve_cache_activation_generation(void) {
    return runtime_next_nonzero_u8(&g_resolve_cache_activation_generation);
}
#endif

static inline uint32_t runtime_next_activation_id_value(void) {
    return runtime_next_nonzero_u32(&g_runtime_activation_counter);
}

#if defined(ZOMBIE_ENABLED) && ZOMBIE_ENABLED
static void zombie_log_fn(CljObject *v, bool is_double_free) {
    WITH_AUTORELEASE_POOL({
        CljString *s = make_string_description((ID)v);
        if (s) {
            fputs(is_double_free ? "DOUBLE-FREE make_string_description: " : "ZOMBIE make_string_description: ", stderr);
            fputs(string_data((CljObject *)s), stderr);
            fputc('\n', stderr);
        }
    });
}
#endif

uint16_t runtime_next_resolve_epoch(uint8_t *out_generation) {
    uint32_t next = ++g_resolve_cache_epoch_counter;
    // Protect against 32-bit overflow to 0.
    if (next == 0) {
        next = ++g_resolve_cache_epoch_counter;
    }
    uint16_t epoch = (uint16_t)(next & 0xFFFFu);
    uint8_t generation = (uint8_t)((next >> 16) & 0xFFu);
    // Keep epoch 0 reserved for "cache disabled" and epoch 1 for lifecycle activation.
    while (epoch == 0 || epoch == 1) {
        next = ++g_resolve_cache_epoch_counter;
        epoch = (uint16_t)(next & 0xFFFFu);
        generation = (uint8_t)((next >> 16) & 0xFFu);
    }
    if ((next % 1000u) == 0u) {
        fprintf(stderr,
                "Warning: resolve_cache_epoch=%" PRIu32 " (16-bit limit=%u)\n",
                next, (unsigned)UINT16_MAX);
    }
    if (out_generation) {
        *out_generation = generation;
    }
    return epoch;
}

uint32_t runtime_activation_id(void) {
    return g_runtime_activation_id;
}

void runtime_init(TinyClJRuntime *runtime) {
    if (!runtime) return;
    subjective_c_register_main_thread();
    
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
        hashset_set_max_load_percent(runtime->symbol_table, SYMBOL_TABLE_MAX_LOAD_PERCENT);
    }
    
    // Activate callsite caching for this runtime activation without attributing
    // setup churn to invalidation counters.
    // When memory profiling is enabled, leave caching disabled so that
    // CallsiteCache allocations do not distort heap measurements.
#if !MEMORY_PROFILING_ENABLED
    runtime->resolve_cache_epoch = 1;
    runtime->resolve_cache_generation = runtime_next_resolve_cache_activation_generation();
#else
    runtime->resolve_cache_generation = 0;
#endif
    g_runtime_activation_id = runtime_next_activation_id_value();
    
    // Initialize event loop queues as transient vectors (only if not already set)
    if (!runtime->task_queue) {
        CljPersistentVector* task_vec = make_vector(8, false);
        if (task_vec) {
            CljTransientVector* transient_task = make_vector_transient(task_vec);
            RELEASE(task_vec); // vector_transient() retains the result
            // make_vector_transient returns owned; ASSIGN retains again.
            // Drop the local owned ref to avoid leaking one ref per runtime_init().
            ASSIGN(runtime->task_queue, transient_task);
            RELEASE(transient_task);
        }
    }
    // Timer queue is a static C array in event_loop.c – no heap init needed.
    
    // Reset primitive fields
    runtime->timer_id_counter = 0;
    
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

    // Initialize embedded source registry (static table + on-demand byte-array views).
    embedded_source_map_init();

    // runtime_init() is allowed to run repeatedly on an already bootstrapped host
    // process (for example inside unit tests that reuse the global runtime without a
    // preceding runtime_reset()). In that case, keep builtin bootstrap marked ready
    // so evalstate_ensure_builtins_ready() does not transiently re-register the full
    // native table under an already-tight heap budget.
    runtime->builtins_registered = runtime_bootstrap_builtins_present();
}

void runtime_reset(TinyClJRuntime *runtime) {
    if (!runtime) return;
    
    reset_eval_state_current_ns();
    meta_registry_cleanup();
    ns_cleanup();
    macro_cache_reset();
    builtins_reset_cached_funcs();
    ast_canon_reset_caches();
    eval_special_forms_reset_caches();
    reset_eval_arg_depth();
    
    ASSIGN(runtime->task_queue, NULL);
    // Reset cache epoch to disabled state. runtime_init() re-enables callsite caching
    // with a fresh monotonic epoch for the next runtime activation.
    runtime->resolve_cache_epoch = 0;
    runtime->resolve_cache_generation = 0;
    g_resolve_cache_epoch_counter = 1;
    g_resolve_cache_activation_generation = 0;
    g_runtime_activation_id = 0;
    ASSIGN(runtime->pool_stack, NULL);
    ASSIGN(runtime->meta_registry, NULL);
    ASSIGN(runtime->record_registry, NULL);
    
    runtime->builtins_registered = false;
    runtime->timer_id_counter = 0;
    event_loop_clear();
#ifndef ESP32_BUILD
    gpio_runtime_reset_state();
#endif
}
