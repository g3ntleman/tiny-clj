#include "event_loop.h"
#include "eval.h"
#include "symbol.h"
#include "memory.h"
#include "exception.h"
#include "channel.h"
#include "types.h"
#include "runtime.h"
#include "vector.h"
#include "map.h"
#include "value.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#if defined(ESP32_BUILD)
#include "gpio_esp32.h"
#endif

// Task Map keys (go-block tasks only)
static CljSymbol *KW_FN;
static CljSymbol *KW_RESULT_CHAN;

typedef struct NamedTimerEntry {
    ID key;
    int timer_id;
    struct NamedTimerEntry *next;
} NamedTimerEntry;

static NamedTimerEntry *g_named_timers = NULL;

// --- Timer Queue: plain C struct array (zero heap allocs per tick) ---

typedef struct {
    ID   fn;
    int  scheduled_sec;
    int  scheduled_msec;
    int  period_ms;
    int  timer_id;
    bool periodic;
} TimerEntry;

#define TIMER_QUEUE_CAP 32

static TimerEntry g_timer_queue[TIMER_QUEUE_CAP];
static int        g_timer_count = 0;

#define EVENT_LOOP_INGRESS_CAP 64
static ID g_event_loop_ingress_queue[EVENT_LOOP_INGRESS_CAP];
static uint16_t g_event_loop_ingress_head = 0u;
static uint16_t g_event_loop_ingress_count = 0u;
static atomic_flag g_event_loop_ingress_lock = ATOMIC_FLAG_INIT;

static inline void event_loop_ingress_lock_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&g_event_loop_ingress_lock, memory_order_acquire)) {
    }
}

static inline void event_loop_ingress_lock_release(void) {
    atomic_flag_clear_explicit(&g_event_loop_ingress_lock, memory_order_release);
}

static bool event_loop_ingress_push(ID fn_zero_arity) {
    if (!fn_zero_arity) return false;
    event_loop_ingress_lock_acquire();
    bool ok = false;
    if (g_event_loop_ingress_count < EVENT_LOOP_INGRESS_CAP) {
        uint16_t tail = (uint16_t)((g_event_loop_ingress_head + g_event_loop_ingress_count) % EVENT_LOOP_INGRESS_CAP);
        g_event_loop_ingress_queue[tail] = fn_zero_arity;
        g_event_loop_ingress_count++;
        ok = true;
    }
    event_loop_ingress_lock_release();
    return ok;
}

static ID event_loop_ingress_pop(void) {
    ID out = NULL;
    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_count > 0u) {
        out = g_event_loop_ingress_queue[g_event_loop_ingress_head];
        g_event_loop_ingress_queue[g_event_loop_ingress_head] = NULL;
        g_event_loop_ingress_head = (uint16_t)((g_event_loop_ingress_head + 1u) % EVENT_LOOP_INGRESS_CAP);
        g_event_loop_ingress_count--;
    }
    event_loop_ingress_lock_release();
    return out;
}

static void event_loop_ingress_drain(void) {
    while (true) {
        ID fn = event_loop_ingress_pop();
        if (!fn) {
            return;
        }
        event_loop_enqueue(fn, NULL);
    }
}

// Go-block task map helpers (tasks WITH result channel)
static CljPersistentMap* task_to_map(CljObject *fn, CljTransientMap *result_chan) {
    CljPersistentMap *task_map = make_map(2, STRONG);
    if (!task_map) return NULL;
    
    CljTransientMap *tmap = map_transient(task_map);
    RELEASE(task_map);
    if (!tmap) return NULL;
    
    map_conj(tmap, KW_FN, fn);
    if (result_chan) {
        map_conj(tmap, KW_RESULT_CHAN, result_chan);
    }
    
    CljPersistentMap *pmap = map_persistent(tmap);
    RETAIN(pmap);
    RELEASE(tmap);
    return pmap;
}

static bool task_from_map(CljPersistentMap *task_map, CljObject **fn, CljTransientMap **result_chan) {
    if (!task_map) return false;
    
    ID fn_val = map_get_sentinel(task_map, KW_FN, NULL);
    ID result_chan_val = map_get_sentinel(task_map, KW_RESULT_CHAN, NULL);
    
    if (!fn_val) return false;
    
    if (fn) *fn = fn_val;
    if (result_chan) *result_chan = result_chan_val ? result_chan_val : NULL;
    
    return true;
}

