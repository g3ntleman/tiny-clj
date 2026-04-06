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
#include "thread.h"
#include "to_string.h"
#include "value.h"
#include "gpio.h"
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
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// Task Map keys (go-block tasks only)
static ID KW_FN;
static ID KW_RESULT_CHAN;
static ID KW_ARG;
static ID KW_HAS_ARG;
static ID KW_EVENT_KEY;
static ID KW_EVENT_OPTIONS;
static ID KW_RUNLOOP_OPT_COALESCE;
static ID KW_RUNLOOP_OPT_COALESCE_IDLE;
static ID KW_RUNLOOP_OPT_COALESCE_FIRST;
static ID KW_RUNLOOP_OPT_COALESCE_FIRST_IDLE;
static const IdSymbolCacheEntry g_event_loop_kw_cache[] = {
    {&KW_FN, ":fn"},
    {&KW_RESULT_CHAN, ":result-chan"},
    {&KW_ARG, ":arg"},
    {&KW_HAS_ARG, ":has-arg"},
    {&KW_EVENT_KEY, ":key"},
    {&KW_EVENT_OPTIONS, ":event/options"},
    {&KW_RUNLOOP_OPT_COALESCE, ":coalesce"},
    {&KW_RUNLOOP_OPT_COALESCE_IDLE, ":coalesce-idle"},
    {&KW_RUNLOOP_OPT_COALESCE_FIRST, ":coalesce-first"},
    {&KW_RUNLOOP_OPT_COALESCE_FIRST_IDLE, ":coalesce-first-idle"},
};

typedef struct {
    ID key;
    int timer_id;
    bool occupied;
} NamedTimerEntry;

#define NAMED_TIMER_CAP 16
static NamedTimerEntry g_named_timers[NAMED_TIMER_CAP];

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
#define EVENT_LOOP_NATIVE_TICK_BUDGET   4u
typedef enum {
    EVENT_LOOP_INGRESS_KIND_CLOJURE = 0,
    EVENT_LOOP_INGRESS_KIND_NATIVE = 1,
} EventLoopIngressKind;

typedef struct {
    EventLoopIngressKind kind;
    ID fn;
    ID arg;
    ID coalesce_key;
    bool has_arg;
    bool coalesce_enabled;
    bool coalesce_keep_first;
    bool dispatch_idle_only;
    EventLoopNativeIngressFn native_callback;
    EventLoopNativeIngressCleanupFn native_cleanup;
    void *native_ctx;
} EventLoopIngressSlot;

static EventLoopIngressSlot g_event_loop_ingress_queue[EVENT_LOOP_INGRESS_CAP];
static uint16_t g_event_loop_ingress_head = 0u;
static uint16_t g_event_loop_ingress_count = 0u;
static bool g_event_loop_ingress_closed = false;
static uint32_t g_event_loop_ingress_accepted_count = 0u;
static uint32_t g_event_loop_ingress_rejected_count = 0u;
static uint32_t g_event_loop_ingress_drained_count = 0u;
static uint32_t g_event_loop_ingress_high_watermark = 0u;
static atomic_flag g_event_loop_ingress_lock = ATOMIC_FLAG_INIT;
static SubjectiveCMutex *g_event_loop_wait_mutex = NULL;
static SubjectiveCCondVar *g_event_loop_wait_cond = NULL;
static atomic_uint_fast64_t g_event_loop_wait_epoch = 1u;
#if defined(ESP_PLATFORM)
static atomic_uintptr_t g_event_loop_wait_task_handle = 0u;
#endif
static uint64_t g_runloop_last_warn_ns = 0u;

#define RUNLOOP_BLOCK_WARN_THRESHOLD_NS 1000000000ull
#define RUNLOOP_SLOW_CLOJURE_TASK_WARN_THRESHOLD_NS (20ull * 1000ull * 1000ull)

static CljPersistentMap* task_to_map(CljObject *fn, CljTransientMap *result_chan, ID arg, bool has_arg);
static CljTransientVector* task_queue_get(void);
static inline bool event_loop_value_equals(ID a, ID b);

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

static inline void event_loop_notify_waiters(void) {
    (void)atomic_fetch_add_explicit(&g_event_loop_wait_epoch, 1u, memory_order_release);
#if defined(ESP_PLATFORM)
    uintptr_t handle_bits = atomic_load_explicit(&g_event_loop_wait_task_handle, memory_order_acquire);
    if (handle_bits == 0u) {
        return;
    }
    TaskHandle_t waiter = (TaskHandle_t)handle_bits;
    if (xPortInIsrContext()) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(waiter, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        xTaskNotifyGive(waiter);
    }
    return;
#endif
    if (!g_event_loop_wait_mutex || !g_event_loop_wait_cond) {
        return;
    }
    subjective_c_mutex_lock(g_event_loop_wait_mutex);
    subjective_c_condvar_broadcast(g_event_loop_wait_cond);
    subjective_c_mutex_unlock(g_event_loop_wait_mutex);
}

