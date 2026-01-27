#include "event_loop.h"
#include "eval.h"
#include "symbol.h"
#include "memory.h"
#include "exception.h"
#include "channel.h"
#include "types.h"  // For ZOMBIE_RC, clj_type_name
#include "runtime.h"
#include "vector.h"
#include "map.h"
#include "value.h"  // For as_fixnum, clj_true
#include "kv_macros.h"  // For KV_VALUE
#include "to_string.h"  // For to_string
#include <stdbool.h>
#include <sys/time.h>

// Task Map keys (static keywords, similar to SYM_IF etc.)
static CljSymbol *KW_FN;
static CljSymbol *KW_RESULT_CHAN;
static CljSymbol *KW_SCHEDULED_SEC;
static CljSymbol *KW_SCHEDULED_MSEC;
static CljSymbol *KW_PERIODIC;
static CljSymbol *KW_PERIOD_MS;
static CljSymbol *KW_TIMER_ID;

// Timer-Task Map index constants for direct O(1) access
// CRITICAL: The order of these enum values MUST match exactly the order of keys
// in task_timer_to_map() when calling make_transient_map_from_kv().
// This pattern (enum for indices, guaranteed key order, direct index access)
// can be reused for future defrecord implementations.
enum {
    TIMER_TASK_IDX_FN = 0,
    TIMER_TASK_IDX_SCHEDULED_SEC,
    TIMER_TASK_IDX_SCHEDULED_MSEC,
    TIMER_TASK_IDX_PERIODIC,
    TIMER_TASK_IDX_PERIOD_MS,
    TIMER_TASK_IDX_TIMER_ID
};

// Helper functions for normal Tasks as Maps
// Task Map keys: :fn, :result-chan
static CljMap* task_to_map(CljObject *fn, CljMap *result_chan) {
    CljMap *task_map = make_map(2);
    if (!task_map) return NULL;
    
    CljMap *tmap = map_transient(task_map);
    RELEASE(task_map);
    if (!tmap) return NULL;
    
    map_conj(tmap, KW_FN, fn);
    if (result_chan) {
        map_conj(tmap, KW_RESULT_CHAN, result_chan);
    }
    
    CljMap *pmap = map_persistent(tmap);
    RELEASE(tmap);
    return pmap;
}

static bool task_from_map(CljMap *task_map, CljObject **fn, CljMap **result_chan) {
    if (!task_map) return false;
    
    ID fn_val = map_get_sentinel(task_map, KW_FN, NULL);
    ID result_chan_val = map_get_sentinel(task_map, KW_RESULT_CHAN, NULL);
    
    if (!fn_val) return false;
    
    if (fn) *fn = (CljObject*)fn_val;
    if (result_chan) *result_chan = result_chan_val ? (CljMap*)result_chan_val : NULL;
    
    return true;
}

// Helper functions for Timer-Tasks as Maps
// Timer-Task Map keys: :fn, :scheduled-sec, :scheduled-msec, :periodic, :period-ms, :timer-id
//
// CRITICAL: The order of keys in make_transient_map_from_kv() MUST match exactly
// the order of the TIMER_TASK_IDX_* enum values defined above.
// The optimized Getter functions (task_get_scheduled_sec(), etc.) rely on this
// guaranteed key order for direct O(1) index access instead of O(n) keyword search.
// If you change the key order here, you MUST also update the enum values accordingly.
static CljMap* task_timer_to_map(CljObject *fn, int scheduled_sec, int scheduled_msec, bool periodic, int period_ms, int timer_id) {
    CljMap *tmap = make_transient_map_from_kv(6,
        KW_FN, fn,
        KW_SCHEDULED_SEC, fixnum((int32_t)scheduled_sec),
        KW_SCHEDULED_MSEC, fixnum((int32_t)scheduled_msec),
        KW_PERIODIC, periodic ? clj_true : clj_false,
        KW_PERIOD_MS, fixnum((int32_t)period_ms),
        KW_TIMER_ID, fixnum((int32_t)timer_id));
    if (!tmap) return NULL;
    
    CljMap *pmap = map_persistent(tmap);
    RELEASE(tmap);
    return pmap;
}

// Task Getter functions (static inline for performance)
// Works for both normal tasks and timer tasks
static inline ID task_get_fn(CljMap *task_map) {
    return map_get_sentinel(task_map, KW_FN, NULL);
}

