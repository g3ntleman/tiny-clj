#include "event_loop.h"
#include "eval.h"
#include "symbol.h"
#include "callbacks.h"
#include "memory.h"
#include "exception.h"
#include "channel.h"
#include "types.h"
#include "runtime.h"
#include "vector.h"
#include "map.h"
#include "strings.h"
#include "record.h"
#include "value.h"
#include "gpio.h"
#include "viewer_collision_bridge.h"
#include "mini_format.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>

// Task Map keys (go-block tasks only)
static CljSymbol *KW_FN;
static CljSymbol *KW_RESULT_CHAN;
static CljSymbol *KW_ARG;
static CljSymbol *KW_HAS_ARG;
static CljSymbol *KW_EVENT_KIND;
static CljSymbol *KW_EVENT_KEY;

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
/*
 * Drain budget from ingress -> regular task queue per event-loop tick.
 * Keep this bounded so collision bursts cannot bypass ingress backpressure
 * by dumping unbounded work into the regular task queue in one tick.
 */
#define EVENT_LOOP_INGRESS_DRAIN_BUDGET 1u
static ID g_event_loop_ingress_queue[EVENT_LOOP_INGRESS_CAP];
static uint16_t g_event_loop_ingress_head = 0u;
static uint16_t g_event_loop_ingress_count = 0u;
static bool g_event_loop_ingress_closed = false;
static uint32_t g_event_loop_ingress_accepted_count = 0u;
static uint32_t g_event_loop_ingress_rejected_count = 0u;
static uint32_t g_event_loop_ingress_drained_count = 0u;
static uint32_t g_event_loop_ingress_high_watermark = 0u;
static atomic_flag g_event_loop_ingress_lock = ATOMIC_FLAG_INIT;
static uint64_t g_runloop_last_warn_ns = 0u;

#define RUNLOOP_BLOCK_WARN_THRESHOLD_NS 1000000000ull

static inline void event_loop_mini_fprintf(FILE *stream, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    (void)mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stream ? stream : stderr);
}

static uint64_t event_loop_monotonic_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static void event_loop_warn_if_slow_tick(uint64_t elapsed_ns, uint64_t end_ns) {
    if (elapsed_ns < RUNLOOP_BLOCK_WARN_THRESHOLD_NS || end_ns == 0u) {
        return;
    }
    bool should_warn = (g_runloop_last_warn_ns == 0u) ||
                       ((end_ns - g_runloop_last_warn_ns) >= RUNLOOP_BLOCK_WARN_THRESHOLD_NS);
    if (!should_warn) {
        return;
    }
    unsigned long elapsed_ms = (unsigned long)(elapsed_ns / 1000000ull);
    event_loop_mini_fprintf(stderr,
                            "[runloop] warning: runloop tick took %lums (threshold: 1000ms)\n",
                            elapsed_ms);
    g_runloop_last_warn_ns = end_ns;
}

static inline void event_loop_ingress_lock_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&g_event_loop_ingress_lock, memory_order_acquire)) {
    }
}

static inline void event_loop_ingress_lock_release(void) {
    atomic_flag_clear_explicit(&g_event_loop_ingress_lock, memory_order_release);
}

static inline bool event_loop_value_equals(ID a, ID b) {
    return (a == b) || (a && b && clj_equal(a, b));
}

static ID event_loop_value_get_sentinel(ID value, ID key, ID not_found) {
    if (!value || !key) return not_found;
    if (is_map(value)) {
        return map_get_sentinel(value, key, not_found);
    }
    if (TAG(value) == CLJ_RECORD) {
        return record_get_sentinel(value, key, not_found);
    }
    return not_found;
}

static bool event_loop_extract_event_kind_and_key(ID payload, ID *out_kind, ID *out_key) {
    if (out_kind) *out_kind = NULL;
    if (out_key) *out_key = NULL;
    if (!payload || !KW_EVENT_KIND || !KW_EVENT_KEY) {
        return false;
    }
    ID kind = event_loop_value_get_sentinel(payload, KW_EVENT_KIND, NOT_FOUND);
    ID key = event_loop_value_get_sentinel(payload, KW_EVENT_KEY, NOT_FOUND);
    if (kind == NOT_FOUND || key == NOT_FOUND || !kind || !key) {
        return false;
    }
    if (out_kind) *out_kind = kind;
    if (out_key) *out_key = key;
    return true;
}

#ifdef DEBUG
static const char *event_loop_debug_value_cstr(ID value, CljString **owned_string) {
    if (owned_string) {
        *owned_string = NULL;
    }
    if (!value) {
        return "nil";
    }
    CljString *rendered = clj_to_string(value);
    if (!rendered) {
        return "<to_string failed>";
    }
    // clj_to_string returns an autoreleased string in tiny-clj integration.
    // Do not RELEASE here; just keep pointer valid for immediate logging.
    (void)owned_string;
    return string_data((ID)rendered);
}