// --- Timer queue array operations ---

static void timer_remove_at(int index) {
    if (index < 0 || index >= g_timer_count) return;
    RELEASE(g_timer_queue[index].fn);
    g_timer_count--;
    if (index < g_timer_count) {
        memmove(&g_timer_queue[index], &g_timer_queue[index + 1],
                (size_t)(g_timer_count - index) * sizeof(TimerEntry));
    }
}

static bool timer_insert_sorted(TimerEntry entry) {
    if (g_timer_count >= TIMER_QUEUE_CAP) return false;
    int pos = g_timer_count;
    for (int i = 0; i < g_timer_count; i++) {
        if (entry.scheduled_sec < g_timer_queue[i].scheduled_sec ||
            (entry.scheduled_sec == g_timer_queue[i].scheduled_sec &&
             entry.scheduled_msec < g_timer_queue[i].scheduled_msec)) {
            pos = i;
            break;
        }
    }
    if (pos < g_timer_count) {
        memmove(&g_timer_queue[pos + 1], &g_timer_queue[pos],
                (size_t)(g_timer_count - pos) * sizeof(TimerEntry));
    }
    g_timer_queue[pos] = entry;
    g_timer_count++;
    return true;
}

// Forward declarations
static void timer_process(void);
static bool timer_named_remove_by_id(int timer_id);

static bool timer_key_is_valid(ID key) {
    return key != NULL;
}

static NamedTimerEntry *timer_named_find_by_key(ID key) {
    if (!timer_key_is_valid(key)) return NULL;
    NamedTimerEntry *cur = g_named_timers;
    while (cur) {
        if (clj_equal(cur->key, key)) return cur;
        cur = cur->next;
    }
    return NULL;
}

static bool timer_named_set(ID key, int timer_id) {
    if (!timer_key_is_valid(key) || timer_id <= 0) return false;
    NamedTimerEntry *existing = timer_named_find_by_key(key);
    if (existing) {
        existing->timer_id = timer_id;
        return true;
    }
    NamedTimerEntry *entry = CLJ_MALLOC(sizeof(NamedTimerEntry));
    if (!entry) return false;
    entry->key = RETAIN(key);
    entry->timer_id = timer_id;
    entry->next = g_named_timers;
    g_named_timers = entry;
    return true;
}