// Timer-Task Getter functions (optimized with direct index access O(1))
// These use direct index access instead of map_get() for better performance.
// CRITICAL: These only work correctly for Timer-Task maps created by task_timer_to_map(),
// which guarantees the key order matches the TIMER_TASK_IDX_* enum values.
static inline int task_get_scheduled_sec(CljMap *task_map) {
    ID val = KV_VALUE(task_map->data, TIMER_TASK_IDX_SCHEDULED_SEC);
    return val ? (int)as_fixnum(val) : 0;
}

static inline int task_get_scheduled_msec(CljMap *task_map) {
    ID val = KV_VALUE(task_map->data, TIMER_TASK_IDX_SCHEDULED_MSEC);
    return val ? (int)as_fixnum(val) : 0;
}

static inline bool task_get_periodic(CljMap *task_map) {
    ID val = KV_VALUE(task_map->data, TIMER_TASK_IDX_PERIODIC);
    return val == clj_true;
}

static inline int task_get_period_ms(CljMap *task_map) {
    ID val = KV_VALUE(task_map->data, TIMER_TASK_IDX_PERIOD_MS);
    return val ? (int)as_fixnum(val) : 0;
}

static inline int task_get_timer_id(CljMap *task_map) {
    ID val = KV_VALUE(task_map->data, TIMER_TASK_IDX_TIMER_ID);
    return val ? (int)as_fixnum(val) : 0;
}

// Forward declarations
static void timer_process(void);
static void timer_insert_sorted_map(CljMap *task_map);

// Helper function to ensure task queue is initialized
static CljPersistentVector* task_queue_get(void) {
    if (!g_runtime.task_queue) {
        CljPersistentVector* task_vec = make_vector(8, CLJ_VECTOR_PERSISTENT);
        if (task_vec) {
            g_runtime.task_queue = (CljPersistentVector*)vector_transient(task_vec);
            RELEASE(task_vec);
        }
    }
    if (!g_runtime.task_queue) return NULL;
    CljPersistentVector *task_vec = g_runtime.task_queue;
    // Safety check: validate pointer before calling TAG
    if ((uintptr_t)task_vec < 0x1000) {
        return NULL; // Invalid pointer
    }
    // Safety: check tag only if pointer is valid
    // In DEBUG builds, CLJ_ASSERT will verify the tag, but we need to avoid crashes
    // if the pointer is invalid (e.g., freed but not NULL)
#ifdef DEBUG
    if ((uintptr_t)task_vec >= 0x1000) {
        CljType tag = TAG(task_vec);
        CLJ_ASSERT(tag == CLJ_VECTOR_TRANSIENT);
    }
#endif
    return task_vec;
}

// Helper function to ensure timer queue is initialized
static CljPersistentVector* timer_queue_get(void) {
    if (!g_runtime.timer_queue) {
        CljPersistentVector* timer_vec = make_vector(8, CLJ_VECTOR_PERSISTENT);
        ASSIGN(g_runtime.timer_queue, (CljPersistentVector*)vector_transient(timer_vec));
        RELEASE(timer_vec);
    }
    if (!g_runtime.timer_queue) return NULL;
    CljPersistentVector *timer_vec = g_runtime.timer_queue;
    
    // Safety check: validate pointer before calling TAG
    if ((uintptr_t)timer_vec < 0x1000) {
        return NULL; // Invalid pointer
    }
    
    // Safety: validate pointer before calling TAG
    if (timer_vec && (uintptr_t)timer_vec >= 0x1000) {
        CLJ_ASSERT(TAG(timer_vec) == CLJ_VECTOR_TRANSIENT);
    }
    return timer_vec;
}

// Helper function to remove element from timer queue at index
static void timer_queue_remove_at(CljPersistentVector *timer_vec, int index) {
    if (!timer_vec) return;
    // Use vector abstraction - vector_remove_at handles RELEASE automatically

        ASSIGN(g_runtime.timer_queue, vector_remove_at(timer_vec, index));
}

void event_loop_init(void) {
    // Initialize keywords
    KW_FN = intern_symbol_global(":fn");
    KW_RESULT_CHAN = intern_symbol_global(":result-chan");
    KW_SCHEDULED_SEC = intern_symbol_global(":scheduled-sec");
    KW_SCHEDULED_MSEC = intern_symbol_global(":scheduled-msec");
    KW_PERIODIC = intern_symbol_global(":periodic");
    KW_PERIOD_MS = intern_symbol_global(":period-ms");
    KW_TIMER_ID = intern_symbol_global(":timer-id");
    
    // Ensure task queue is initialized (idempotent)
    task_queue_get();
}

