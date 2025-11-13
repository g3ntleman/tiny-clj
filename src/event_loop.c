#include "event_loop.h"
#include "function_call.h"
#include "symbol.h"
#include "memory.h"
#include "exception.h"
#include "channel.h"
#include "types.h"  // For ZOMBIE_RC
#include "runtime.h"
#include "vector.h"
#include "vector_internal.h"  // For direct manipulation of timer queue vectors
#include "map.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>


// Task Map keys (DRY: shared keywords, cached once)
static struct {
    CljObject *kw_fn;
    CljObject *kw_result_chan;
    CljObject *kw_scheduled_sec;
    CljObject *kw_scheduled_msec;
    CljObject *kw_periodic;
    CljObject *kw_period_ms;
    CljObject *kw_timer_id;
    bool initialized;
} g_task_keywords = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, false};

static void task_init_keywords(void) {
    if (g_task_keywords.initialized) return;
    g_task_keywords.kw_fn = (CljObject*)intern_symbol(NULL, ":fn");
    g_task_keywords.kw_result_chan = (CljObject*)intern_symbol(NULL, ":result-chan");
    g_task_keywords.kw_scheduled_sec = (CljObject*)intern_symbol(NULL, ":scheduled-sec");
    g_task_keywords.kw_scheduled_msec = (CljObject*)intern_symbol(NULL, ":scheduled-msec");
    g_task_keywords.kw_periodic = (CljObject*)intern_symbol(NULL, ":periodic");
    g_task_keywords.kw_period_ms = (CljObject*)intern_symbol(NULL, ":period-ms");
    g_task_keywords.kw_timer_id = (CljObject*)intern_symbol(NULL, ":timer-id");
    g_task_keywords.initialized = true;
}

// Helper functions for normal Tasks as Maps
// Task Map keys: :fn, :result-chan
static CljMap* task_to_map(CljObject *fn, CljMap *result_chan) {
    CljMap *task_map = make_map(2);
    if (!task_map) return NULL;
    
    CljMap *tmap = map_transient(task_map);
    RELEASE(task_map);
    if (!tmap) return NULL;
    
    task_init_keywords();
    
    map_conj(tmap, (ID)g_task_keywords.kw_fn, (ID)fn);
    if (result_chan) {
        map_conj(tmap, (ID)g_task_keywords.kw_result_chan, (ID)result_chan);
    }
    
    return tmap;
}

static bool task_from_map(CljMap *task_map, CljObject **fn, CljMap **result_chan) {
    if (!task_map || TAG((ID)task_map) != CLJ_TRANSIENT_MAP) return false;
    
    task_init_keywords();
    
    ID fn_val = map_get(task_map, (ID)g_task_keywords.kw_fn);
    ID result_chan_val = map_get(task_map, (ID)g_task_keywords.kw_result_chan);
    
    if (!fn_val) return false;
    
    if (fn) *fn = (CljObject*)fn_val;
    if (result_chan) *result_chan = result_chan_val ? (CljMap*)result_chan_val : NULL;
    
    return true;
}

// Helper functions for Timer-Tasks as Maps
// Timer-Task Map keys: :fn, :scheduled-sec, :scheduled-msec, :periodic, :period-ms, :timer-id
static CljMap* timer_task_to_map(CljObject *fn, int32_t scheduled_sec, int32_t scheduled_msec, bool periodic, int32_t period_ms, int32_t timer_id) {
    task_init_keywords();
    
    return make_transient_map_from_kv(6,
        (ID)g_task_keywords.kw_fn, (ID)fn,
        (ID)g_task_keywords.kw_scheduled_sec, fixnum(scheduled_sec),
        (ID)g_task_keywords.kw_scheduled_msec, fixnum(scheduled_msec),
        (ID)g_task_keywords.kw_periodic, periodic ? clj_true : clj_false,
        (ID)g_task_keywords.kw_period_ms, fixnum(period_ms),
        (ID)g_task_keywords.kw_timer_id, fixnum(timer_id));
}

