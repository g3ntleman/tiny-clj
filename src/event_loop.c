#include "event_loop.h"
#include "function_call.h"
#include "symbol.h"
#include "memory.h"
#include "exception.h"
#include "channel.h"
#include "types.h"  // For ZOMBIE_RC
#include "runtime.h"
#include "vector.h"
#include "map.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

typedef struct GoTask {
    CljObject *fn;            // zero-arity function to execute
    CljMap *result_chan;      // result channel (transient map) to put value and close
    int64_t scheduled_time;   // milliseconds since Epoch (-1 for normal tasks)
    bool periodic;            // true for periodic timers
    int64_t period_ms;        // period duration in milliseconds (only for periodic timers)
} GoTask;

static GoTask *g_tasks = NULL;
static int g_task_count = 0;
static int g_task_capacity = 0;

// Helper function to get current time in milliseconds since Epoch
static int64_t current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}

// Helper functions for int64_t ↔ int32_t (high/low) conversion
static inline int32_t int64_high(int64_t value) {
    return (int32_t)(value >> 32);
}

static inline int32_t int64_low(int64_t value) {
    return (int32_t)(value & 0xFFFFFFFFLL);
}

static inline int64_t int64_from_parts(int32_t high, int32_t low) {
    return ((int64_t)high << 32) | ((int64_t)low & 0xFFFFFFFFLL);
}

// Timer-Task Map keys (DRY: shared keywords, cached once)
static struct {
    CljObject *kw_fn;
    CljObject *kw_scheduled_high;
    CljObject *kw_scheduled_low;
    CljObject *kw_periodic;
    CljObject *kw_period_high;
    CljObject *kw_period_low;
    bool initialized;
} g_timer_keywords = {NULL, NULL, NULL, NULL, NULL, NULL, false};

static void timer_task_init_keywords(void) {
    if (g_timer_keywords.initialized) return;
    g_timer_keywords.kw_fn = (CljObject*)intern_symbol(NULL, ":fn");
    g_timer_keywords.kw_scheduled_high = (CljObject*)intern_symbol(NULL, ":scheduled-time-high");
    g_timer_keywords.kw_scheduled_low = (CljObject*)intern_symbol(NULL, ":scheduled-time-low");
    g_timer_keywords.kw_periodic = (CljObject*)intern_symbol(NULL, ":periodic");
    g_timer_keywords.kw_period_high = (CljObject*)intern_symbol(NULL, ":period-ms-high");
    g_timer_keywords.kw_period_low = (CljObject*)intern_symbol(NULL, ":period-ms-low");
    g_timer_keywords.initialized = true;
}

// Helper functions for Timer-Tasks as Maps
// Timer-Task Map keys: :fn, :scheduled-time-high, :scheduled-time-low, :periodic, :period-ms-high, :period-ms-low
static CljMap* timer_task_to_map(CljObject *fn, int64_t scheduled_time, bool periodic, int64_t period_ms) {
    CljMap *task_map = make_map(6);
    if (!task_map) return NULL;
    
    CljMap *tmap = map_transient(task_map);
    RELEASE(task_map);
    if (!tmap) return NULL;
    
    timer_task_init_keywords();
    
    map_conj(tmap, (ID)g_timer_keywords.kw_fn, (ID)fn);
    map_conj(tmap, (ID)g_timer_keywords.kw_scheduled_high, fixnum(int64_high(scheduled_time)));
    map_conj(tmap, (ID)g_timer_keywords.kw_scheduled_low, fixnum(int64_low(scheduled_time)));
    map_conj(tmap, (ID)g_timer_keywords.kw_periodic, periodic ? clj_true : clj_false);
    map_conj(tmap, (ID)g_timer_keywords.kw_period_high, fixnum(int64_high(period_ms)));
    map_conj(tmap, (ID)g_timer_keywords.kw_period_low, fixnum(int64_low(period_ms)));
    
    return tmap;
}