static bool timer_named_take_by_key(ID key, int *out_timer_id) {
    if (out_timer_id) *out_timer_id = 0;
    if (!timer_key_is_valid(key)) return false;
    NamedTimerEntry *prev = NULL;
    NamedTimerEntry *cur = g_named_timers;
    while (cur) {
        if (clj_equal(cur->key, key)) {
            if (out_timer_id) *out_timer_id = cur->timer_id;
            if (prev) prev->next = cur->next;
            else g_named_timers = cur->next;
            RELEASE(cur->key);
            CLJ_FREE(cur);
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

static bool timer_named_remove_by_id(int timer_id) {
    if (timer_id <= 0) return false;
    NamedTimerEntry *prev = NULL;
    NamedTimerEntry *cur = g_named_timers;
    while (cur) {
        if (cur->timer_id == timer_id) {
            if (prev) prev->next = cur->next;
            else g_named_timers = cur->next;
            RELEASE(cur->key);
            CLJ_FREE(cur);
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

static void timer_named_clear_all(void) {
    NamedTimerEntry *cur = g_named_timers;
    while (cur) {
        NamedTimerEntry *next = cur->next;
        RELEASE(cur->key);
        CLJ_FREE(cur);
        cur = next;
    }
    g_named_timers = NULL;
}

static bool timer_schedule_with_id(CljObject *fn_zero_arity,
                                   int64_t delay_ms,
                                   bool periodic,
                                   int64_t period_ms,
                                   int timer_id) {
    if (!fn_zero_arity || timer_id <= 0) return false;
    if (delay_ms < 0) return false;
    if (periodic && period_ms <= 0) return false;

    if (delay_ms == 0 && !periodic) {
        event_loop_enqueue(RETAIN(fn_zero_arity), NULL);
        return true;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t total_msec = (int64_t)(tv.tv_usec / 1000) + delay_ms;

    TimerEntry entry = {
        .fn            = RETAIN(fn_zero_arity),
        .scheduled_sec = (int)(tv.tv_sec + total_msec / 1000),
        .scheduled_msec= (int)(total_msec % 1000),
        .periodic      = periodic,
        .period_ms     = (int)period_ms,
        .timer_id      = timer_id
    };
    return timer_insert_sorted(entry);
}

// Helper function to ensure task queue is initialized
static CljTransientVector* task_queue_get(void) {
    if (!g_runtime.task_queue) {
        CljPersistentVector* task_vec = make_vector(8, STRONG);
        if (task_vec) {
            g_runtime.task_queue = vector_transient(task_vec);
            RELEASE(task_vec);
        }
    }
    if (!g_runtime.task_queue) return NULL;
    CljTransientVector *task_vec = g_runtime.task_queue;
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

void event_loop_init(void) {
    KW_FN = intern_symbol_global(":fn");
    KW_RESULT_CHAN = intern_symbol_global(":result-chan");
    task_queue_get();
}

void event_loop_clear(void) {
    if (g_runtime.task_queue && (uintptr_t)g_runtime.task_queue >= 0x1000) {
        CljTransientVector *task_vec = task_queue_get();
        if (task_vec && task_vec->backing) {
            vector_clear(task_vec->backing);
        }
    } else {
        g_runtime.task_queue = NULL;
    }

    for (int i = 0; i < g_timer_count; i++) {
        RELEASE(g_timer_queue[i].fn);
    }
    g_timer_count = 0;
    timer_named_clear_all();

    while (true) {
        ID fn = event_loop_ingress_pop();
        if (!fn) {
            break;
        }
        RELEASE(fn);
    }
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljTransientMap *result_channel) {
    if (!fn_zero_arity) return;
    
    CljTransientVector *task_vec = task_queue_get();
    if (!task_vec) return;
    
    if (!result_channel) {
        vector_push(task_vec, fn_zero_arity);
        return;
    }
    
    CljPersistentMap *task_map = task_to_map(RETAIN(fn_zero_arity), RETAIN(result_channel));
    if (!task_map) {
        RELEASE(fn_zero_arity);
        RELEASE(result_channel);
        return;
    }
    
    vector_push(task_vec, task_map);
    RELEASE(task_map);
}

bool event_loop_enqueue_ingress(CljObject *fn_zero_arity) {
    if (!fn_zero_arity) return false;
    ID retained = RETAIN(fn_zero_arity);
    if (event_loop_ingress_push(retained)) {
        return true;
    }
    RELEASE(retained);
    return false;
}

bool event_loop_ingress_has_pending(void) {
    event_loop_ingress_lock_acquire();
    bool pending = g_event_loop_ingress_count > 0u;
    event_loop_ingress_lock_release();
    return pending;
}

bool event_loop_has_pending_tasks(void) {
    if (event_loop_ingress_has_pending()) return true;
    CljTransientVector *task_vec = task_queue_get();
    if (!task_vec || !task_vec->backing) return false;
    return vector_count(task_vec->backing) > 0;
}

int event_loop_time_until_next_timer_ms(void) {
    if (g_timer_count == 0) return -1;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t delta_ms = ((int64_t)g_timer_queue[0].scheduled_sec - (int64_t)tv.tv_sec) * 1000ll
                     + ((int64_t)g_timer_queue[0].scheduled_msec - (int64_t)(tv.tv_usec / 1000));
    if (delta_ms <= 0) return 0;
    if (delta_ms > INT_MAX) return INT_MAX;
    return (int)delta_ms;
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
bool event_loop_run_next(CljPersistentMap *env, EvalState *st) {
    (void)env;

#if defined(ESP32_BUILD)
    // Promote ISR-raised drain requests into regular event-loop tasks.
    gpio_esp32_poll_drain();
#endif

    // Consume cross-thread callback ingress before timer/task processing.
    event_loop_ingress_drain();

    timer_process();
    
    CljTransientVector *task_vec = task_queue_get();
    if (!task_vec) return false;
    unsigned int count = task_vec->backing ? vector_count(task_vec->backing) : 0;
    if (count == 0) {
        return false;
    }
    
    // Get first task (FIFO)
    if (vector_count(task_vec->backing) == 0) return false;
    ID entry = vector_nth(task_vec->backing, 0);
    RETAIN(entry);
    vector_remove_at(task_vec, 0);

    CljObject *fn = NULL;
    CljTransientMap *result_chan = NULL;

    if (TAG(entry) == CLJ_MAP_PERSISTENT) {
        CljPersistentMap *task_map = entry;
        if (!task_from_map(task_map, &fn, &result_chan)) {
            RELEASE(task_map);
            return false;
        }
        RETAIN(fn);
        RETAIN(result_chan);
        RELEASE(task_map);
    } else {
        fn = entry;
    }

    if (!fn || (TAG(fn) != CLJ_FUNC && TAG(fn) != CLJ_CLOSURE)) {
        const char *fn_type = fn ? clj_type_name(TAG(fn)) : "NULL";
        throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
            "Invalid function object in task queue: expected CLJ_FUNC or CLJ_CLOSURE, got %s",
            fn_type);
        if (result_chan) {
            result_channel_close(result_chan);
            RELEASE(result_chan);
        }
        RELEASE(fn);
        return false;
    }
    
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
        if (ok) {
            result_channel_put(result_chan, result);
        }
        result_channel_close(result_chan);
        RELEASE(result_chan);
    }

    if (!IS_IMMEDIATE(result)) RELEASE(result);
    RELEASE(fn);
    return true;
}

// Enqueue a timer task
int timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms) {
    if (!fn_zero_arity) return 0;

    int timer_id = ++g_runtime.timer_id_counter;
    bool ok = timer_schedule_with_id(fn_zero_arity, delay_ms, periodic, period_ms, timer_id);
    RELEASE(fn_zero_arity);
    return ok ? timer_id : 0;
}

int timer_upsert_named(ID key,
                       CljObject *fn_zero_arity,
                       int64_t delay_ms,
                       bool periodic,
                       int64_t period_ms) {
    if (!timer_key_is_valid(key) || !fn_zero_arity) return 0;

    NamedTimerEntry *existing = timer_named_find_by_key(key);
    int timer_id = existing ? existing->timer_id : (++g_runtime.timer_id_counter);
    if (existing) {
        (void)timer_cancel(timer_id);
    }

    bool ok = timer_schedule_with_id(fn_zero_arity, delay_ms, periodic, period_ms, timer_id);
    RELEASE(fn_zero_arity);
    if (!ok) {
        (void)timer_named_take_by_key(key, NULL);
        return 0;
    }

    if (delay_ms == 0 && !periodic) {
        (void)timer_named_take_by_key(key, NULL);
        return timer_id;
    }

    if (!timer_named_set(key, timer_id)) {
        (void)timer_cancel(timer_id);
        return 0;
    }

    return timer_id;
}

bool timer_cancel_named(ID key) {
    int timer_id = 0;
    if (!timer_named_take_by_key(key, &timer_id)) return false;
    return timer_cancel(timer_id);
}

static void timer_process(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int now_sec = (int)tv.tv_sec;
    int now_msec = (int)(tv.tv_usec / 1000);

    while (g_timer_count > 0) {
        TimerEntry *t = &g_timer_queue[0];
        if (t->scheduled_sec > now_sec ||
            (t->scheduled_sec == now_sec && t->scheduled_msec > now_msec))
            break;

        ID fn        = t->fn;
        bool periodic = t->periodic;
        int period_ms = t->period_ms;
        int timer_id  = t->timer_id;

        RETAIN(fn);

        // Drop timer-queue ownership for the head entry while keeping our
        // local retained reference alive.
        timer_remove_at(0);

        if (!periodic) {
            (void)timer_named_remove_by_id(timer_id);
        }

        event_loop_enqueue(fn, NULL);

        if (periodic) {
            int64_t total_msec = (int64_t)now_msec + period_ms;
            TimerEntry next = {
                .fn             = RETAIN(fn),
                .scheduled_sec  = now_sec + (int)(total_msec / 1000),
                .scheduled_msec = (int)(total_msec % 1000),
                .periodic       = true,
                .period_ms      = period_ms,
                .timer_id       = timer_id
            };
            timer_insert_sorted(next);
        }
        RELEASE(fn);
    }
}

bool timer_cancel(int timer_id) {
    if (timer_id <= 0) return false;
    for (int i = 0; i < g_timer_count; i++) {
        if (g_timer_queue[i].timer_id == timer_id) {
            timer_remove_at(i);
            (void)timer_named_remove_by_id(timer_id);
            return true;
        }
    }
    return false;
}