void event_loop_clear(void) {
    // Safety: check if task_queue is valid before accessing it
    // runtime_reset() sets task_queue to NULL, but event_loop_clear() is called after
    // So we need to check if it's already NULL or invalid
    if (!g_runtime.task_queue) return;
    
    // Safety: validate pointer before calling task_queue_get which calls TAG()
    if ((uintptr_t)g_runtime.task_queue < 0x1000) {
        g_runtime.task_queue = NULL; // Mark as invalid
        return;
    }
    
    // task_queue_get() calls TAG() which might crash if task_queue is invalid
    // So we validate the pointer first
    CljPersistentVector *task_vec = NULL;
    if (g_runtime.task_queue && (uintptr_t)g_runtime.task_queue >= 0x1000) {
        task_vec = task_queue_get();
    }
    if (task_vec) {
        // Clear vector count - elements will be freed when vector is released
        // For transient vectors, we just reset the count
        vector_clear(task_vec);
    }
    
    // Safety: check timer_queue before accessing it
    // Note: runtime_reset() now calls event_loop_clear() before setting timer_queue to NULL
    // So we can safely access it here
    if (!g_runtime.timer_queue) return; // Already cleared
    
    // Safety: validate pointer before calling timer_queue_get which calls TAG()
    if ((uintptr_t)g_runtime.timer_queue < 0x1000) {
        g_runtime.timer_queue = NULL; // Mark as invalid
        return;
    }
    
    CljPersistentVector *timer_vec = NULL;
    if (g_runtime.timer_queue && (uintptr_t)g_runtime.timer_queue >= 0x1000) {
        timer_vec = timer_queue_get();
    }
    if (timer_vec) {
        // Clear vector count - elements will be freed when vector is released
        // For transient vectors, we just reset the count
        vector_clear(timer_vec);
    }
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljMap *result_channel) {
    if (!fn_zero_arity) return;
    
    CljPersistentVector *task_vec = task_queue_get();
    if (!task_vec) return;
    
    CljMap *task_map = task_to_map(RETAIN(fn_zero_arity), RETAIN(result_channel));
    if (!task_map) {
        RELEASE(fn_zero_arity);
        RELEASE(result_channel);
        return;
    }
    
    // Mutate transient queues in-place (no return value).
    if (TAG(task_vec) == CLJ_VECTOR_TRANSIENT) {
        clj_conj(as_transient_vector((ID)task_vec), task_map);
    } else {
        // Fallback: if we ever end up with a persistent vector here, keep behavior correct.
        CljPersistentVector *new_vec = vector_conj(task_vec, task_map);
        if (!new_vec) {
            RELEASE(task_map);
            return;
        }
        if (new_vec != task_vec) {
            RELEASE(task_vec);
            g_runtime.task_queue = new_vec;
        }
    }
    RELEASE(task_map);
}


/**
 * @brief Execute the next enqueued go-block task
 * @param env Environment map (currently unused, may be NULL)
 * @param st Evaluation state for exception handling
 * @return true if a task was executed, false if queue is empty
 * 
 * This function processes one go-block task from the queue (FIFO order):
 * 1. Executes the zero-arity function with exception safety
 * 2. Puts the result (or NULL on error) into the result channel
 * 3. Closes the channel by setting :closed to true
 * 4. Releases all task resources
 */