static bool timer_task_from_map(CljMap *task_map, CljObject **fn, int64_t *scheduled_time, bool *periodic, int64_t *period_ms) {
    if (!task_map || TAG((ID)task_map) != CLJ_TRANSIENT_MAP) return false;
    
    timer_task_init_keywords();
    
    ID fn_val = map_get(task_map, (ID)g_timer_keywords.kw_fn);
    ID scheduled_high_val = map_get(task_map, (ID)g_timer_keywords.kw_scheduled_high);
    ID scheduled_low_val = map_get(task_map, (ID)g_timer_keywords.kw_scheduled_low);
    ID periodic_val = map_get(task_map, (ID)g_timer_keywords.kw_periodic);
    ID period_high_val = map_get(task_map, (ID)g_timer_keywords.kw_period_high);
    ID period_low_val = map_get(task_map, (ID)g_timer_keywords.kw_period_low);
    
    if (!fn_val || !scheduled_high_val || !scheduled_low_val || !periodic_val || !period_high_val || !period_low_val) {
        return false;
    }
    
    if (fn) *fn = (CljObject*)fn_val;
    
    if (scheduled_time) {
        int32_t high = as_fixnum((CljValue)scheduled_high_val);
        int32_t low = as_fixnum((CljValue)scheduled_low_val);
        *scheduled_time = int64_from_parts(high, low);
    }
    
    if (periodic) {
        *periodic = (periodic_val == (ID)clj_true);
    }
    
    if (period_ms) {
        int32_t high = as_fixnum((CljValue)period_high_val);
        int32_t low = as_fixnum((CljValue)period_low_val);
        *period_ms = int64_from_parts(high, low);
    }
    
    return true;
}

// Forward declarations
static void timer_process(void);
static void timer_insert_sorted_map(CljMap *task_map);