static bool timer_task_from_map(CljMap *task_map, CljObject **fn, int32_t *scheduled_sec, int32_t *scheduled_msec, bool *periodic, int32_t *period_ms, int32_t *timer_id) {
    if (!task_map || TAG((ID)task_map) != CLJ_TRANSIENT_MAP) return false;
    
    task_init_keywords();
    
    ID fn_val = map_get(task_map, (ID)g_task_keywords.kw_fn);
    ID scheduled_sec_val = map_get(task_map, (ID)g_task_keywords.kw_scheduled_sec);
    ID scheduled_msec_val = map_get(task_map, (ID)g_task_keywords.kw_scheduled_msec);
    ID periodic_val = map_get(task_map, (ID)g_task_keywords.kw_periodic);
    ID period_ms_val = map_get(task_map, (ID)g_task_keywords.kw_period_ms);
    ID timer_id_val = map_get(task_map, (ID)g_task_keywords.kw_timer_id);
    
    if (!fn_val || !scheduled_sec_val || !scheduled_msec_val || !periodic_val || !period_ms_val || !timer_id_val) {
        return false;
    }
    
    if (fn) *fn = (CljObject*)fn_val;
    if (scheduled_sec) *scheduled_sec = as_fixnum((CljValue)scheduled_sec_val);
    if (scheduled_msec) *scheduled_msec = as_fixnum((CljValue)scheduled_msec_val);
    if (periodic) *periodic = (periodic_val == (ID)clj_true);
    if (period_ms) *period_ms = as_fixnum((CljValue)period_ms_val);
    if (timer_id) *timer_id = as_fixnum((CljValue)timer_id_val);
    
    return true;
}

// Forward declarations
static void timer_process(void);
static void timer_insert_sorted_map(CljMap *task_map);

// Helper function to ensure task queue is initialized
static CljPersistentVector* task_queue_get(void) {
    if (!g_runtime.task_queue) {
        CljPersistentVector* task_vec = make_vector(8, false);
        if (task_vec) {
            g_runtime.task_queue = (CljPersistentVector*)transient((ID)task_vec);
            RELEASE(task_vec);
        }
    }
    if (!g_runtime.task_queue) return NULL;
    CljPersistentVector *task_vec = g_runtime.task_queue;
    if (TAG((ID)task_vec) != CLJ_TRANSIENT_VECTOR) return NULL;
    return task_vec;
}

// Helper function to ensure timer queue is initialized
static CljPersistentVector* timer_queue_get(void) {
    if (!g_runtime.timer_queue) {
        CljPersistentVector* timer_vec = make_vector(8, false);
        if (timer_vec) {
            g_runtime.timer_queue = (CljPersistentVector*)transient((ID)timer_vec);
            RELEASE(timer_vec);
        }
    }
    if (!g_runtime.timer_queue) return NULL;
    CljPersistentVector *timer_vec = g_runtime.timer_queue;
    if (TAG((ID)timer_vec) != CLJ_TRANSIENT_VECTOR) return NULL;
    return timer_vec;
}

// Helper function to remove element from timer queue at index
static void timer_queue_remove_at(CljPersistentVector *timer_vec, int index) {
    if (!timer_vec) return;
    // Use vector abstraction - vector_remove_at handles RELEASE automatically
    vector_remove_at(timer_vec, index);
}

void event_loop_init(void) {
    // Ensure task queue is initialized (idempotent)
    task_queue_get();
}