bool event_loop_run_next(CljMap *env, EvalState *st) {
    (void)env;
    
    timer_process();
    
    CljPersistentVector *task_vec = task_queue_get();
    if (!task_vec) return false;
    unsigned int count = vector_count(task_vec);
    if (count == 0) {
        return false;
    }
    
    // Get first task (FIFO)
    ID *data = vector_as_array(task_vec);
    if (!data) return false;
    CljMap *task_map = (CljMap*)data[0];
    
    // Retain task_map before removing it from the queue (vector_remove_at will release it)
    RETAIN(task_map);
    
    // Remove from queue using vector_remove_at
    // vector_remove_at may return a new vector (COW), so we need to update g_runtime.task_queue
    CljPersistentVector *new_task_vec = vector_remove_at(task_vec, 0);
    if (new_task_vec != task_vec) {
        // vector_remove_at returned a new vector (COW)
        RELEASE(task_vec);
        g_runtime.task_queue = new_task_vec;
    }
    
    CljObject *fn;
    CljMap *result_chan;
    if (!task_from_map(task_map, &fn, &result_chan)) {
        RELEASE(task_map);
        return false;
    }
    
    // CRITICAL: Validate that fn is actually a function before calling eval_function_call
    // This prevents crashes when run-next-task is called recursively during go-block execution
    // The fn must be CLJ_FUNC (native function) or CLJ_CLOSURE (Clojure function)
    // 
    // NOTE: If fn is not a function, this indicates a serious bug in task creation/storage.
    // We should throw an exception rather than silently skipping, to help debug the issue.
    if (!fn || (TAG(fn) != CLJ_FUNC && TAG(fn) != CLJ_CLOSURE)) {
        // Invalid function object - this should never happen if tasks are created correctly
        // This could happen if:
        // 1. The task map is corrupted
        // 2. The function object was incorrectly stored in the task map
        // 3. Memory corruption
        // 
        // Throw exception to help debug, but also handle gracefully to prevent crash
        const char *fn_type = fn ? clj_type_name(TAG(fn)) : "NULL";
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
            "Invalid function object in task queue: expected CLJ_FUNC or CLJ_CLOSURE, got %s",
            fn_type);
        RELEASE(task_map);
        if (result_chan) {
            // Close channel without value (error case)
            result_channel_close(result_chan);
            RELEASE(result_chan);
        }
        RELEASE(fn);
        return false;
    }
    
    // Retain result_chan before releasing task_map, since it's part of the map
    RETAIN(result_chan);
    RELEASE(task_map);
    
    CljObject *result = NULL;
    bool ok = true;
    TRY {
        WITH_AUTORELEASE_POOL({
            result = eval_function_call(fn, NULL, 0, env, st);
        });
    } CATCH(ex) {
        ok = false;
    } END_TRY
    
    if (result_chan) {
        CljMap *chan = result_chan;
        CljObject *obj = (CljObject*)chan;
        
        CLJ_ASSERT(chan != NULL);
        CLJ_ASSERT(obj != NULL);
        CLJ_ASSERT(obj->type == CLJ_MAP_TRANSIENT || obj->type == CLJ_MAP);
        
        if (obj->type == CLJ_MAP) {
            CLJ_ASSERT(obj->rc == 1);
        }
        
#if defined(DEBUG)
        void *chan_ptr_before = (void*)chan;
#endif
        
        // Clojure-compatibility: nil is a valid value that can be sent through channels
        // result_channel_put can handle NULL/nil values, so we write the result even if it's nil
        if (ok) {
            result_channel_put(chan, result);
#if defined(DEBUG)
            CLJ_ASSERT((void*)chan == chan_ptr_before);
#endif
        }
        
        result_channel_close(chan);
#if defined(DEBUG)
        CLJ_ASSERT((void*)chan == chan_ptr_before);
        
        CljObject *kw_closed = (CljObject*)intern_symbol_global(":closed");
        CljValue closed_val = map_get_sentinel(chan, (CljValue)kw_closed, NULL);
        CLJ_ASSERT(closed_val != NULL);
        CLJ_ASSERT(is_special(closed_val));
        CLJ_ASSERT(as_special(closed_val) == SPECIAL_TRUE);
#endif
    } else {
        CLJ_ASSERT(0 && "event_loop_run_next: task.result_chan is NULL");
    }

    if (!IS_IMMEDIATE(result)) RELEASE(result);
    RELEASE(fn);
    RELEASE(result_chan);
    return true;
}

// Helper function to insert timer task map in sorted order (by scheduled time)
static void timer_insert_sorted_map(CljMap *task_map) {
    if (!task_map) return;
    
    int scheduled_sec = task_get_scheduled_sec(task_map);
    int scheduled_msec = task_get_scheduled_msec(task_map);
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return;
    
    int count = vector_count(timer_vec);
    int insert_pos = count;
    for (int i = 0; i < count; i++) {
        CljMap *existing_map = vector_nth(timer_vec, i);
        int existing_sec = task_get_scheduled_sec(existing_map);
        int existing_msec = task_get_scheduled_msec(existing_map);
        if (scheduled_sec < existing_sec || (scheduled_sec == existing_sec && scheduled_msec < existing_msec)) {
            insert_pos = i;
            RELEASE(existing_map);
            break;
        }
        RELEASE(existing_map);
    }
    
    // Use vector_insert_at to insert at the correct position
    // vector_insert_at handles capacity growth, element shifting, and RETAIN automatically

    ASSIGN(g_runtime.timer_queue, vector_insert_at(timer_vec, insert_pos, task_map));
}

