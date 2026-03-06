// ESP32 GPIO integration for Tiny-CLJ.
//
// Build constraints:
// - This file must only be compiled for ESP32 targets (see CMake target_sources).
// - Guarded by ESP32_BUILD as an extra safety net.
//
// Design:
// - Map-based watcher storage: g_gpio_watchers maps pin->watcher_map
// - watcher_map: {:pin <fixnum> :callback-fn <fn> :watcher-id <fixnum>}
// - ISR retains only callback-fn; thread context drains into generic event-loop ingress maps.
//
// Defaults (see plan "answer_questions"):
// - Pin config: GPIO input, interrupt on any edge, no pull-up/down.
// - Debouncing: none (userland can filter).
// - Errors: throw exceptions (IO / illegal-state / arity).
// - Multi-core: rely on ESP-IDF defaults.

#ifdef ESP32_BUILD

#include "gpio_esp32.h"
#include "gpio_common.h"

#include "exception.h"
#include "function.h"   // is_callable
#include "map.h"
#include "memory.h"
#include "symbol.h"
#include "value.h"
#include "event_loop.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_attr.h>
#include <stdatomic.h>

// Map: pin (fixnum) -> watcher_map
static CljPersistentMap *g_gpio_watchers = NULL;
static int32_t g_next_watcher_id = 1;

// Watcher map keys (interned keywords; singletons)
static CljSymbol *KW_SOURCE = NULL;
static CljSymbol *KW_KIND = NULL;
static CljSymbol *KW_PIN = NULL;
static CljSymbol *KW_VALUE = NULL;
static CljSymbol *KW_GPIO = NULL;
static CljSymbol *KW_EDGE = NULL;

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
static _Atomic bool g_gpio_drain_requested = false;
static _Atomic uint32_t g_gpio_event_drop_count = 0;

// PWM bindings (LEDC): static table, no heap.
typedef struct {
    int32_t pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
    bool configured;
} GpioPwmBinding;

#if defined(LEDC_CHANNEL_MAX)
enum { GPIO_PWM_BINDING_CAP = LEDC_CHANNEL_MAX };
#else
enum { GPIO_PWM_BINDING_CAP = 8 };
#endif
#if defined(LEDC_TIMER_MAX)
enum { GPIO_PWM_TIMER_CAP = LEDC_TIMER_MAX };
#else
enum { GPIO_PWM_TIMER_CAP = 4 };
#endif
enum { GPIO_PWM_MAX_FREQ_HZ = 100000 };

static GpioPwmBinding g_pwm_bindings[GPIO_PWM_BINDING_CAP];
static bool g_pwm_bindings_initialized = false;

/** Ensure watcher map and keywords are initialized (once). */
static inline void gpio_ensure_initialized(void) {
    if (g_gpio_watchers) return;
    g_gpio_watchers = make_map(4, STRONG);
    KW_SOURCE = intern_symbol_global(":source");
    KW_KIND = intern_symbol_global(":kind");
    KW_PIN = intern_symbol_global(":pin");
    KW_VALUE = intern_symbol_global(":value");
    KW_GPIO = intern_symbol_global(":gpio");
    KW_EDGE = intern_symbol_global(":edge");
}

static inline void gpio_pwm_bindings_init(void) {
    if (g_pwm_bindings_initialized) return;
    for (int i = 0; i < GPIO_PWM_BINDING_CAP; i++) {
        g_pwm_bindings[i].pin = -1;
        g_pwm_bindings[i].channel = (ledc_channel_t)i;
        g_pwm_bindings[i].timer = (ledc_timer_t)(i % GPIO_PWM_TIMER_CAP);
        g_pwm_bindings[i].configured = false;
    }
    g_pwm_bindings_initialized = true;
}

static inline GpioPwmBinding *gpio_pwm_binding_find_by_pin(int32_t pin) {
    for (int i = 0; i < GPIO_PWM_BINDING_CAP; i++) {
        if (g_pwm_bindings[i].pin == pin) return &g_pwm_bindings[i];
    }
    return NULL;
}

static inline GpioPwmBinding *gpio_pwm_binding_acquire(int32_t pin) {
    GpioPwmBinding *existing = gpio_pwm_binding_find_by_pin(pin);
    if (existing) return existing;
    for (int i = 0; i < GPIO_PWM_BINDING_CAP; i++) {
        if (g_pwm_bindings[i].pin < 0) {
            g_pwm_bindings[i].pin = pin;
            g_pwm_bindings[i].configured = false;
            return &g_pwm_bindings[i];
        }
    }
    return NULL;
}

static inline void gpio_pwm_binding_release(GpioPwmBinding *binding) {
    if (!binding) return;
    binding->pin = -1;
    binding->configured = false;
}