void event_loop_clear(void) {
    CljPersistentVector *task_vec = task_queue_get();
    if (task_vec) {
        VECTOR_FOR_EACH(task_vec, task_elem) {
            CljMap *task_map = (CljMap*)task_elem;
            CljObject *fn;
            CljMap *result_chan;
            if (task_from_map(task_map, &fn, &result_chan)) {
                RELEASE(fn);
                if (result_chan) RELEASE(result_chan);
            }
            RELEASE(task_map);
        }
        task_vec->count = 0;
    }
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (timer_vec) {
        int count = vector_count(timer_vec);
        for (int i = 0; i < count; i++) {
            CljMap *task_map = (CljMap*)vector_get_element_no_retain(timer_vec, i);
            if (task_map) {
                CljObject *fn;
                if (timer_task_from_map(task_map, &fn, NULL, NULL, NULL, NULL, NULL)) {
                    RELEASE(fn);
                }
                RELEASE(task_map);
            }
        }
        // Clear vector by removing all elements
        while (vector_count(timer_vec) > 0) {
            vector_remove_at(timer_vec, 0);
        }
    }
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljMap *result_channel) {
    if (!fn_zero_arity) return;
    
    CljPersistentVector *task_vec = task_queue_get();
    if (!task_vec) return;
    
    CljMap *task_map = task_to_map(RETAIN(fn_zero_arity), result_channel ? RETAIN(result_channel) : NULL);
    if (!task_map) {
        RELEASE(fn_zero_arity);
        if (result_channel) RELEASE(result_channel);
        return;
    }
    
    vector_grow_capacity(task_vec);
    task_vec->data[task_vec->count] = (CljObject*)RETAIN((ID)task_map);
    task_vec->count++;
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
    if (!task_vec || task_vec->count <= 0) return false;
    
    // Get first task (FIFO)
    CljMap *task_map = (CljMap*)task_vec->data[0];
    
    // Remove from queue
    for (int i = 1; i < task_vec->count; i++) {
        task_vec->data[i - 1] = task_vec->data[i];
    }
    task_vec->count--;
    
    CljObject *fn;
    CljMap *result_chan;
    if (!task_from_map(task_map, &fn, &result_chan)) {
        RELEASE(task_map);
        return false;
    }
    
    RELEASE(task_map);
    
    CljObject *result = NULL;
    bool ok = true;
    CljObjectPool *_pool = autorelease_pool_push();
    TRY {
        result = eval_function_call(fn, NULL, 0, env, st);
        autorelease_pool_pop(_pool);
    } CATCH(ex) {
        ok = false;
        autorelease_pool_pop(_pool);
    } END_TRY
    
    if (result_chan) {
        CljMap *chan = result_chan;
        CljObject *obj = (CljObject*)chan;
        
        CLJ_ASSERT(chan != NULL);
        CLJ_ASSERT(obj != NULL);
        CLJ_ASSERT(obj->type == CLJ_TRANSIENT_MAP || obj->type == CLJ_MAP);
        
        if (obj->type == CLJ_MAP) {
            CLJ_ASSERT(obj->rc == 1);
        }
        
        void *chan_ptr_before = (void*)chan;
        
        if (ok && result) {
            result_channel_put(chan, (ID)result);
            CLJ_ASSERT((void*)chan == chan_ptr_before);
        }
        
        result_channel_close(chan);
        CLJ_ASSERT((void*)chan == chan_ptr_before);
        
        CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
        CljValue closed_val = map_get(chan, (CljValue)kw_closed);
        CLJ_ASSERT(closed_val != NULL);
        CLJ_ASSERT(is_special(closed_val));
        CLJ_ASSERT(as_special(closed_val) == SPECIAL_TRUE);
    } else {
        CLJ_ASSERT(0 && "event_loop_run_next: task.result_chan is NULL");
    }

    if (!IS_IMMEDIATE(result)) RELEASE(result);
    RELEASE(fn);
    if (result_chan) RELEASE(result_chan);
    return true;
}

// Helper function to insert timer task map in sorted order (by scheduled time)
static void timer_insert_sorted_map(CljMap *task_map) {
    if (!task_map || TAG((ID)task_map) != CLJ_TRANSIENT_MAP) return;
    
    int32_t scheduled_sec, scheduled_msec;
    if (!timer_task_from_map(task_map, NULL, &scheduled_sec, &scheduled_msec, NULL, NULL, NULL)) {
        return;
    }
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return;
    
    int count = vector_count(timer_vec);
    int insert_pos = count;
    for (int i = 0; i < count; i++) {
        CljMap *existing_map = (CljMap*)vector_get_element_no_retain(timer_vec, i);
        int32_t existing_sec, existing_msec;
        if (timer_task_from_map(existing_map, NULL, &existing_sec, &existing_msec, NULL, NULL, NULL)) {
            if (scheduled_sec < existing_sec || (scheduled_sec == existing_sec && scheduled_msec < existing_msec)) {
                insert_pos = i;
                break;
            }
        }
    }
    
    // vector_grow_capacity checks capacity internally and only grows if needed
    vector_grow_capacity(timer_vec);
    
    // For inserting at a specific position, we need direct access
    // This is acceptable for transient vectors (RC=1) used as internal data structures
    // vector_internal.h is already included at the top of the file
    for (int i = count; i > insert_pos; i--) {
        timer_vec->data[i] = timer_vec->data[i - 1];
    }
    
    timer_vec->data[insert_pos] = (CljObject*)RETAIN((ID)task_map);
    timer_vec->count++;
}