// Enqueue a timer task
int timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms) {
    if (!fn_zero_arity) return 0;
    
    // Generate unique timer ID
    int timer_id = ++g_runtime.timer_id_counter;
    
    // If delay is 0 and not periodic, execute immediately
    if (delay_ms == 0 && !periodic) {
        CljMap *chan = make_result_channel();
        event_loop_enqueue(RETAIN(fn_zero_arity), chan);
        RELEASE(chan);
        RELEASE(fn_zero_arity);
        return timer_id;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int sec = (int)tv.tv_sec;
    int msec = (int)(tv.tv_usec / 1000);
    
    // Add delay_ms to current time
    int64_t total_msec = (int64_t)msec + delay_ms;
    int scheduled_sec = (int)(sec + total_msec / 1000);
    int scheduled_msec = (int)(total_msec % 1000);
    
    CljMap *task_map = task_timer_to_map(RETAIN(fn_zero_arity), scheduled_sec, scheduled_msec, periodic, (int)period_ms, timer_id);
    if (!task_map) {
        RELEASE(fn_zero_arity);
        return 0;
    }
    
    timer_insert_sorted_map(task_map);
    return timer_id;
}

// Process timer queue: move ready timers to normal queue
static void timer_process(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int now_sec = (int)tv.tv_sec;
    int now_msec = (int)(tv.tv_usec / 1000);
    
    while (true) {
        CljPersistentVector *timer_vec = timer_queue_get();
        if (!timer_vec || vector_count(timer_vec) == 0) break;
        
        CljMap *task_map = (CljMap*)vector_nth(timer_vec, 0);
        // Retain task_map because we'll release it later (it's removed from vector)
        RETAIN(task_map);
        
        int scheduled_sec = task_get_scheduled_sec(task_map);
        int scheduled_msec = task_get_scheduled_msec(task_map);
        ID fn = task_get_fn(task_map);
        if (!fn) {
            RELEASE(task_map);
            timer_queue_remove_at(timer_vec, 0);
            continue;
        }
        bool periodic = task_get_periodic(task_map);
        int period_ms = task_get_period_ms(task_map);
        int timer_id = task_get_timer_id(task_map);
        
        bool timer_ready = (scheduled_sec < now_sec) || 
                          (scheduled_sec == now_sec && scheduled_msec <= now_msec);
        if (!timer_ready) {
            RELEASE(task_map);
            break;  // Timers are sorted, so rest are also not ready
        }
        
        timer_queue_remove_at(timer_vec, 0);
        RETAIN(fn);  // Retain before releasing task_map
        RELEASE(task_map);
        
        CljMap *chan = make_result_channel();
        
        if (periodic) {
            RETAIN(fn);  // Extra retain for next period
        }
        
        event_loop_enqueue((CljObject*)fn, chan);
        RELEASE(chan);
        
        if (periodic) {
            int64_t total_msec = (int64_t)now_msec + period_ms;
            int next_sec = now_sec + (int)(total_msec / 1000);
            int next_msec = (int)(total_msec % 1000);
            CljMap *next_task_map = task_timer_to_map(fn, next_sec, next_msec, true, period_ms, timer_id);
            if (next_task_map) {
                timer_insert_sorted_map(next_task_map);
            }
            RELEASE(fn);  // Release extra retain (task_timer_to_map and event_loop_enqueue retained it)
        }
    }
}

// Cancel a timer by ID
bool timer_cancel(int timer_id) {
    if (timer_id <= 0) return false;
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return false;
    
    int count = vector_count(timer_vec);
    for (int i = 0; i < count; i++) {
        CljMap *task_map = vector_nth(timer_vec, i);
        if (!task_map) continue;
        
        int map_timer_id = task_get_timer_id(task_map);
        if (map_timer_id == timer_id) {
            // Found the timer - remove it (timer_queue_remove_at releases it automatically)
            timer_queue_remove_at(timer_vec, i);
            return true;
        }
    }
    
    return false;  // Timer not found
}