/** Push one event from ISR into ring; callback_fn_retained is consumed. Returns false if ring full. */
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

/** Pop next event from ring into *out. Returns false if empty. */
static inline bool gpio_event_ring_pop(GpioEvent *out) {
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_acquire);
    if (r == w) return false;

    *out = g_gpio_events[r];
    atomic_store_explicit(&g_gpio_event_r, (r + 1u) % GPIO_EVENT_RING_CAP, memory_order_release);
    return true;
}

/** ISR-side signal only: request one drain in thread context. */
static inline void gpio_request_drain_from_isr(void) {
    atomic_store_explicit(&g_gpio_drain_requested, true, memory_order_release);
}

static ID gpio_make_watch_event(int32_t pin, int32_t value) {
    gpio_ensure_initialized();
    return make_map_from_kv(4,
                            KW_SOURCE, KW_GPIO,
                            KW_KIND, KW_EDGE,
                            KW_PIN, fixnum(pin),
                            KW_VALUE, fixnum(value == 0 ? 0 : 1));
}

/** Promote pending ISR events into generic event-loop ingress calls. */
void gpio_esp32_poll_drain(void) {
    gpio_ensure_initialized();

    bool requested = atomic_load_explicit(&g_gpio_drain_requested, memory_order_acquire);
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_acquire);
    if (!requested && r == w) {
        return;
    }

    atomic_store_explicit(&g_gpio_drain_requested, false, memory_order_release);

    GpioEvent ev;
    while (gpio_event_ring_pop(&ev)) {
        ID event_map = gpio_make_watch_event(ev.pin, ev.value);
        bool enqueued = false;
        if (event_map) {
            enqueued = event_loop_enqueue_ingress_call(ev.callback_fn, event_map);
            RELEASE(event_map);
        }
        if (!enqueued) {
            atomic_fetch_add_explicit(&g_gpio_event_drop_count, 1u, memory_order_relaxed);
        }
        RELEASE(ev.callback_fn);
    }

    if (atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed) !=
        atomic_load_explicit(&g_gpio_event_w, memory_order_acquire)) {
        atomic_store_explicit(&g_gpio_drain_requested, true, memory_order_release);
    }
}

uint32_t gpio_esp32_get_event_drop_count(void) {
    return atomic_load_explicit(&g_gpio_event_drop_count, memory_order_relaxed);
}

/** ISR: read pin level, retain callback, push event, schedule drain. */
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
        atomic_fetch_add_explicit(&g_gpio_event_drop_count, 1u, memory_order_relaxed);
        RELEASE(retained_cb);
        return;
    }
    gpio_request_drain_from_isr();
}

/** Drain all queued GPIO events from thread context. */
/** Install ESP32 GPIO ISR service once; throw on failure. */
static inline void gpio_ensure_isr_service(void) {
    if (g_gpio_isr_service_installed) return;
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        g_gpio_isr_service_installed = true;
        return;
    }
    throw_exception(EXCEPTION_RUNTIME, "gpio_install_isr_service failed", __FILE__, __LINE__, 0);
}

/**
 * @brief Register a watcher for a GPIO pin (Clojure: gpio-watch).
 * @param args [pin fixnum, callback fn]
 * @return Watcher ID (fixnum) or NULL on error
 */