// Enqueue a timer task
int32_t timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms) {
    if (!fn_zero_arity) return 0;
    
    // Generate unique timer ID
    int32_t timer_id = ++g_runtime.timer_id_counter;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int32_t sec = (int32_t)tv.tv_sec;
    int32_t msec = (int32_t)(tv.tv_usec / 1000);
    
    // Add delay_ms to current time
    int64_t total_msec = (int64_t)msec + delay_ms;
    int32_t scheduled_sec = (int32_t)(sec + total_msec / 1000);
    int32_t scheduled_msec = (int32_t)(total_msec % 1000);
    
    CljMap *task_map = timer_task_to_map(RETAIN(fn_zero_arity), scheduled_sec, scheduled_msec, periodic, (int32_t)period_ms, timer_id);
    if (!task_map) {
        RELEASE(fn_zero_arity);
        return 0;
    }
    
    timer_insert_sorted_map(task_map);
    return timer_id;
}

// Process timer queue: move ready timers to normal queue
static void timer_process(void) {
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int32_t now_sec = (int32_t)tv.tv_sec;
    int32_t now_msec = (int32_t)(tv.tv_usec / 1000);
    
    while (vector_count(timer_vec) > 0) {
        CljMap *task_map = (CljMap*)vector_get_element_no_retain(timer_vec, 0);
        
        int32_t scheduled_sec, scheduled_msec;
        CljObject *fn;
        bool periodic;
        int32_t period_ms;
        
        int32_t timer_id;
        if (!timer_task_from_map(task_map, &fn, &scheduled_sec, &scheduled_msec, &periodic, &period_ms, &timer_id)) {
            timer_queue_remove_at(timer_vec, 0);
            continue;
        }
        
        // Compare scheduled time with current time
        if (scheduled_sec > now_sec || (scheduled_sec == now_sec && scheduled_msec > now_msec)) {
            break;
        }
        
        timer_queue_remove_at(timer_vec, 0);
        CljMap *chan = make_result_channel();
        if (!chan) {
            RELEASE(fn);
            continue;
        }
        
        event_loop_enqueue(fn, chan);
        RELEASE(fn);
        RELEASE(chan);
        
        if (periodic) {
            // Calculate next scheduled time
            int64_t total_msec = (int64_t)now_msec + period_ms;
            int32_t next_sec = (int32_t)(now_sec + total_msec / 1000);
            int32_t next_msec = (int32_t)(total_msec % 1000);
            // Preserve timer ID for periodic timers
            CljMap *next_task_map = timer_task_to_map(RETAIN(fn), next_sec, next_msec, true, period_ms, timer_id);
            if (next_task_map) {
                timer_insert_sorted_map(next_task_map);
            }
        }
    }
}

// Cancel a timer by ID
bool timer_cancel(int32_t timer_id) {
    if (timer_id <= 0) return false;
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return false;
    
    int count = vector_count(timer_vec);
    for (int i = 0; i < count; i++) {
        CljMap *task_map = (CljMap*)vector_get_element_no_retain(timer_vec, i);
        if (!task_map) continue;
        
        int32_t map_timer_id;
        if (timer_task_from_map(task_map, NULL, NULL, NULL, NULL, NULL, &map_timer_id)) {
            if (map_timer_id == timer_id) {
                // Found the timer - remove it
                timer_queue_remove_at(timer_vec, i);
                return true;
            }
        }
    }
    
    return false;  // Timer not found
}