static bool event_loop_wait_state_init(void) {
#if defined(ESP_PLATFORM)
    return true;
#else
    if (!g_event_loop_wait_mutex) {
        g_event_loop_wait_mutex = subjective_c_mutex_create();
    }
    if (!g_event_loop_wait_cond) {
        g_event_loop_wait_cond = subjective_c_condvar_create();
    }
    return g_event_loop_wait_mutex && g_event_loop_wait_cond;
#endif
}

static void event_loop_wait_for_signal_or_timeout(int timeout_ms, uint64_t observed_epoch) {
    if (timeout_ms == 0) {
        return;
    }
#if defined(ESP_PLATFORM)
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (!self) {
        return;
    }
    (void)ulTaskNotifyTake(pdTRUE, 0);
    atomic_store_explicit(&g_event_loop_wait_task_handle, (uintptr_t)self, memory_order_release);
    while (atomic_load_explicit(&g_event_loop_wait_epoch, memory_order_acquire) == observed_epoch) {
        if (timeout_ms < 0) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        TickType_t wait_ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }
        if (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0u) {
            break;
        }
    }
    atomic_store_explicit(&g_event_loop_wait_task_handle, 0u, memory_order_release);
    return;
#endif
    if (!event_loop_wait_state_init()) {
        return;
    }
    subjective_c_mutex_lock(g_event_loop_wait_mutex);
    if (timeout_ms < 0) {
        while (atomic_load_explicit(&g_event_loop_wait_epoch, memory_order_acquire) == observed_epoch) {
            if (!subjective_c_condvar_wait(g_event_loop_wait_cond,
                                           g_event_loop_wait_mutex,
                                           UINT32_MAX)) {
                break;
            }
        }
    } else {
        while (atomic_load_explicit(&g_event_loop_wait_epoch, memory_order_acquire) == observed_epoch) {
            if (!subjective_c_condvar_wait(g_event_loop_wait_cond,
                                           g_event_loop_wait_mutex,
                                           (uint32_t)timeout_ms)) {
                break;
            }
        }
    }
    subjective_c_mutex_unlock(g_event_loop_wait_mutex);
}

static inline void event_loop_wait_state_reset(void) {
    atomic_store_explicit(&g_event_loop_wait_epoch, 1u, memory_order_release);
#if defined(ESP_PLATFORM)
    atomic_store_explicit(&g_event_loop_wait_task_handle, 0u, memory_order_release);
#endif
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

static void event_loop_log_value_to_buffer(ID value, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0u) {
        return;
    }

    if (!value) {
        (void)mini_snprintf(buffer, buffer_size, "nil");
        return;
    }

    if (IS_IMMEDIATE(value)) {
        CljType tag = TAG(value);
        if (tag == CLJ_FIXNUM) {
            (void)mini_snprintf(buffer, buffer_size, "%d", as_fixnum(value));
            return;
        }
        if (tag == CLJ_BOOL) {
            (void)mini_snprintf(buffer, buffer_size, "%s", clj_is_truthy(value) ? "true" : "false");
            return;
        }
        (void)mini_snprintf(buffer, buffer_size, "<%s>", clj_type_name(tag));
        return;
    }

    CljString *rendered = to_string(value);
    if (rendered) {
        (void)mini_snprintf(buffer, buffer_size, "%s", string_data((ID)rendered));
        return;
    }

    (void)mini_snprintf(buffer, buffer_size, "<%s>", clj_type_name(TAG(value)));
}