ID native_gpio_watch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "gpio-watch");

    ID callback = args[1];
    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-watch", args[0], &pin);
    if (!is_callable(callback)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-watch: callback must be callable", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_ensure_initialized();

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
    map_assoc_inplace(&g_gpio_watchers, pin_key, watcher_map);

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        map_remove_inplace(&g_gpio_watchers, pin_key);
        RELEASE(watcher_map);
        throw_exception(EXCEPTION_RUNTIME, "gpio-watch: gpio_config failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_ensure_isr_service();
    err = gpio_isr_handler_add((gpio_num_t)pin, gpio_isr_handler, (void*)watcher_map);
    if (err != ESP_OK) {
        map_remove_inplace(&g_gpio_watchers, pin_key);
        RELEASE(watcher_map);
        throw_exception(EXCEPTION_RUNTIME, "gpio-watch: gpio_isr_handler_add failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    RELEASE(watcher_map);
    return fixnum(watcher_id);
}

/**
 * @brief Remove watcher by ID (Clojure: gpio-unwatch).
 * @param args [watcher-id fixnum]
 * @return nil
 */
ID native_gpio_unwatch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "gpio-unwatch");

    ID wid_val = args[0];
    if (!wid_val || !is_fixnum(wid_val)) return NULL;
    int32_t wid = (int32_t)as_fixnum(wid_val);

    if (!g_gpio_watchers) return NULL;

    ID found_pin_key = NULL;
    MAP_FOR_EACH(g_gpio_watchers, k, v) {
        ID watcher_map = v;
        ID stored_wid = map_get_sentinel(watcher_map, (ID)SYM_KW_WATCHER_ID, NOT_FOUND);
        if (stored_wid != NOT_FOUND && stored_wid && is_fixnum(stored_wid) && (int32_t)as_fixnum(stored_wid) == wid) {
            found_pin_key = k;
            break;
        }
    }
    if (!found_pin_key) return NULL;

    int32_t pin = (int32_t)as_fixnum(found_pin_key);
    (void)gpio_isr_handler_remove((gpio_num_t)pin);
    map_remove_inplace(&g_gpio_watchers, found_pin_key);
    return NULL;
}

/**
 * @brief Set a GPIO pin output level (Clojure: gpio-write!).
 * @param args [pin fixnum, level fixnum]
 * @return nil
 */
ID native_gpio_write(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "gpio-write!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-write!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("gpio-write!", args[1], &level);
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-write!: invalid output pin", __FILE__, __LINE__, 0);
        return NULL;
    }

    // Force output mode before writing to survive mode drift across watchers/reconfigurations.
    esp_err_t dir_err = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    if (dir_err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-write!: gpio_set_direction failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    esp_err_t err = gpio_set_level((gpio_num_t)pin, (uint32_t)level);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-write!: gpio_set_level failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    return NULL;
}

/**
 * @brief Read a GPIO pin level (Clojure: gpio-read).
 * @param args [pin fixnum]
 * @return fixnum 0|1
 */
ID native_gpio_read(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "gpio-read");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-read", args[0], &pin);
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-read: invalid pin", __FILE__, __LINE__, 0);
        return NULL;
    }

    int level = gpio_get_level((gpio_num_t)pin);
    return fixnum((level == 0) ? 0 : 1);
}

/**
 * @brief Configure PWM output for pin (Clojure: gpio-pwm!).
 * @param args [pin fixnum, freq-hz fixnum, duty fixnum 0..255]
 * @return nil
 */
ID native_gpio_pwm(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 3, "gpio-pwm!");

    int32_t pin = 0;
    int32_t freq_hz = 0;
    int32_t duty8 = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-pwm!", args[0], &pin);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("gpio-pwm!", "freq-hz", args[1], 1, GPIO_PWM_MAX_FREQ_HZ, &freq_hz);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("gpio-pwm!", "duty", args[2], 0, 255, &duty8);

    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-pwm!: invalid output pin", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_pwm_bindings_init();
    GpioPwmBinding *binding = gpio_pwm_binding_acquire(pin);
    if (!binding) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-pwm!: no free PWM channel", __FILE__, __LINE__, 0);
        return NULL;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = binding->timer,
        .freq_hz = (uint32_t)freq_hz,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-pwm!: ledc_timer_config failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!binding->configured) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num = pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = binding->channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = binding->timer,
            .duty = 0,
            .hpoint = 0
        };
        err = ledc_channel_config(&ch_cfg);
        if (err != ESP_OK) {
            throw_exception(EXCEPTION_RUNTIME, "gpio-pwm!: ledc_channel_config failed", __FILE__, __LINE__, 0);
            return NULL;
        }
        binding->configured = true;
    }

    uint32_t duty10 = ((uint32_t)duty8 * 1023u + 127u) / 255u;
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, binding->channel, duty10);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-pwm!: ledc_set_duty failed", __FILE__, __LINE__, 0);
        return NULL;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, binding->channel);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-pwm!: ledc_update_duty failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    return NULL;
}

/**
 * @brief Stop PWM output for pin (Clojure: gpio-pwm-stop!).
 * @param args [pin fixnum]
 * @return nil
 */
ID native_gpio_pwm_stop(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "gpio-pwm-stop!");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-pwm-stop!", args[0], &pin);
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-pwm-stop!: invalid output pin", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_pwm_bindings_init();
    GpioPwmBinding *binding = gpio_pwm_binding_find_by_pin(pin);
    if (!binding) return NULL;

    esp_err_t err = ledc_stop(LEDC_LOW_SPEED_MODE, binding->channel, 0);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-pwm-stop!: ledc_stop failed", __FILE__, __LINE__, 0);
        return NULL;
    }
    err = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    if (err == ESP_OK) {
        (void)gpio_set_level((gpio_num_t)pin, 0u);
    }

    gpio_pwm_binding_release(binding);
    return NULL;
}

/** No-op on ESP32 (macOS tests use gpio-simulate! for mock). */
ID native_gpio_simulate(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "gpio-simulate!");
    (void)args;
    return NULL;
}

#endif // ESP32_BUILD