static void event_loop_debug_log_coalesced_ingress_event(ID fn_one_arity,
                                                         ID payload,
                                                         ID kind,
                                                         ID key) {
    const char *fn_cstr = event_loop_debug_value_cstr(fn_one_arity, NULL);
    const char *kind_cstr = event_loop_debug_value_cstr(kind, NULL);
    const char *key_cstr = event_loop_debug_value_cstr(key, NULL);
    const char *payload_cstr = event_loop_debug_value_cstr(payload, NULL);

    event_loop_mini_fprintf(stderr,
                            "[runloop][ingress][coalesce] fn=%s kind=%s key=%s payload=%s\n",
                            fn_cstr,
                            kind_cstr,
                            key_cstr,
                            payload_cstr);
}
#endif

static bool event_loop_ingress_entry_matches_kind_key(ID entry,
                                                      ID fn_one_arity,
                                                      ID kind,
                                                      ID key) {
    if (!entry || !fn_one_arity || !kind || !key || TAG(entry) != CLJ_MAP_PERSISTENT) {
        return false;
    }
    ID queued_fn = map_get_sentinel(entry, KW_FN, NULL);
    if (!event_loop_value_equals(queued_fn, fn_one_arity)) {
        return false;
    }
    if (!map_get_sentinel(entry, KW_HAS_ARG, NULL)) {
        return false;
    }
    ID queued_arg = map_get_sentinel(entry, KW_ARG, NOT_FOUND);
    if (queued_arg == NOT_FOUND || !queued_arg) {
        return false;
    }
    ID queued_kind = NULL;
    ID queued_key = NULL;
    if (!event_loop_extract_event_kind_and_key(queued_arg, &queued_kind, &queued_key)) {
        return false;
    }
    return event_loop_value_equals(queued_kind, kind) &&
           event_loop_value_equals(queued_key, key);
}

typedef enum {
    EVENT_LOOP_INGRESS_PUSH_REJECTED = 0,
    EVENT_LOOP_INGRESS_PUSH_ENQUEUED = 1,
    EVENT_LOOP_INGRESS_PUSH_COALESCED = 2
} EventLoopIngressPushResult;

static EventLoopIngressPushResult event_loop_ingress_push_with_coalescing(ID entry,
                                                                           ID coalesce_fn,
                                                                           ID coalesce_kind,
                                                                           ID coalesce_key) {
    if (!entry) return EVENT_LOOP_INGRESS_PUSH_REJECTED;
    bool coalescing_enabled = coalesce_fn && coalesce_kind && coalesce_key;

    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_closed || g_event_loop_ingress_count >= EVENT_LOOP_INGRESS_CAP) {
        g_event_loop_ingress_rejected_count++;
        event_loop_ingress_lock_release();
        return EVENT_LOOP_INGRESS_PUSH_REJECTED;
    }

    if (coalescing_enabled) {
        for (uint16_t i = 0u; i < g_event_loop_ingress_count; i++) {
            uint16_t idx = (uint16_t)((g_event_loop_ingress_head + i) % EVENT_LOOP_INGRESS_CAP);
            ID queued = g_event_loop_ingress_queue[idx];
            if (event_loop_ingress_entry_matches_kind_key(queued,
                                                          coalesce_fn,
                                                          coalesce_kind,
                                                          coalesce_key)) {
                event_loop_ingress_lock_release();
                return EVENT_LOOP_INGRESS_PUSH_COALESCED;
            }
        }
    }

    uint16_t tail = (uint16_t)((g_event_loop_ingress_head + g_event_loop_ingress_count) % EVENT_LOOP_INGRESS_CAP);
    g_event_loop_ingress_queue[tail] = entry;
    g_event_loop_ingress_count++;
    g_event_loop_ingress_accepted_count++;
    if ((uint32_t)g_event_loop_ingress_count > g_event_loop_ingress_high_watermark) {
        g_event_loop_ingress_high_watermark = (uint32_t)g_event_loop_ingress_count;
    }
    event_loop_ingress_lock_release();
    return EVENT_LOOP_INGRESS_PUSH_ENQUEUED;
}

static ID event_loop_ingress_pop(void) {
    ID out = NULL;
    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_count > 0u) {
        out = g_event_loop_ingress_queue[g_event_loop_ingress_head];
        g_event_loop_ingress_queue[g_event_loop_ingress_head] = NULL;
        g_event_loop_ingress_head = (uint16_t)((g_event_loop_ingress_head + 1u) % EVENT_LOOP_INGRESS_CAP);
        g_event_loop_ingress_count--;
        g_event_loop_ingress_drained_count++;
    }
    event_loop_ingress_lock_release();
    return out;
}