static void event_loop_warn_if_slow_clojure_task(uint64_t elapsed_ns,
                                                 ID fn,
                                                 bool has_arg,
                                                 ID arg) {
    if (elapsed_ns < RUNLOOP_SLOW_CLOJURE_TASK_WARN_THRESHOLD_NS) {
        return;
    }
    unsigned long elapsed_ms = (unsigned long)(elapsed_ns / 1000000ull);
    char fn_buf[160] = {0};
    event_loop_log_value_to_buffer(fn, fn_buf, sizeof(fn_buf));
    if (has_arg) {
        char arg_buf[160] = {0};
        event_loop_log_value_to_buffer(arg, arg_buf, sizeof(arg_buf));
        fprintf(stdout,
                "[runloop] warning: clojure runloop event took %lums "
                "(threshold: 20ms, fn=%s, payload=%s)\n",
                elapsed_ms,
                fn_buf,
                arg_buf);
    } else {
        fprintf(stdout,
                "[runloop] warning: clojure runloop event took %lums "
                "(threshold: 20ms, fn=%s)\n",
                elapsed_ms,
                fn_buf);
    }
    fflush(stdout);
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

static bool event_loop_ingress_slot_valid(const EventLoopIngressSlot *slot) {
    if (!slot) {
        return false;
    }
    if (slot->kind == EVENT_LOOP_INGRESS_KIND_NATIVE) {
        return slot->native_callback != NULL;
    }
    return slot->fn != NULL;
}

static void event_loop_ingress_slot_cleanup(EventLoopIngressSlot *slot) {
    if (!slot) {
        return;
    }
    if (slot->kind == EVENT_LOOP_INGRESS_KIND_NATIVE) {
        if (slot->native_cleanup) {
            slot->native_cleanup(slot->native_ctx);
        }
    } else {
        WITH_MUTEX(event_loop_ingress_lock) {
            RELEASE(slot->coalesce_key);
            RELEASE(slot->arg);
            RELEASE(slot->fn);
        }
    }
    memset(slot, 0, sizeof(*slot));
}

static inline void event_loop_ingress_slot_retain_clojure(EventLoopIngressSlot *slot) {
    if (!slot || slot->kind != EVENT_LOOP_INGRESS_KIND_CLOJURE) {
        return;
    }
    slot->fn = RETAIN(slot->fn);
    slot->arg = RETAIN(slot->arg);
    slot->coalesce_key = RETAIN(slot->coalesce_key);
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

static bool event_loop_is_event_options_key(ID key_obj) {
    if (!is_symbol(key_obj)) {
        return false;
    }
    CljSymbol *sym = as_symbol(key_obj);
    if (!sym || !sym->cname || strcmp(sym->cname, ":options") != 0) {
        return false;
    }
    CljSymbol *ns_sym = sym->ns_name;
    return ns_sym && ns_sym->cname && strcmp(ns_sym->cname, "event") == 0;
}

static void event_loop_parse_payload_options(ID payload,
                                             bool *out_coalesce_enabled,
                                             bool *out_coalesce_keep_first,
                                             bool *out_dispatch_idle_only,
                                             ID *out_coalesce_key) {
    if (out_coalesce_enabled) *out_coalesce_enabled = false;
    if (out_coalesce_keep_first) *out_coalesce_keep_first = false;
    if (out_dispatch_idle_only) *out_dispatch_idle_only = false;
    if (out_coalesce_key) *out_coalesce_key = NULL;
    if (!payload || !KW_EVENT_OPTIONS) return;

    ID options = event_loop_value_get_sentinel(payload, KW_EVENT_OPTIONS, NOT_FOUND);
    if (options == NOT_FOUND && is_map(payload)) {
        MAP_FOR_EACH(payload, k, v) {
            if (event_loop_is_event_options_key((ID)k)) {
                options = (ID)v;
                break;
            }
        }
    }
    if (options == NOT_FOUND) {
        return;
    }
    bool coalesce_enabled = false;
    bool keep_first = false;
    bool dispatch_idle_only = false;
    if (event_loop_value_equals(options, KW_RUNLOOP_OPT_COALESCE)) {
        coalesce_enabled = true;
    } else if (event_loop_value_equals(options, KW_RUNLOOP_OPT_COALESCE_IDLE)) {
        coalesce_enabled = true;
        dispatch_idle_only = true;
    } else if (event_loop_value_equals(options, KW_RUNLOOP_OPT_COALESCE_FIRST)) {
        coalesce_enabled = true;
        keep_first = true;
    } else if (event_loop_value_equals(options, KW_RUNLOOP_OPT_COALESCE_FIRST_IDLE)) {
        coalesce_enabled = true;
        keep_first = true;
        dispatch_idle_only = true;
    }

    ID coalesce_key = NULL;
    if (coalesce_enabled && KW_EVENT_KEY) {
        ID key = event_loop_value_get_sentinel(payload, KW_EVENT_KEY, NOT_FOUND);
        if (key != NOT_FOUND && key) {
            coalesce_key = key;
        } else {
            coalesce_enabled = false;
            keep_first = false;
            dispatch_idle_only = false;
        }
    }

    if (out_coalesce_enabled) *out_coalesce_enabled = coalesce_enabled;
    if (out_coalesce_keep_first) *out_coalesce_keep_first = keep_first;
    if (out_dispatch_idle_only) *out_dispatch_idle_only = dispatch_idle_only;
    if (out_coalesce_key) *out_coalesce_key = coalesce_key;
}

static void event_loop_debug_log_coalesced_ingress_event(ID fn_one_arity,
                                                         ID payload) {
    char fn_buf[160] = {0};
    char payload_buf[160] = {0};
    event_loop_log_value_to_buffer(fn_one_arity, fn_buf, sizeof(fn_buf));
    event_loop_log_value_to_buffer(payload, payload_buf, sizeof(payload_buf));

    event_loop_mini_fprintf(stderr,
                            "[runloop][ingress][coalesce] fn=%s payload=%s\n",
                            fn_buf,
                            payload_buf);
}

static bool event_loop_ingress_entry_matches_payload(const EventLoopIngressSlot *entry,
                                                     const EventLoopIngressSlot *candidate) {
    if (!entry || !candidate ||
        entry->kind != EVENT_LOOP_INGRESS_KIND_CLOJURE ||
        candidate->kind != EVENT_LOOP_INGRESS_KIND_CLOJURE ||
        !entry->coalesce_enabled || !candidate->coalesce_enabled ||
        !entry->fn || !candidate->fn ||
        !entry->coalesce_key || !candidate->coalesce_key) {
        return false;
    }
    if (!event_loop_value_equals(entry->fn, candidate->fn)) {
        return false;
    }
    return event_loop_value_equals(entry->coalesce_key, candidate->coalesce_key);
}

typedef enum {
    EVENT_LOOP_INGRESS_PUSH_REJECTED = 0,
    EVENT_LOOP_INGRESS_PUSH_ENQUEUED = 1,
    EVENT_LOOP_INGRESS_PUSH_COALESCED = 2
} EventLoopIngressPushResult;

static EventLoopIngressPushResult event_loop_ingress_push_with_coalescing(EventLoopIngressSlot entry) {
    if (!event_loop_ingress_slot_valid(&entry)) return EVENT_LOOP_INGRESS_PUSH_REJECTED;

    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_closed || g_event_loop_ingress_count >= EVENT_LOOP_INGRESS_CAP) {
        g_event_loop_ingress_rejected_count++;
        event_loop_ingress_lock_release();
        return EVENT_LOOP_INGRESS_PUSH_REJECTED;
    }

    if (entry.kind == EVENT_LOOP_INGRESS_KIND_CLOJURE && entry.coalesce_enabled && entry.coalesce_key) {
        for (uint16_t i = 0u; i < g_event_loop_ingress_count; i++) {
            uint16_t idx = (uint16_t)((g_event_loop_ingress_head + i) % EVENT_LOOP_INGRESS_CAP);
            EventLoopIngressSlot *queued = &g_event_loop_ingress_queue[idx];
            if (event_loop_ingress_entry_matches_payload(queued, &entry)) {
                if (!queued->coalesce_keep_first) {
                    RELEASE(queued->coalesce_key);
                    RELEASE(queued->arg);
                    RELEASE(queued->fn);
                    queued->fn = entry.fn;
                    queued->arg = entry.arg;
                    queued->coalesce_key = entry.coalesce_key;
                    event_loop_ingress_slot_retain_clojure(queued);
                    queued->has_arg = entry.has_arg;
                    queued->dispatch_idle_only = entry.dispatch_idle_only;
                    queued->coalesce_keep_first = entry.coalesce_keep_first;
                    queued->coalesce_enabled = entry.coalesce_enabled;
                }
                event_loop_ingress_lock_release();
                return EVENT_LOOP_INGRESS_PUSH_COALESCED;
            }
        }
    }

    uint16_t tail = (uint16_t)((g_event_loop_ingress_head + g_event_loop_ingress_count) % EVENT_LOOP_INGRESS_CAP);
    EventLoopIngressSlot *queued = &g_event_loop_ingress_queue[tail];
    *queued = entry;
    event_loop_ingress_slot_retain_clojure(queued);
    g_event_loop_ingress_count++;
    g_event_loop_ingress_accepted_count++;
    if ((uint32_t)g_event_loop_ingress_count > g_event_loop_ingress_high_watermark) {
        g_event_loop_ingress_high_watermark = (uint32_t)g_event_loop_ingress_count;
    }
    event_loop_ingress_lock_release();
    event_loop_notify_waiters();
    return EVENT_LOOP_INGRESS_PUSH_ENQUEUED;
}

static bool event_loop_ingress_peek(EventLoopIngressSlot *out) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_count > 0u) {
        if (out) {
            *out = g_event_loop_ingress_queue[g_event_loop_ingress_head];
        }
        event_loop_ingress_lock_release();
        return true;
    }
    event_loop_ingress_lock_release();
    return false;
}

