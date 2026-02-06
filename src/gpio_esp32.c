// ESP32 GPIO integration for Tiny-CLJ.
//
// Build constraints:
// - This file must only be compiled for ESP32 targets (see CMake target_sources).
// - Guarded by ESP32_BUILD as an extra safety net.
//
// Design:
// - Map-based watcher storage: g_gpio_watchers maps pin->watcher_map
// - watcher_map: {:pin <fixnum> :callback-fn <fn> :watcher-id <fixnum>}
// - ISR retains only callback-fn and enqueues a drain task via event loop.
//
// Defaults (see plan "answer_questions"):
// - Pin config: GPIO input, interrupt on any edge, no pull-up/down.
// - Debouncing: none (userland can filter).
// - Errors: throw exceptions (IO / illegal-state / arity).
// - Multi-core: rely on ESP-IDF defaults.

#ifdef ESP32_BUILD

#include "gpio_esp32.h"

#include "builtins.h"   // builtin_get_eval_state()
#include "eval.h"       // eval_function_call
#include "exception.h"
#include "function.h"   // is_callable
#include "map.h"
#include "memory.h"
#include "runtime.h"
#include "symbol.h"
#include "value.h"
#include "vector.h"
#include "event_loop.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_attr.h>
#include <stdatomic.h>

// Map: pin (fixnum) -> watcher_map
static CljPersistentMap *g_gpio_watchers = NULL;
static int32_t g_next_watcher_id = 1;

// Watcher map keys (interned keywords; singletons)
static CljSymbol *KW_PIN = NULL;

// ISR service installed once.
static bool g_gpio_isr_service_installed = false;

// Event ring buffer (ISR -> event-loop).
// Fixed-size to avoid heap allocations in ISR.
typedef struct {
    int32_t pin;
    int32_t value;
    ID callback_fn; // retained in ISR; released in drain
} GpioEvent;

enum { GPIO_EVENT_RING_CAP = 32 };
static GpioEvent g_gpio_events[GPIO_EVENT_RING_CAP];
static _Atomic uint32_t g_gpio_event_w = 0;
static _Atomic uint32_t g_gpio_event_r = 0;
static _Atomic bool g_gpio_drain_scheduled = false;

// Cached drain function object (zero-arity task for event loop).
static ID g_gpio_drain_fn_obj = NULL;
static CljSymbol *SYM_GPIO_DRAIN = NULL;

// Forward declare drain builtin (zero-arity task).
static ID native_gpio_drain_events(ID *args, unsigned int argc);

static inline void gpio_ensure_initialized(void) {
    if (g_gpio_watchers) return;
    g_gpio_watchers = make_map(4, STRONG);
    KW_PIN = intern_symbol_global(":pin");

    // Create cached drain function object once.
    SYM_GPIO_DRAIN = intern_symbol_global("tinyclj.gpio/drain-events");
    g_gpio_drain_fn_obj = make_named_func(native_gpio_drain_events, SYM_GPIO_DRAIN);
}

static inline bool gpio_event_ring_push_from_isr(int32_t pin, int32_t value, ID callback_fn_retained) {
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_acquire);
    uint32_t next_w = (w + 1u) % GPIO_EVENT_RING_CAP;
    if (next_w == r) return false; // full

    g_gpio_events[w].pin = pin;
    g_gpio_events[w].value = value;
    g_gpio_events[w].callback_fn = callback_fn_retained;
    atomic_store_explicit(&g_gpio_event_w, next_w, memory_order_release);
    return true;
}

static inline bool gpio_event_ring_pop(GpioEvent *out) {
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_acquire);
    if (r == w) return false;

    *out = g_gpio_events[r];
    atomic_store_explicit(&g_gpio_event_r, (r + 1u) % GPIO_EVENT_RING_CAP, memory_order_release);
    return true;
}

static inline void gpio_schedule_drain_from_isr(void) {
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&g_gpio_drain_scheduled, &expected, true, memory_order_acq_rel, memory_order_relaxed)) {
        // Enqueue one drain task; it will drain everything currently queued.
        // NOTE: event_loop_enqueue is not strictly ISR-safe; this follows the chosen plan.
        event_loop_enqueue((CljObject*)g_gpio_drain_fn_obj, NULL);
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg) {
    CljPersistentMap *watcher_map = (CljPersistentMap*)arg;
    if (!watcher_map) return;

    ID pin_val = map_get_sentinel((ID)watcher_map, (ID)KW_PIN, NOT_FOUND);
    if (!pin_val || pin_val == NOT_FOUND) return;
    int32_t pin = (int32_t)as_fixnum(pin_val);
    int32_t value = gpio_get_level((gpio_num_t)pin);

    ID callback_fn = map_get_sentinel((ID)watcher_map, (ID)SYM_KW_CALLBACK_FN, NULL);
    if (!callback_fn || IS_IMMEDIATE(callback_fn)) return;

    // Retain callback_fn so unwatch can't free it while event is pending.
    ID retained_cb = RETAIN(callback_fn);
    if (!gpio_event_ring_push_from_isr(pin, value, retained_cb)) {
        // Ring full: drop event, but balance RETAIN.
        RELEASE(retained_cb);
        return;
    }
    gpio_schedule_drain_from_isr();
}