static void event_loop_ingress_drain(void) {
    uint32_t drained = 0u;
    while (drained < EVENT_LOOP_INGRESS_DRAIN_BUDGET) {
        ID fn = event_loop_ingress_pop();
        if (!fn) {
            return;
        }
        event_loop_enqueue(fn, NULL);
        RELEASE(fn);
        drained++;
    }
}

// Go-block task map helpers (tasks WITH result channel)
static CljPersistentMap* task_to_map(CljObject *fn, CljTransientMap *result_chan, ID arg, bool has_arg) {
    CljPersistentMap *task_map = make_map(4, STRONG);
    if (!task_map) return NULL;
    
    CljTransientMap *tmap = map_transient(task_map);
    RELEASE(task_map);
    if (!tmap) return NULL;
    
    map_conj(tmap, KW_FN, fn);
    if (result_chan) {
        map_conj(tmap, KW_RESULT_CHAN, result_chan);
    }
    if (has_arg) {
        map_conj(tmap, KW_HAS_ARG, clj_true);
    }
    if (arg) {
        map_conj(tmap, KW_ARG, arg);
    }
    
    CljPersistentMap *pmap = map_persistent(tmap);
    RETAIN(pmap);
    RELEASE(tmap);
    return pmap;
}

static bool task_from_map(CljPersistentMap *task_map, CljObject **fn, CljTransientMap **result_chan, ID *arg, bool *has_arg) {
    if (!task_map) return false;
    
    ID fn_val = map_get_sentinel(task_map, KW_FN, NULL);
    ID result_chan_val = map_get_sentinel(task_map, KW_RESULT_CHAN, NULL);
    ID has_arg_val = map_get_sentinel(task_map, KW_HAS_ARG, NULL);
    ID arg_val = map_get_sentinel(task_map, KW_ARG, NOT_FOUND);
    
    if (!fn_val) return false;
    
    if (fn) *fn = fn_val;
    if (result_chan) *result_chan = result_chan_val ? result_chan_val : NULL;
    if (arg) *arg = (arg_val == NOT_FOUND) ? NULL : arg_val;
    if (has_arg) *has_arg = (has_arg_val != NULL);
    
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
        event_loop_enqueue(fn_zero_arity, NULL);
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
            g_runtime.task_queue = make_vector_transient(task_vec);
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
    KW_ARG = intern_symbol_global(":arg");
    KW_HAS_ARG = intern_symbol_global(":has-arg");
    KW_EVENT_KIND = intern_symbol_global(":kind");
    KW_EVENT_KEY = intern_symbol_global(":key");
    g_event_loop_ingress_closed = false;
    g_event_loop_ingress_accepted_count = 0u;
    g_event_loop_ingress_rejected_count = 0u;
    g_event_loop_ingress_drained_count = 0u;
    g_event_loop_ingress_high_watermark = 0u;
    g_runloop_last_warn_ns = 0u;
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
    g_event_loop_ingress_closed = false;
    g_event_loop_ingress_accepted_count = 0u;
    g_event_loop_ingress_rejected_count = 0u;
    g_event_loop_ingress_drained_count = 0u;
    g_event_loop_ingress_high_watermark = 0u;
    g_runloop_last_warn_ns = 0u;
    viewer_collision_reset_dispatch_state();
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljTransientMap *result_channel) {
    if (!fn_zero_arity) return;
    
    CljTransientVector *task_vec = task_queue_get();
    if (!task_vec) return;
    
    if (!result_channel) {
        vector_push(task_vec, fn_zero_arity);
        return;
    }
    
    CljPersistentMap *task_map = task_to_map(fn_zero_arity, result_channel, NULL, false);
    if (!task_map) {
        return;
    }
    
    vector_push(task_vec, task_map);
    RELEASE(task_map);
}

bool event_loop_enqueue_ingress(CljObject *fn_zero_arity) {
    if (!fn_zero_arity) return false;
    ID retained = RETAIN(fn_zero_arity);
    EventLoopIngressPushResult push_result =
        event_loop_ingress_push_with_coalescing(retained, NULL, NULL, NULL);
    if (push_result == EVENT_LOOP_INGRESS_PUSH_ENQUEUED) {
        return true;
    }
    RELEASE(retained);
    return false;
}