static void event_loop_ingress_drop_head(void) {
    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_count > 0u) {
        memset(&g_event_loop_ingress_queue[g_event_loop_ingress_head], 0,
               sizeof(g_event_loop_ingress_queue[g_event_loop_ingress_head]));
        g_event_loop_ingress_head = (uint16_t)((g_event_loop_ingress_head + 1u) % EVENT_LOOP_INGRESS_CAP);
        g_event_loop_ingress_count--;
        g_event_loop_ingress_drained_count++;
    }
    event_loop_ingress_lock_release();
}

static bool event_loop_ingress_pop_dispatchable(EventLoopIngressSlot *out,
                                                bool allow_immediate_native,
                                                bool allow_idle_clojure) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }

    event_loop_ingress_lock_acquire();
    if (g_event_loop_ingress_count == 0u) {
        event_loop_ingress_lock_release();
        return false;
    }

    bool found = false;
    uint16_t selected_offset = 0u;
    for (uint16_t i = 0u; i < g_event_loop_ingress_count; i++) {
        uint16_t idx = (uint16_t)((g_event_loop_ingress_head + i) % EVENT_LOOP_INGRESS_CAP);
        EventLoopIngressSlot *slot = &g_event_loop_ingress_queue[idx];
        if (!event_loop_ingress_slot_valid(slot)) {
            continue;
        }
        if (slot->kind == EVENT_LOOP_INGRESS_KIND_NATIVE) {
            if (!allow_immediate_native) {
                continue;
            }
            selected_offset = i;
            found = true;
            break;
        }
        if (slot->dispatch_idle_only && !allow_idle_clojure) {
            continue;
        }
        selected_offset = i;
        found = true;
        break;
    }

    if (!found) {
        event_loop_ingress_lock_release();
        return false;
    }

    uint16_t selected_index = (uint16_t)((g_event_loop_ingress_head + selected_offset) % EVENT_LOOP_INGRESS_CAP);
    if (out) {
        *out = g_event_loop_ingress_queue[selected_index];
    }
    if (selected_offset == 0u) {
        memset(&g_event_loop_ingress_queue[g_event_loop_ingress_head],
               0,
               sizeof(g_event_loop_ingress_queue[g_event_loop_ingress_head]));
        g_event_loop_ingress_head = (uint16_t)((g_event_loop_ingress_head + 1u) % EVENT_LOOP_INGRESS_CAP);
        g_event_loop_ingress_count--;
        g_event_loop_ingress_drained_count++;
        event_loop_ingress_lock_release();
        return true;
    }
    for (uint16_t j = selected_offset; j + 1u < g_event_loop_ingress_count; j++) {
        uint16_t dst = (uint16_t)((g_event_loop_ingress_head + j) % EVENT_LOOP_INGRESS_CAP);
        uint16_t src = (uint16_t)((g_event_loop_ingress_head + j + 1u) % EVENT_LOOP_INGRESS_CAP);
        g_event_loop_ingress_queue[dst] = g_event_loop_ingress_queue[src];
    }
    uint16_t tail = (uint16_t)((g_event_loop_ingress_head + g_event_loop_ingress_count - 1u) % EVENT_LOOP_INGRESS_CAP);
    memset(&g_event_loop_ingress_queue[tail], 0, sizeof(g_event_loop_ingress_queue[tail]));
    g_event_loop_ingress_count--;
    g_event_loop_ingress_drained_count++;
    event_loop_ingress_lock_release();
    return true;
}