static ID native_gpio_drain_events(ID *args, unsigned int argc) {
    (void)args;
    CHECK_ARITY(argc, 0, "tinyclj.gpio/drain-events");

    // Drain all pending events.
    GpioEvent ev;
    while (gpio_event_ring_pop(&ev)) {
        CljPersistentVector *event_vec = make_vector(2, STRONG);
        ASSIGN(event_vec, vector_conj(event_vec, fixnum(ev.pin)));
        ASSIGN(event_vec, vector_conj(event_vec, fixnum(ev.value)));

        ID call_args[1] = { (ID)event_vec };
        (void)eval_function_call(ev.callback_fn, call_args, 1, NULL, builtin_get_eval_state());

        RELEASE(event_vec);
        RELEASE(ev.callback_fn);
    }

    // If queue is empty, allow future scheduling.
    atomic_store_explicit(&g_gpio_drain_scheduled, false, memory_order_release);

    // Race: if ISR enqueued while we cleared scheduled, reschedule.
    if (atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed) != atomic_load_explicit(&g_gpio_event_w, memory_order_acquire)) {
        gpio_schedule_drain_from_isr();
    }

    return NULL;
}

static inline void gpio_ensure_isr_service(void) {
    if (g_gpio_isr_service_installed) return;
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        g_gpio_isr_service_installed = true;
        return;
    }
    throw_exception(EXCEPTION_RUNTIME, "gpio_install_isr_service failed", __FILE__, __LINE__, 0);
}

ID native_gpio_watch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "gpio-watch");

    ID pin_val = args[0];
    ID callback = args[1];
    if (!pin_val || !is_fixnum(pin_val)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-watch: pin must be fixnum", __FILE__, __LINE__, 0);
        return NULL;
    }
    if (!is_callable(callback)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-watch: callback must be callable", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_ensure_initialized();

    int32_t pin = (int32_t)as_fixnum(pin_val);
    ID pin_key = fixnum(pin);

    if (map_get((ID)g_gpio_watchers, pin_key) != NOT_FOUND) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-watch: pin already watched", __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t watcher_id = g_next_watcher_id++;
    CljPersistentMap *watcher_map = make_map_from_kv(3,
        (ID)KW_PIN, pin_key,
        (ID)SYM_KW_CALLBACK_FN, callback,
        (ID)SYM_KW_WATCHER_ID, fixnum(watcher_id));

    // Store watcher_map in global index. Map retains value; we release local afterwards.
    ASSIGN(g_gpio_watchers, map_assoc(g_gpio_watchers, pin_key, (ID)watcher_map));

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ASSIGN(g_gpio_watchers, map_remove(g_gpio_watchers, pin_key));
        RELEASE(watcher_map);
        throw_exception(EXCEPTION_RUNTIME, "gpio-watch: gpio_config failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_ensure_isr_service();
    err = gpio_isr_handler_add((gpio_num_t)pin, gpio_isr_handler, (void*)watcher_map);
    if (err != ESP_OK) {
        ASSIGN(g_gpio_watchers, map_remove(g_gpio_watchers, pin_key));
        RELEASE(watcher_map);
        throw_exception(EXCEPTION_RUNTIME, "gpio-watch: gpio_isr_handler_add failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    RELEASE(watcher_map);
    return fixnum(watcher_id);
}

ID native_gpio_unwatch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "gpio-unwatch");

    ID wid_val = args[0];
    if (!wid_val || !is_fixnum(wid_val)) return NULL;
    int32_t wid = (int32_t)as_fixnum(wid_val);

    if (!g_gpio_watchers) return NULL;

    ID found_pin_key = NULL;
    MAP_FOR_EACH(g_gpio_watchers, k, v) {
        ID watcher_map = (ID)v;
        ID stored_wid = map_get_sentinel(watcher_map, (ID)SYM_KW_WATCHER_ID, NOT_FOUND);
        if (stored_wid != NOT_FOUND && stored_wid && is_fixnum(stored_wid) && (int32_t)as_fixnum(stored_wid) == wid) {
            found_pin_key = (ID)k;
            break;
        }
    }
    if (!found_pin_key) return NULL;

    int32_t pin = (int32_t)as_fixnum(found_pin_key);
    (void)gpio_isr_handler_remove((gpio_num_t)pin);
    ASSIGN(g_gpio_watchers, map_remove(g_gpio_watchers, found_pin_key));
    return NULL;
}

// ESP32: no gpio-simulate! (used by macOS tests). Keep as no-op.
ID native_gpio_simulate(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "gpio-simulate!");
    (void)args;
    return NULL;
}

#endif // ESP32_BUILD