bool event_loop_enqueue_ingress_call(CljObject *fn_one_arity, ID arg) {
    if (!fn_one_arity) return false;
    ID event_kind = NULL;
    ID event_key = NULL;
    bool can_coalesce = event_loop_extract_event_kind_and_key(arg, &event_kind, &event_key);

    CljPersistentMap *task_map = task_to_map(fn_one_arity, NULL, arg, true);
    if (!task_map) {
        return false;
    }
    EventLoopIngressPushResult push_result =
        event_loop_ingress_push_with_coalescing(task_map,
                                                can_coalesce ? fn_one_arity : NULL,
                                                can_coalesce ? event_kind : NULL,
                                                can_coalesce ? event_key : NULL);
    if (push_result == EVENT_LOOP_INGRESS_PUSH_ENQUEUED) {
        return true;
    }
    if (push_result == EVENT_LOOP_INGRESS_PUSH_COALESCED) {
#ifdef DEBUG
        if (can_coalesce) {
            event_loop_debug_log_coalesced_ingress_event(fn_one_arity, arg, event_kind, event_key);
        }
#endif
        RELEASE(task_map);
        return true;
    }
    RELEASE(task_map);
    return false;
}

bool event_loop_ingress_has_pending(void) {
    event_loop_ingress_lock_acquire();
    bool pending = g_event_loop_ingress_count > 0u;
    event_loop_ingress_lock_release();
    return pending;
}

void event_loop_ingress_close(void) {
    event_loop_ingress_lock_acquire();
    g_event_loop_ingress_closed = true;
    event_loop_ingress_lock_release();
}

bool event_loop_ingress_is_closed(void) {
    event_loop_ingress_lock_acquire();
    bool closed = g_event_loop_ingress_closed;
    event_loop_ingress_lock_release();
    return closed;
}

bool event_loop_ingress_stats(EventLoopIngressStats *out_stats) {
    if (!out_stats) return false;
    event_loop_ingress_lock_acquire();
    out_stats->accepted_count = g_event_loop_ingress_accepted_count;
    out_stats->rejected_count = g_event_loop_ingress_rejected_count;
    out_stats->drained_count = g_event_loop_ingress_drained_count;
    out_stats->high_watermark = g_event_loop_ingress_high_watermark;
    out_stats->pending_count = (uint32_t)g_event_loop_ingress_count;
    out_stats->closed = g_event_loop_ingress_closed;
    event_loop_ingress_lock_release();
    return true;
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
    uint64_t tick_start_ns = event_loop_monotonic_now_ns();
    (void)env;

    // Promote ISR-raised drain requests into regular event-loop tasks.
    gpio_poll_drain();

    // Promote raw UI-thread collision hits into runloop-safe ingress calls.
    (void)viewer_collision_poll_drain();

    // Consume cross-thread callback ingress before timer/task processing.
    event_loop_ingress_drain();

    timer_process();

    CljTransientVector *task_vec = task_queue_get();
    if (!task_vec) return false;
    if (!task_vec->backing || vector_count(task_vec->backing) == 0u) {
        return false;
    }

    // Get first task (FIFO)
    ID entry = vector_nth(task_vec->backing, 0);
    RETAIN(entry);
    vector_remove_at(task_vec, 0);

    CljObject *fn = NULL;
    CljTransientMap *result_chan = NULL;
    ID arg = NULL;
    bool has_arg = false;

    if (TAG(entry) == CLJ_MAP_PERSISTENT) {
        CljPersistentMap *task_map = entry;
        if (!task_from_map(task_map, &fn, &result_chan, &arg, &has_arg)) {
            RELEASE(task_map);
            return false;
        }
        RETAIN(fn);
        RETAIN(result_chan);
        RETAIN(arg);
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
    char eval_task_stack_anchor;
    TRY {
        /*
         * Always bind a per-task stack anchor so eval's byte-based stack guard
         * can trap deep recursion before the OS guard page is hit.
         */
        eval_bind_task_stack_anchor(&eval_task_stack_anchor);
        WITH_AUTORELEASE_POOL({
            if (has_arg) {
                ID call_args[1] = {arg};
                result = eval_function_call(fn, call_args, 1, env, st);
            } else {
                result = eval_function_call(fn, NULL, 0, env, st);
            }
        });
    } CATCH(ex) {
        ok = false;
        if (ex) {
            event_loop_mini_fprintf(stderr,
                                    "[runloop] task exception while executing deferred callback/task\n");
            print_exception(ex);
            fflush(stderr);
        }
    } END_TRY

    if (result_chan) {
        if (ok) {
            result_channel_put(result_chan, result);
        }
        result_channel_close(result_chan);
        RELEASE(result_chan);
    }

    if (!IS_IMMEDIATE(result)) RELEASE(result);
    RELEASE(arg);
    RELEASE(fn);
    uint64_t tick_end_ns = event_loop_monotonic_now_ns();
    if (tick_start_ns != 0u && tick_end_ns > tick_start_ns) {
        event_loop_warn_if_slow_tick(tick_end_ns - tick_start_ns, tick_end_ns);
    }
    return true;
}

// Enqueue a timer task
int timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms) {
    if (!fn_zero_arity) return 0;

    int timer_id = ++g_runtime.timer_id_counter;
    bool ok = timer_schedule_with_id(fn_zero_arity, delay_ms, periodic, period_ms, timer_id);
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