static bool event_loop_run_native_ingress_callback(EventLoopNativeIngressFn callback,
                                                   void *ctx,
                                                   EvalState *st) {
    if (!callback) {
        return false;
    }

    bool ok = true;
    char eval_task_stack_anchor;
    TRY {
        eval_bind_task_stack_anchor(&eval_task_stack_anchor);
        WITH_AUTORELEASE_POOL({
            callback(ctx, st);
        });
    } CATCH(ex) {
        ok = false;
        if (ex) {
            event_loop_mini_fprintf(stderr,
                                    "[runloop] native ingress callback threw while executing deferred work\n");
            print_exception(ex);
            fflush(stderr);
        }
    } END_TRY
    return ok;
}

static bool event_loop_ingress_drain(EventLoopIngressSlot *out_native_slot,
                                     bool allow_immediate_native,
                                     bool allow_idle_clojure) {
    if (out_native_slot) {
        memset(out_native_slot, 0, sizeof(*out_native_slot));
    }
    uint32_t drained = 0u;
    while (drained < EVENT_LOOP_INGRESS_DRAIN_BUDGET) {
        EventLoopIngressSlot slot = {0};
        if (!event_loop_ingress_pop_dispatchable(&slot,
                                                 allow_immediate_native,
                                                 allow_idle_clojure) ||
            !event_loop_ingress_slot_valid(&slot)) {
            return false;
        }
        if (slot.kind == EVENT_LOOP_INGRESS_KIND_NATIVE) {
            if (out_native_slot) {
                *out_native_slot = slot;
            } else {
                event_loop_ingress_slot_cleanup(&slot);
            }
            return true;
        }
        if (slot.has_arg) {
            CljPersistentMap *task_map = task_to_map(slot.fn, NULL, slot.arg, true);
            if (!task_map) {
                return false;
            }
            CljTransientVector *task_vec = task_queue_get();
            if (!task_vec) {
                RELEASE(task_map);
                return false;
            }
            vector_push(task_vec, task_map);
            RELEASE(task_map);
        } else {
            event_loop_enqueue(slot.fn, NULL);
        }
        event_loop_ingress_slot_cleanup(&slot);
        drained++;
    }
    return false;
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

static int timer_named_find_by_key_index(ID key) {
    if (!timer_key_is_valid(key)) return -1;
    for (int i = 0; i < NAMED_TIMER_CAP; i++) {
        if (g_named_timers[i].occupied && clj_equal(g_named_timers[i].key, key)) {
            return i;
        }
    }
    return -1;
}

static int timer_named_find_by_id_index(int timer_id) {
    if (timer_id <= 0) return -1;
    for (int i = 0; i < NAMED_TIMER_CAP; i++) {
        if (g_named_timers[i].occupied && g_named_timers[i].timer_id == timer_id) {
            return i;
        }
    }
    return -1;
}

static int timer_named_find_free_index(void) {
    for (int i = 0; i < NAMED_TIMER_CAP; i++) {
        if (!g_named_timers[i].occupied) {
            return i;
        }
    }
    return -1;
}

static bool timer_named_set(ID key, int timer_id) {
    if (!timer_key_is_valid(key) || timer_id <= 0) return false;
    int existing_index = timer_named_find_by_key_index(key);
    if (existing_index >= 0) {
        g_named_timers[existing_index].timer_id = timer_id;
        return true;
    }
    int free_index = timer_named_find_free_index();
    if (free_index < 0) return false;
    g_named_timers[free_index].key = RETAIN(key);
    g_named_timers[free_index].timer_id = timer_id;
    g_named_timers[free_index].occupied = true;
    return true;
}

static bool timer_named_take_by_key(ID key, int *out_timer_id) {
    if (out_timer_id) *out_timer_id = 0;
    if (!timer_key_is_valid(key)) return false;
    int index = timer_named_find_by_key_index(key);
    if (index < 0) return false;
    if (out_timer_id) *out_timer_id = g_named_timers[index].timer_id;
    RELEASE(g_named_timers[index].key);
    memset(&g_named_timers[index], 0, sizeof(g_named_timers[index]));
    return true;
}

static bool timer_named_remove_by_id(int timer_id) {
    int index = timer_named_find_by_id_index(timer_id);
    if (index < 0) return false;
    RELEASE(g_named_timers[index].key);
    memset(&g_named_timers[index], 0, sizeof(g_named_timers[index]));
    return true;
}

static void timer_named_clear_all(void) {
    for (int i = 0; i < NAMED_TIMER_CAP; i++) {
        if (g_named_timers[i].occupied) {
            RELEASE(g_named_timers[i].key);
            memset(&g_named_timers[i], 0, sizeof(g_named_timers[i]));
        }
    }
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
    bool inserted = timer_insert_sorted(entry);
    if (inserted) {
        event_loop_notify_waiters();
    }
    return inserted;
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
    (void)id_symbol_cache_init_global(
        g_event_loop_kw_cache,
        sizeof(g_event_loop_kw_cache) / sizeof(g_event_loop_kw_cache[0]));
    (void)event_loop_wait_state_init();
    g_event_loop_ingress_closed = false;
    g_event_loop_ingress_accepted_count = 0u;
    g_event_loop_ingress_rejected_count = 0u;
    g_event_loop_ingress_drained_count = 0u;
    g_event_loop_ingress_high_watermark = 0u;
    event_loop_wait_state_reset();
    g_runloop_last_warn_ns = 0u;
    task_queue_get();
}

void event_loop_clear(void) {
    if (g_runtime.task_queue && (uintptr_t)g_runtime.task_queue >= 0x1000) {
        CljTransientVector *task_vec = task_queue_get();
        if (task_vec && task_vec->backing) {
            // Clear the backing and reset the ringbuffer head so subsequent
            // push operations write at data[0] again.
            vector_clear(task_vec->backing);
            task_vec->head = 0;
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
        EventLoopIngressSlot slot = {0};
        if (!event_loop_ingress_peek(&slot) || !event_loop_ingress_slot_valid(&slot)) {
            break;
        }
        event_loop_ingress_drop_head();
        event_loop_ingress_slot_cleanup(&slot);
    }
    g_event_loop_ingress_closed = false;
    g_event_loop_ingress_accepted_count = 0u;
    g_event_loop_ingress_rejected_count = 0u;
    g_event_loop_ingress_drained_count = 0u;
    g_event_loop_ingress_high_watermark = 0u;
    event_loop_wait_state_reset();
    g_runloop_last_warn_ns = 0u;
    event_loop_notify_waiters();
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljTransientMap *result_channel) {
    if (!fn_zero_arity) return;
    
    CljTransientVector *task_vec = task_queue_get();
    if (!task_vec) return;
    
    if (!result_channel) {
        vector_push(task_vec, fn_zero_arity);
        event_loop_notify_waiters();
        return;
    }
    
    CljPersistentMap *task_map = task_to_map(fn_zero_arity, result_channel, NULL, false);
    if (!task_map) {
        return;
    }
    
    vector_push(task_vec, task_map);
    RELEASE(task_map);
    event_loop_notify_waiters();
}

bool event_loop_enqueue_ingress(CljObject *fn_zero_arity) {
    if (!fn_zero_arity) return false;
    EventLoopIngressSlot slot = {
        .kind = EVENT_LOOP_INGRESS_KIND_CLOJURE,
        .fn = fn_zero_arity,
        .arg = NULL,
        .coalesce_key = NULL,
        .has_arg = false,
        .coalesce_enabled = false,
        .coalesce_keep_first = false,
        .dispatch_idle_only = false,
    };
    EventLoopIngressPushResult push_result =
        event_loop_ingress_push_with_coalescing(slot);
    return push_result == EVENT_LOOP_INGRESS_PUSH_ENQUEUED;
}

bool event_loop_enqueue_ingress_call(CljObject *fn_one_arity, ID arg) {
    if (!fn_one_arity) return false;
    bool coalesce_enabled = false;
    bool coalesce_keep_first = false;
    bool dispatch_idle_only = false;
    ID coalesce_key = NULL;
    event_loop_parse_payload_options(arg,
                                     &coalesce_enabled,
                                     &coalesce_keep_first,
                                     &dispatch_idle_only,
                                     &coalesce_key);
    EventLoopIngressSlot slot = {
        .kind = EVENT_LOOP_INGRESS_KIND_CLOJURE,
        .fn = fn_one_arity,
        .arg = arg,
        .coalesce_key = coalesce_key,
        .has_arg = true,
        .coalesce_enabled = coalesce_enabled,
        .coalesce_keep_first = coalesce_keep_first,
        .dispatch_idle_only = dispatch_idle_only,
    };
    EventLoopIngressPushResult push_result =
        event_loop_ingress_push_with_coalescing(slot);
    if (push_result == EVENT_LOOP_INGRESS_PUSH_ENQUEUED) {
        return true;
    }
    if (push_result == EVENT_LOOP_INGRESS_PUSH_COALESCED) {
#ifdef DEBUG
        if (coalesce_enabled) {
            event_loop_debug_log_coalesced_ingress_event(fn_one_arity, arg);
        }
#endif
        return true;
    }
    return false;
}

bool event_loop_enqueue_ingress_native(EventLoopNativeIngressFn callback,
                                       void *ctx,
                                       EventLoopNativeIngressCleanupFn cleanup) {
    if (!callback) {
        return false;
    }
    EventLoopIngressSlot slot = {
        .kind = EVENT_LOOP_INGRESS_KIND_NATIVE,
        .coalesce_key = NULL,
        .coalesce_enabled = false,
        .coalesce_keep_first = false,
        .dispatch_idle_only = false,
        .native_callback = callback,
        .native_cleanup = cleanup,
        .native_ctx = ctx,
    };
    EventLoopIngressPushResult push_result =
        event_loop_ingress_push_with_coalescing(slot);
    return push_result == EVENT_LOOP_INGRESS_PUSH_ENQUEUED;
}

bool event_loop_dispatch_native(EventLoopNativeIngressFn callback,
                                void *ctx,
                                EventLoopNativeIngressCleanupFn cleanup) {
    if (!callback) {
        return false;
    }
    if (!subjective_c_has_interpreter_thread() || subjective_c_is_interpreter_thread()) {
        EvalState *st = get_global_eval_state();
        bool ok = event_loop_run_native_ingress_callback(callback, ctx, st);
        if (cleanup) {
            cleanup(ctx);
        }
        return ok;
    }
    return event_loop_enqueue_ingress_native(callback, ctx, cleanup);
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
    event_loop_notify_waiters();
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
 * @brief Wakes threads blocked in event_loop_run().
 *
 * @param void
 * @return void
 */
void event_loop_wake(void) {
    event_loop_notify_waiters();
}

/**
 * @brief Blocking driver for event-loop work.
 *
 * Waits until one runnable callback/task exists (or timer becomes due), executes
 * work through event_loop_run_next(), and returns true. Returns false when the
 * wait was interrupted but no runnable work became ready.
 *
 * @param env Optional eval environment, forwarded to event_loop_run_next().
 * @param st Eval state for callback execution.
 * @return true if one event-loop step executed, false if interrupted without work.
 */
bool event_loop_run(CljPersistentMap *env, EvalState *st) {
    while (true) {
        if (event_loop_run_next(env, st)) {
            return true;
        }

        int timeout_ms = event_loop_time_until_next_timer_ms();
        if (timeout_ms == 0 || event_loop_has_pending_tasks()) {
            continue;
        }

        uint64_t observed_epoch = atomic_load_explicit(&g_event_loop_wait_epoch, memory_order_acquire);
        event_loop_wait_for_signal_or_timeout(timeout_ms, observed_epoch);

        if (atomic_load_explicit(&g_event_loop_wait_epoch, memory_order_acquire) != observed_epoch &&
            !event_loop_has_pending_tasks() &&
            event_loop_time_until_next_timer_ms() != 0) {
            return false;
        }
    }
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

    CljTransientVector *task_vec = task_queue_get();
    size_t preexisting_task_count =
        (task_vec && task_vec->backing) ? vector_count(task_vec->backing) : 0u;

    uint32_t native_processed = 0u;
    while (native_processed < EVENT_LOOP_NATIVE_TICK_BUDGET) {
        EventLoopIngressSlot native_slot = {0};
        bool have_native = event_loop_ingress_drain(&native_slot,
                                                     preexisting_task_count == 0u || native_processed > 0u,
                                                     preexisting_task_count == 0u);
        if (!have_native) break;
        (void)event_loop_run_native_ingress_callback(native_slot.native_callback,
                                                      native_slot.native_ctx,
                                                      st);
        event_loop_ingress_slot_cleanup(&native_slot);
        native_processed++;
    }

    timer_process();

    if (native_processed > 0u) {
        uint64_t tick_end_ns = event_loop_monotonic_now_ns();
        if (tick_start_ns != 0u && tick_end_ns > tick_start_ns) {
            event_loop_warn_if_slow_tick(tick_end_ns - tick_start_ns, tick_end_ns);
        }
        return true;
    }

    task_vec = task_queue_get();
    if (!task_vec) return false;
    if (!task_vec->backing || vector_count(task_vec->backing) == 0u) {
        return false;
    }

    // Get first task (FIFO).
    // Access via the ringbuffer head so the physical slot is correct even after
    // front removals have advanced head past data[0].
    ID entry = task_vec->backing->data[task_vec->head % (unsigned int)task_vec->backing->capacity];
    WITH_MUTEX(event_loop_ingress_lock) {
        RETAIN(entry);
    }
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
        WITH_MUTEX(event_loop_ingress_lock) {
            RETAIN(fn);
            RETAIN(arg);
        }
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
        WITH_MUTEX(event_loop_ingress_lock) {
            RELEASE(arg);
            RELEASE(fn);
        }
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
            ID call_result = NULL;
            if (has_arg) {
                ID call_args[1] = {arg};
                call_result = eval_function_call(fn, call_args, 1, env, st);
            } else {
                call_result = eval_function_call(fn, NULL, 0, env, st);
            }
            if (call_result && !IS_IMMEDIATE(call_result)) {
                /*
                 * eval_function_call returns pool-managed heap refs. Keep one
                 * explicit retain so the result survives this local pool drain
                 * until post-call bookkeeping (result-channel put/logging).
                 */
                result = RETAIN(call_result);
            } else {
                result = call_result;
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

    uint64_t tick_end_ns = event_loop_monotonic_now_ns();
    if (tick_start_ns != 0u && tick_end_ns > tick_start_ns) {
        uint64_t elapsed_ns = tick_end_ns - tick_start_ns;
        WITH_AUTORELEASE_POOL({
            event_loop_warn_if_slow_clojure_task(elapsed_ns, fn, has_arg, arg);
        });
        event_loop_warn_if_slow_tick(elapsed_ns, tick_end_ns);
    }
    if (!IS_IMMEDIATE(result)) RELEASE(result);
    WITH_MUTEX(event_loop_ingress_lock) {
        RELEASE(arg);
        RELEASE(fn);
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

    int existing_index = timer_named_find_by_key_index(key);
    int timer_id = (existing_index >= 0) ? g_named_timers[existing_index].timer_id
                                         : (++g_runtime.timer_id_counter);
    if (existing_index >= 0) {
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
            event_loop_notify_waiters();
            return true;
        }
    }
    return false;
}