// Helper function to ensure timer queue is initialized
static CljPersistentVector* timer_queue_get(void) {
    if (!g_runtime.timer_queue) {
        CljVector timer_vec = make_vector(8, false);
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
    if (!timer_vec || index < 0 || index >= timer_vec->count) return;
    CljMap *task_map = (CljMap*)timer_vec->data[index];
    RELEASE(task_map);
    for (int i = index + 1; i < timer_vec->count; i++) {
        timer_vec->data[i - 1] = timer_vec->data[i];
    }
    timer_vec->count--;
}

void event_loop_init(void) {
    if (g_tasks == NULL) {
        g_task_capacity = 8;
        g_tasks = (GoTask*)malloc(sizeof(GoTask) * (size_t)g_task_capacity);
        g_task_count = 0;
    }
}

void event_loop_clear(void) {
    for (int i = 0; i < g_task_count; i++) {
#ifdef DEBUG
        if (g_tasks[i].fn && g_tasks[i].fn->rc != ZOMBIE_RC) {
            RELEASE(g_tasks[i].fn);
        }
        if (g_tasks[i].result_chan) {
            CljObject *chan_obj = (CljObject*)g_tasks[i].result_chan;
            if (chan_obj->rc != ZOMBIE_RC) {
                RELEASE(g_tasks[i].result_chan);
            }
        }
#else
        RELEASE(g_tasks[i].fn);
        RELEASE(g_tasks[i].result_chan);
#endif
    }
    g_task_count = 0;
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (timer_vec) {
        for (int i = 0; i < timer_vec->count; i++) {
            CljMap *task_map = (CljMap*)timer_vec->data[i];
            CljObject *fn;
            if (timer_task_from_map(task_map, &fn, NULL, NULL, NULL)) {
                RELEASE(fn);
            }
            RELEASE(task_map);
        }
        timer_vec->count = 0;
    }
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljMap *result_channel) {
    event_loop_init();
    if (!fn_zero_arity) return;
    if (g_task_count >= g_task_capacity) {
        int newcap = g_task_capacity * 2;
        void *newmem = realloc(g_tasks, sizeof(GoTask) * (size_t)newcap);
        if (!newmem) return;
        g_tasks = (GoTask*)newmem;
        g_task_capacity = newcap;
    }
    g_tasks[g_task_count].fn = RETAIN(fn_zero_arity);
    g_tasks[g_task_count].result_chan = result_channel ? RETAIN(result_channel) : NULL;
    g_tasks[g_task_count].scheduled_time = -1;
    g_tasks[g_task_count].periodic = false;
    g_tasks[g_task_count].period_ms = 0;
    g_task_count++;
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
    
    if (g_task_count <= 0) return false;
    
    GoTask task = g_tasks[0];
    for (int i = 1; i < g_task_count; ++i) g_tasks[i - 1] = g_tasks[i];
    g_task_count--;

    CljObject *result = NULL;
    bool ok = true;
    CljObjectPool *_pool = autorelease_pool_push();
    TRY {
        result = eval_function_call(task.fn, NULL, 0, env, st);
        autorelease_pool_pop(_pool);
    } CATCH(ex) {
        ok = false;
        autorelease_pool_pop(_pool);
    } END_TRY
    
    if (task.result_chan) {
        CljMap *chan = task.result_chan;
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
    RELEASE(task.fn);
    RELEASE(task.result_chan);
    return true;
}

// Helper function to insert timer task map in sorted order (by scheduled_time)
static void timer_insert_sorted_map(CljMap *task_map) {
    if (!task_map || TAG((ID)task_map) != CLJ_TRANSIENT_MAP) return;
    
    int64_t scheduled_time;
    if (!timer_task_from_map(task_map, NULL, &scheduled_time, NULL, NULL)) {
        return;
    }
    
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return;
    
    int insert_pos = timer_vec->count;
    for (int i = 0; i < timer_vec->count; i++) {
        CljMap *existing_map = (CljMap*)timer_vec->data[i];
        int64_t existing_time;
        if (timer_task_from_map(existing_map, NULL, &existing_time, NULL, NULL)) {
            if (scheduled_time < existing_time) {
                insert_pos = i;
                break;
            }
        }
    }
    
    if (timer_vec->count >= timer_vec->capacity) {
        vector_grow_capacity(timer_vec);
    }
    
    for (int i = timer_vec->count; i > insert_pos; i--) {
        timer_vec->data[i] = timer_vec->data[i - 1];
    }
    
    timer_vec->data[insert_pos] = (CljObject*)RETAIN((ID)task_map);
    timer_vec->count++;
}

// Enqueue a timer task
void timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms) {
    if (!fn_zero_arity) return;
    
    int64_t scheduled_time = current_time_ms() + delay_ms;
    
    CljMap *task_map = timer_task_to_map(RETAIN(fn_zero_arity), scheduled_time, periodic, period_ms);
    if (!task_map) {
        RELEASE(fn_zero_arity);
        return;
    }
    
    timer_insert_sorted_map(task_map);
}

// Process timer queue: move ready timers to normal queue
static void timer_process(void) {
    CljPersistentVector *timer_vec = timer_queue_get();
    if (!timer_vec) return;
    
    int64_t now = current_time_ms();
    
    while (timer_vec->count > 0) {
        CljMap *task_map = (CljMap*)timer_vec->data[0];
        
        int64_t scheduled_time;
        CljObject *fn;
        bool periodic;
        int64_t period_ms;
        
        if (!timer_task_from_map(task_map, &fn, &scheduled_time, &periodic, &period_ms)) {
            timer_queue_remove_at(timer_vec, 0);
            continue;
        }
        
        if (scheduled_time > now) break;
        
        timer_queue_remove_at(timer_vec, 0);
        CljMap *chan = make_result_channel();
        if (!chan) {
            RELEASE(fn);
            continue;
        }
        
        event_loop_init();
        if (g_task_count >= g_task_capacity) {
            int newcap = g_task_capacity * 2;
            void *newmem = realloc(g_tasks, sizeof(GoTask) * (size_t)newcap);
            if (!newmem) {
                RELEASE(fn);
                RELEASE(chan);
                continue;
            }
            g_tasks = (GoTask*)newmem;
            g_task_capacity = newcap;
        }
        
        g_tasks[g_task_count].fn = fn;
        g_tasks[g_task_count].result_chan = chan;
        g_tasks[g_task_count].scheduled_time = -1;
        g_tasks[g_task_count].periodic = false;
        g_tasks[g_task_count].period_ms = 0;
        g_task_count++;
        
        if (periodic) {
            int64_t next_time = now + period_ms;
            CljMap *next_task_map = timer_task_to_map(RETAIN(fn), next_time, true, period_ms);
            if (next_task_map) {
                timer_insert_sorted_map(next_task_map);
            }
        }
    }
}


