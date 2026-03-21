#include "gpio.h"

#include "builtins_gpio.h"
#include "event_loop.h"
#include "function.h"
#include "gpio_common.h"
#include "map.h"
#include "memory.h"
#include "platform.h"
#include "symbol_cache.h"
#include "symbol.h"
#include "value.h"
#ifdef ESP32_BUILD
#include <driver/gpio.h>
#include "gpio_esp32.h"
#else
#include "gpio_host.h"
#endif

#include <limits.h>
#include <stdlib.h>

static CljPersistentMap *g_gpio_watchers = NULL;
static CljPersistentMap *g_gpio_pin_levels = NULL;
static CljPersistentMap *g_gpio_analog_levels = NULL;
static CljPersistentMap *g_gpio_pin_modes = NULL;
static CljSymbol *KW_SIGNAL = NULL;
static CljSymbol *KW_SOURCE = NULL;
static CljSymbol *KW_KIND = NULL;
static CljSymbol *KW_PIN = NULL;
static CljSymbol *KW_VALUE = NULL;
static CljSymbol *KW_GPIO = NULL;
static CljSymbol *KW_EDGE = NULL;
static CljSymbol *KW_BUTTON = NULL;
static CljSymbol *KW_SENSOR = NULL;
static CljSymbol *KW_ID = NULL;
static CljSymbol *KW_HELD_MS = NULL;
static CljSymbol *KW_PRESSED_MS = NULL;
static CljSymbol *KW_DELTA = NULL;
static CljSymbol *KW_ACTIVE = NULL;
static CljSymbol *KW_BUTTON_DOWN = NULL;
static CljSymbol *KW_BUTTON_UP = NULL;
static CljSymbol *KW_BUTTON_CLICK = NULL;
static CljSymbol *KW_BUTTON_HOLD = NULL;
static CljSymbol *KW_SENSOR_CHANGE = NULL;
static CljSymbol *KW_SENSOR_THRESHOLD_CROSSED = NULL;
static CljSymbol *KW_SENSOR_ACTIVE = NULL;
static CljSymbol *KW_SENSOR_INACTIVE = NULL;
static CljSymbol *KW_INPUT_RUNTIME = NULL;
static CljSymbol *SYM_GPIO_INPUT_RUNTIME_TICK = NULL;
static const SymbolCacheEntry g_gpio_runtime_symbol_cache[] = {
    {&KW_SOURCE, ":source", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_KIND, ":kind", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_PIN, ":pin", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_VALUE, ":value", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_GPIO, ":gpio", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_EDGE, ":edge", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_BUTTON, ":button", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_SENSOR, ":sensor", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_ID, ":id", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_HELD_MS, ":held-ms", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_PRESSED_MS, ":pressed-ms", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_DELTA, ":delta", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_ACTIVE, ":active", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_BUTTON_DOWN, ":button/down", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_BUTTON_UP, ":button/up", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_BUTTON_CLICK, ":button/click", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_BUTTON_HOLD, ":button/hold", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_SENSOR_CHANGE, ":sensor/change", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_SENSOR_THRESHOLD_CROSSED, ":sensor/threshold-crossed", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_SENSOR_ACTIVE, ":sensor/active", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_SENSOR_INACTIVE, ":sensor/inactive", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&KW_INPUT_RUNTIME, ":gpio-input-runtime", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&SYM_GPIO_INPUT_RUNTIME_TICK, "tiny-clj.gpio/input-runtime-tick!", SYMBOL_CACHE_SCOPE_GLOBAL},
};
static int32_t g_next_watcher_id = 1;

enum {
    GPIO_C_CALLBACK_CAP = 48,
    GPIO_BUTTON_WATCH_CAP = 24,
    GPIO_SENSOR_WATCH_CAP = 16,
    GPIO_INPUT_RUNTIME_TICK_MS = 5
};

typedef struct {
    bool active;
    int32_t pin;
    GpioCRawCallback callback;
    void *ctx;
} GpioCRawCallbackEntry;

typedef struct {
    bool active;
    ID control_id;
    ID callback;
    int32_t pin;
    uint32_t debounce_ms;
    uint32_t hold_ms;
    int8_t active_level;
    int8_t raw_level;
    int8_t stable_level;
    uint32_t last_change_ms;
    uint32_t down_at_ms;
    bool pressed;
    bool hold_fired;
} GpioButtonWatch;

typedef struct {
    bool active;
    ID sensor_id;
    ID callback;
    int32_t pin;
    ID signal;
    int32_t range_min;
    int32_t range_max;
    int32_t threshold;
    int32_t hysteresis;
    uint32_t stable_ms;
    int32_t outlier_delta_max;
    uint32_t sample_period_ms;
    uint32_t last_sample_ms;
    int32_t last_sample;
    bool initialized;
    bool active_state;
    bool candidate_valid;
    bool candidate_active;
    uint32_t candidate_since_ms;
    int32_t last_emitted_value;
    bool last_emitted_valid;
} GpioSensorWatch;

static GpioCRawCallbackEntry g_gpio_c_callbacks[GPIO_C_CALLBACK_CAP];
static GpioButtonWatch g_gpio_button_watches[GPIO_BUTTON_WATCH_CAP];
static GpioSensorWatch g_gpio_sensor_watches[GPIO_SENSOR_WATCH_CAP];
static ID g_gpio_input_timer_key = NULL;
static ID g_gpio_input_timer_fn = NULL;

static inline uint32_t gpio_runtime_elapsed_ms(uint32_t start_ms, uint32_t end_ms) {
    return (end_ms >= start_ms) ? (end_ms - start_ms) : (86400000u - start_ms + end_ms);
}

static inline void gpio_runtime_release_button_watch(GpioButtonWatch *watch) {
    if (!watch) {
        return;
    }
    ASSIGN(watch->control_id, NULL);
    ASSIGN(watch->callback, NULL);
    watch->active = false;
    watch->pin = -1;
    watch->debounce_ms = 0u;
    watch->hold_ms = 0u;
    watch->active_level = 0;
    watch->raw_level = 0;
    watch->stable_level = 0;
    watch->last_change_ms = 0u;
    watch->down_at_ms = 0u;
    watch->pressed = false;
    watch->hold_fired = false;
}

static inline void gpio_runtime_release_sensor_watch(GpioSensorWatch *watch) {
    if (!watch) {
        return;
    }
    ASSIGN(watch->sensor_id, NULL);
    ASSIGN(watch->callback, NULL);
    watch->active = false;
    watch->pin = -1;
    ASSIGN(watch->signal, NULL);
    watch->range_min = 0;
    watch->range_max = 0;
    watch->threshold = -1;
    watch->hysteresis = 0;
    watch->stable_ms = 0u;
    watch->outlier_delta_max = -1;
    watch->sample_period_ms = 0u;
    watch->last_sample_ms = 0u;
    watch->last_sample = 0;
    watch->initialized = false;
    watch->active_state = false;
    watch->candidate_valid = false;
    watch->candidate_active = false;
    watch->candidate_since_ms = 0u;
    watch->last_emitted_value = 0;
    watch->last_emitted_valid = false;
}

static ID gpio_runtime_input_tick_native(ID *args, unsigned int argc);
static void gpio_runtime_input_timer_refresh(void);
static ID gpio_runtime_make_button_event(GpioButtonWatch *watch, ID kind, int32_t pressed_ms, int32_t held_ms);
static ID gpio_runtime_make_sensor_event(GpioSensorWatch *watch, ID kind, int32_t value, int32_t delta, bool has_active, bool active_value);
static void gpio_runtime_emit_button_event(GpioButtonWatch *watch, ID kind, int32_t pressed_ms, int32_t held_ms);
static void gpio_runtime_emit_sensor_event(GpioSensorWatch *watch, ID kind, int32_t value, int32_t delta, bool has_active, bool active_value);
static void gpio_button_raw_callback(int32_t pin, int32_t level, void *ctx);

static inline void gpio_runtime_ensure_initialized(void) {
    if (g_gpio_watchers) return;
    g_gpio_watchers = make_map(4, STRONG);
    g_gpio_pin_levels = make_map(8, STRONG);
    g_gpio_analog_levels = make_map(8, STRONG);
    g_gpio_pin_modes = make_map(8, STRONG);
    (void)symbol_cache_init_global(g_gpio_runtime_symbol_cache,
                                   sizeof(g_gpio_runtime_symbol_cache) / sizeof(g_gpio_runtime_symbol_cache[0]));
    KW_SIGNAL = SYM_KW_SIGNAL;
    g_gpio_input_timer_key = KW_INPUT_RUNTIME;
}

#ifdef ESP32_BUILD
static inline bool gpio_runtime_pin_slot_valid(int32_t pin) {
    return pin >= 0 && pin < GPIO_NUM_MAX;
}
#endif

static ID gpio_runtime_make_watch_event(int32_t pin, int32_t level) {
    gpio_runtime_ensure_initialized();
    return make_map_from_kv(5,
                            KW_SOURCE, KW_GPIO,
                            KW_SIGNAL, SYM_KW_DIGITAL,
                            KW_KIND, KW_EDGE,
                            KW_PIN, fixnum(pin),
                            KW_VALUE, fixnum(level == 0 ? 0 : 1));
}

void gpio_runtime_reset_state(void) {
    ASSIGN(g_gpio_watchers, NULL);
    ASSIGN(g_gpio_pin_levels, NULL);
    ASSIGN(g_gpio_analog_levels, NULL);
    ASSIGN(g_gpio_pin_modes, NULL);
    for (int i = 0; i < GPIO_C_CALLBACK_CAP; i++) {
        g_gpio_c_callbacks[i].active = false;
        g_gpio_c_callbacks[i].pin = -1;
        g_gpio_c_callbacks[i].callback = NULL;
        g_gpio_c_callbacks[i].ctx = NULL;
    }
    for (int i = 0; i < GPIO_BUTTON_WATCH_CAP; i++) {
        gpio_runtime_release_button_watch(&g_gpio_button_watches[i]);
    }
    for (int i = 0; i < GPIO_SENSOR_WATCH_CAP; i++) {
        gpio_runtime_release_sensor_watch(&g_gpio_sensor_watches[i]);
    }
    if (g_gpio_input_timer_key) {
        (void)timer_cancel_named(g_gpio_input_timer_key);
    }
    ASSIGN(g_gpio_input_timer_fn, NULL);
#ifdef ESP32_BUILD
    gpio_esp32_runtime_reset_state();
#endif
    symbol_cache_clear(g_gpio_runtime_symbol_cache,
                       sizeof(g_gpio_runtime_symbol_cache) / sizeof(g_gpio_runtime_symbol_cache[0]));
    KW_SIGNAL = NULL;
    g_gpio_input_timer_key = NULL;
    g_next_watcher_id = 1;
}

bool gpio_runtime_watch_set(int32_t pin, ID callback) {
    gpio_runtime_ensure_initialized();

    CljPersistentMap *watcher_map = make_map_from_kv(3,
                                                     (ID)KW_PIN, fixnum(pin),
                                                     (ID)SYM_KW_CALLBACK_FN, callback,
                                                     (ID)SYM_KW_WATCHER_ID, fixnum(g_next_watcher_id++));
    map_assoc_inplace(&g_gpio_watchers, fixnum(pin), watcher_map);
    RELEASE(watcher_map);
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        g_gpio_pin_state[pin].watcher_registered = true;
    }
#endif
    return true;
}

bool gpio_runtime_watch_clear(int32_t pin) {
    gpio_runtime_ensure_initialized();
    if (map_get((ID)g_gpio_watchers, fixnum(pin)) == NOT_FOUND) {
        return false;
    }
    map_remove_inplace(&g_gpio_watchers, fixnum(pin));
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        g_gpio_pin_state[pin].watcher_registered = false;
    }
#endif
    return true;
}

bool gpio_runtime_pin_has_watcher(int32_t pin) {
    gpio_runtime_ensure_initialized();
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        return g_gpio_pin_state[pin].watcher_registered;
    }
#endif
    return map_get((ID)g_gpio_watchers, fixnum(pin)) != NOT_FOUND;
}

bool gpio_runtime_pin_has_c_callbacks(int32_t pin) {
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        return g_gpio_pin_state[pin].c_callback_count > 0u;
    }
#endif
    for (int i = 0; i < GPIO_C_CALLBACK_CAP; i++) {
        GpioCRawCallbackEntry *entry = &g_gpio_c_callbacks[i];
        if (entry->active && entry->pin == pin && entry->callback) {
            return true;
        }
    }
    return false;
}

bool gpio_runtime_enqueue_watch_event(int32_t pin, int32_t level) {
    gpio_runtime_ensure_initialized();
    ID callback = NULL;
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        callback = g_gpio_pin_state[pin].watcher_callback;
    }
#endif
    if (!callback) {
        ID watcher_map = map_get((ID)g_gpio_watchers, fixnum(pin));
        if (!watcher_map || watcher_map == NOT_FOUND) {
            return false;
        }
        callback = map_get_sentinel(watcher_map, (ID)SYM_KW_CALLBACK_FN, NULL);
        if (!callback || callback == NOT_FOUND) {
            return false;
        }
    }

    ID event_map = gpio_runtime_make_watch_event(pin, level);
    if (!event_map) {
        return false;
    }

    bool enqueued = event_loop_enqueue_ingress_call(callback, event_map);
    RELEASE(event_map);
    return enqueued;
}

bool gpio_runtime_c_callback_add(int32_t pin, GpioCRawCallback callback, void *ctx) {
    gpio_runtime_ensure_initialized();
    if (!callback) {
        return false;
    }
    for (int i = 0; i < GPIO_C_CALLBACK_CAP; i++) {
        GpioCRawCallbackEntry *entry = &g_gpio_c_callbacks[i];
        if (entry->active &&
            entry->pin == pin &&
            entry->callback == callback &&
            entry->ctx == ctx) {
            return true;
        }
    }
    for (int i = 0; i < GPIO_C_CALLBACK_CAP; i++) {
        GpioCRawCallbackEntry *entry = &g_gpio_c_callbacks[i];
        if (!entry->active) {
            entry->active = true;
            entry->pin = pin;
            entry->callback = callback;
            entry->ctx = ctx;
#ifdef ESP32_BUILD
            if (gpio_runtime_pin_slot_valid(pin) &&
                g_gpio_pin_state[pin].c_callback_count < UINT8_MAX) {
                g_gpio_pin_state[pin].c_callback_count++;
            }
            if (!gpio_esp32_input_irq_consumer_acquire(pin)) {
#ifdef ESP32_BUILD
                if (gpio_runtime_pin_slot_valid(pin) &&
                    g_gpio_pin_state[pin].c_callback_count > 0u) {
                    g_gpio_pin_state[pin].c_callback_count--;
                }
#endif
                entry->active = false;
                entry->pin = -1;
                entry->callback = NULL;
                entry->ctx = NULL;
                return false;
            }
#endif
            return true;
        }
    }
    return false;
}

bool gpio_runtime_c_callback_remove(int32_t pin, GpioCRawCallback callback, void *ctx) {
    gpio_runtime_ensure_initialized();
    for (int i = 0; i < GPIO_C_CALLBACK_CAP; i++) {
        GpioCRawCallbackEntry *entry = &g_gpio_c_callbacks[i];
        if (entry->active &&
            entry->pin == pin &&
            entry->callback == callback &&
            entry->ctx == ctx) {
#ifdef ESP32_BUILD
            if (gpio_runtime_pin_slot_valid(pin) &&
                g_gpio_pin_state[pin].c_callback_count > 0u) {
                g_gpio_pin_state[pin].c_callback_count--;
            }
            (void)gpio_esp32_input_irq_consumer_release(pin);
#endif
            entry->active = false;
            entry->pin = -1;
            entry->callback = NULL;
            entry->ctx = NULL;
            return true;
        }
    }
    return false;
}

void gpio_runtime_dispatch_c_callbacks(int32_t pin, int32_t level) {
    gpio_runtime_ensure_initialized();
    for (int i = 0; i < GPIO_C_CALLBACK_CAP; i++) {
        GpioCRawCallbackEntry *entry = &g_gpio_c_callbacks[i];
        if (!entry->active || entry->pin != pin || !entry->callback) {
            continue;
        }
        entry->callback(pin, level, entry->ctx);
    }
}

void gpio_runtime_store_digital_level(int32_t pin, int32_t level) {
    gpio_runtime_ensure_initialized();
    map_assoc_inplace(&g_gpio_pin_levels, fixnum(pin), fixnum(level == 0 ? 0 : 1));
}

void gpio_runtime_store_analog_level(int32_t pin, int32_t value) {
    gpio_runtime_ensure_initialized();
    map_assoc_inplace(&g_gpio_analog_levels, fixnum(pin), fixnum(value));
}

ID gpio_runtime_read_digital_level(int32_t pin) {
    gpio_runtime_ensure_initialized();
    ID value = map_get((ID)g_gpio_pin_levels, fixnum(pin));
    if (!value || value == NOT_FOUND) {
        return fixnum(0);
    }
    return value;
}

ID gpio_runtime_read_analog_level(int32_t pin) {
    gpio_runtime_ensure_initialized();
    ID value = map_get((ID)g_gpio_analog_levels, fixnum(pin));
    if (!value || value == NOT_FOUND) {
        return fixnum(0);
    }
    return value;
}

static ID gpio_pin_mode_entry(int32_t pin) {
    gpio_runtime_ensure_initialized();
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        return g_gpio_pin_state[pin].mode_entry;
    }
#endif
    ID entry = map_get(g_gpio_pin_modes, fixnum(pin));
    if (!entry || entry == NOT_FOUND) {
        return NULL;
    }
    return entry;
}

static inline void gpio_store_pin_mode_entry(int32_t pin, ID entry) {
    gpio_runtime_ensure_initialized();
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        ASSIGN(g_gpio_pin_state[pin].mode_entry, entry);
        return;
    }
#endif
    map_assoc_inplace(&g_gpio_pin_modes, fixnum(pin), entry);
}

static inline void gpio_clear_pin_mode_entry(int32_t pin) {
    gpio_runtime_ensure_initialized();
#ifdef ESP32_BUILD
    if (gpio_runtime_pin_slot_valid(pin)) {
        ASSIGN(g_gpio_pin_state[pin].mode_entry, NULL);
        return;
    }
#endif
    map_remove_inplace(&g_gpio_pin_modes, fixnum(pin));
}

static ID gpio_require_pin_mode_key(int32_t pin, const char *op_name) {
    ID entry = gpio_pin_mode_entry(pin);
    if (!entry) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s: pin %d has no configured mode; call set-pin-mode! first",
                                  op_name, (int)pin);
        return NULL;
    }

    ID mode = map_get_sentinel(entry, SYM_KW_MODE, NULL);
    if (!mode) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "%s: pin %d mode entry is missing :mode",
                                  op_name, (int)pin);
        return NULL;
    }
    return mode;
}

static CljPersistentMap *gpio_make_pin_mode_entry(ID mode_value, ID opts) {
    if (!opts) {
        return make_map_from_kv(1, SYM_KW_MODE, mode_value);
    }

    if (!is_map(opts)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "set-pin-mode!: opts must be map or nil", __FILE__, __LINE__, 0);
        return NULL;
    }

    CljPersistentMap *entry = RETAIN(as_map(opts));
    ASSIGN(entry, map_assoc(entry, SYM_KW_MODE, mode_value));
    return entry;
}

static CljPersistentMap *gpio_make_pwm_pin_mode_entry(int32_t freq_hz, int32_t duty) {
    return make_map_from_kv(3,
                            SYM_KW_MODE, SYM_KW_PWM,
                            SYM_KW_FREQ, fixnum(freq_hz),
                            SYM_KW_DUTY, fixnum(duty));
}

static GpioButtonWatch *gpio_runtime_find_button_watch(ID control_id) {
    for (int i = 0; i < GPIO_BUTTON_WATCH_CAP; i++) {
        GpioButtonWatch *watch = &g_gpio_button_watches[i];
        if (watch->active && watch->control_id == control_id) {
            return watch;
        }
    }
    return NULL;
}

static GpioButtonWatch *gpio_runtime_alloc_button_watch(void) {
    for (int i = 0; i < GPIO_BUTTON_WATCH_CAP; i++) {
        if (!g_gpio_button_watches[i].active) {
            return &g_gpio_button_watches[i];
        }
    }
    return NULL;
}

static GpioSensorWatch *gpio_runtime_find_sensor_watch(ID sensor_id) {
    for (int i = 0; i < GPIO_SENSOR_WATCH_CAP; i++) {
        GpioSensorWatch *watch = &g_gpio_sensor_watches[i];
        if (watch->active && watch->sensor_id == sensor_id) {
            return watch;
        }
    }
    return NULL;
}

static GpioSensorWatch *gpio_runtime_alloc_sensor_watch(void) {
    for (int i = 0; i < GPIO_SENSOR_WATCH_CAP; i++) {
        if (!g_gpio_sensor_watches[i].active) {
            return &g_gpio_sensor_watches[i];
        }
    }
    return NULL;
}

static bool gpio_runtime_has_input_watchers(void) {
    for (int i = 0; i < GPIO_BUTTON_WATCH_CAP; i++) {
        if (g_gpio_button_watches[i].active) {
            return true;
        }
    }
    for (int i = 0; i < GPIO_SENSOR_WATCH_CAP; i++) {
        if (g_gpio_sensor_watches[i].active) {
            return true;
        }
    }
    return false;
}

static void gpio_runtime_input_timer_refresh(void) {
    gpio_runtime_ensure_initialized();
    if (!g_gpio_input_timer_key) {
        g_gpio_input_timer_key = KW_INPUT_RUNTIME;
    }
    if (!g_gpio_input_timer_fn) {
        g_gpio_input_timer_fn = make_named_func(gpio_runtime_input_tick_native,
                                                SYM_GPIO_INPUT_RUNTIME_TICK);
    }
    if (gpio_runtime_has_input_watchers()) {
        (void)timer_upsert_named(g_gpio_input_timer_key, g_gpio_input_timer_fn, 0, true, GPIO_INPUT_RUNTIME_TICK_MS);
    } else if (g_gpio_input_timer_key) {
        (void)timer_cancel_named(g_gpio_input_timer_key);
    }
}

static ID gpio_runtime_make_button_event(GpioButtonWatch *watch, ID kind, int32_t pressed_ms, int32_t held_ms) {
    if (!watch || !watch->control_id || !kind) {
        return NULL;
    }
    CljPersistentMap *event = make_map_from_kv(5,
                                               KW_SOURCE, KW_BUTTON,
                                               KW_ID, watch->control_id,
                                               KW_KIND, kind,
                                               KW_PIN, fixnum(watch->pin),
                                               KW_VALUE, fixnum(watch->stable_level));
    if (!event) {
        return NULL;
    }
    if (pressed_ms >= 0) {
        ASSIGN(event, map_assoc(event, KW_PRESSED_MS, fixnum(pressed_ms)));
    }
    if (held_ms >= 0) {
        ASSIGN(event, map_assoc(event, KW_HELD_MS, fixnum(held_ms)));
    }
    return event;
}

static ID gpio_runtime_make_sensor_event(GpioSensorWatch *watch,
                                         ID kind,
                                         int32_t value,
                                         int32_t delta,
                                         bool has_active,
                                         bool active_value) {
    if (!watch || !watch->sensor_id || !kind) {
        return NULL;
    }
    CljPersistentMap *event = make_map_from_kv(6,
                                               KW_SOURCE, KW_SENSOR,
                                               KW_ID, watch->sensor_id,
                                               KW_KIND, kind,
                                               KW_PIN, fixnum(watch->pin),
                                               KW_VALUE, fixnum(value),
                                               KW_DELTA, fixnum(delta));
    if (!event) {
        return NULL;
    }
    if (has_active) {
        ASSIGN(event, map_assoc(event, KW_ACTIVE, active_value ? clj_true : clj_false));
    }
    return event;
}

static void gpio_runtime_emit_button_event(GpioButtonWatch *watch, ID kind, int32_t pressed_ms, int32_t held_ms) {
    if (!watch || !watch->active || !watch->callback) {
        return;
    }
    ID event = gpio_runtime_make_button_event(watch, kind, pressed_ms, held_ms);
    if (!event) {
        return;
    }
    (void)event_loop_enqueue_ingress_call(watch->callback, event);
    RELEASE(event);
}

static void gpio_runtime_emit_sensor_event(GpioSensorWatch *watch,
                                           ID kind,
                                           int32_t value,
                                           int32_t delta,
                                           bool has_active,
                                           bool active_value) {
    if (!watch || !watch->active || !watch->callback) {
        return;
    }
    ID event = gpio_runtime_make_sensor_event(watch, kind, value, delta, has_active, active_value);
    if (!event) {
        return;
    }
    (void)event_loop_enqueue_ingress_call(watch->callback, event);
    RELEASE(event);
}

static void gpio_button_raw_callback(int32_t pin, int32_t level, void *ctx) {
    (void)pin;
    GpioButtonWatch *watch = (GpioButtonWatch *)ctx;
    if (!watch || !watch->active) {
        return;
    }
    if (watch->raw_level == level) {
        return;
    }
    watch->raw_level = (int8_t)(level == 0 ? 0 : 1);
    watch->last_change_ms = platform_current_time_ms();
}

static bool gpio_runtime_process_button_watch(GpioButtonWatch *watch, uint32_t now_ms) {
    if (!watch || !watch->active) {
        return false;
    }
    if (watch->raw_level != watch->stable_level &&
        gpio_runtime_elapsed_ms(watch->last_change_ms, now_ms) >= watch->debounce_ms) {
        watch->stable_level = watch->raw_level;
        if (watch->stable_level == watch->active_level) {
            watch->pressed = true;
            watch->down_at_ms = now_ms;
            watch->hold_fired = false;
            gpio_runtime_emit_button_event(watch, KW_BUTTON_DOWN, -1, -1);
        } else {
            int32_t pressed_ms = watch->pressed ? (int32_t)gpio_runtime_elapsed_ms(watch->down_at_ms, now_ms) : 0;
            gpio_runtime_emit_button_event(watch, KW_BUTTON_UP, pressed_ms, -1);
            if (watch->pressed && !watch->hold_fired) {
                gpio_runtime_emit_button_event(watch, KW_BUTTON_CLICK, pressed_ms, -1);
            }
            watch->pressed = false;
            watch->hold_fired = false;
        }
    }
    if (watch->pressed &&
        !watch->hold_fired &&
        gpio_runtime_elapsed_ms(watch->down_at_ms, now_ms) >= watch->hold_ms) {
        watch->hold_fired = true;
        gpio_runtime_emit_button_event(
            watch,
            KW_BUTTON_HOLD,
            -1,
            (int32_t)gpio_runtime_elapsed_ms(watch->down_at_ms, now_ms));
    }
    return false;
}

static bool gpio_runtime_process_sensor_watch(GpioSensorWatch *watch, uint32_t now_ms) {
    if (!watch || !watch->active) {
        return false;
    }
    if (gpio_runtime_elapsed_ms(watch->last_sample_ms, now_ms) < watch->sample_period_ms) {
        return false;
    }
    watch->last_sample_ms = now_ms;
    if (watch->signal != SYM_KW_ANALOG) {
        return false;
    }

    ID raw_value = gpio_read_analog(watch->pin);
    if (!raw_value || !is_fixnum(raw_value)) {
        return false;
    }
    int32_t value = as_fixnum(raw_value);
    if (value < watch->range_min) value = watch->range_min;
    if (value > watch->range_max) value = watch->range_max;

    if (watch->initialized &&
        watch->outlier_delta_max >= 0 &&
        abs(value - watch->last_sample) > watch->outlier_delta_max) {
        return false;
    }

    int32_t delta = watch->initialized ? (value - watch->last_sample) : 0;
    watch->last_sample = value;
    watch->initialized = true;

    if (!watch->last_emitted_valid || value != watch->last_emitted_value) {
        gpio_runtime_emit_sensor_event(watch, KW_SENSOR_CHANGE, value, delta, false, false);
        watch->last_emitted_value = value;
        watch->last_emitted_valid = true;
    }

    if (watch->threshold < 0) {
        watch->candidate_valid = false;
        return false;
    }

    bool desired_active = watch->active_state
                              ? (value > (watch->threshold - watch->hysteresis))
                              : (value >= watch->threshold);
    if (desired_active == watch->active_state) {
        watch->candidate_valid = false;
        return false;
    }

    if (!watch->candidate_valid || watch->candidate_active != desired_active) {
        watch->candidate_valid = true;
        watch->candidate_active = desired_active;
        watch->candidate_since_ms = now_ms;
        return false;
    }

    if (gpio_runtime_elapsed_ms(watch->candidate_since_ms, now_ms) < watch->stable_ms) {
        return false;
    }

    watch->active_state = desired_active;
    watch->candidate_valid = false;
    gpio_runtime_emit_sensor_event(watch,
                                   KW_SENSOR_THRESHOLD_CROSSED,
                                   value,
                                   delta,
                                   true,
                                   watch->active_state);
    gpio_runtime_emit_sensor_event(watch,
                                   watch->active_state
                                       ? KW_SENSOR_ACTIVE
                                       : KW_SENSOR_INACTIVE,
                                   value,
                                   delta,
                                   true,
                                   watch->active_state);
    return false;
}

static bool gpio_runtime_process_input_tick(void) {
    uint32_t now_ms = platform_current_time_ms();
    for (int i = 0; i < GPIO_BUTTON_WATCH_CAP; i++) {
        (void)gpio_runtime_process_button_watch(&g_gpio_button_watches[i], now_ms);
    }
    for (int i = 0; i < GPIO_SENSOR_WATCH_CAP; i++) {
        (void)gpio_runtime_process_sensor_watch(&g_gpio_sensor_watches[i], now_ms);
    }
    return gpio_runtime_has_input_watchers();
}

static ID gpio_runtime_input_tick_native(ID *args, unsigned int argc) {
    (void)args;
    if (argc != 0) {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "Wrong number of args (%u) passed to: tiny-clj.gpio/input-runtime-tick!", argc);
        return NULL;
    }
    (void)gpio_runtime_process_input_tick();
    return NULL;
}

static void gpio_release_mode_resources_for_entry(int32_t pin, ID entry) {
    if (!entry) {
        return;
    }

    ID mode = map_get_sentinel(entry, SYM_KW_MODE, NULL);
    if (mode == SYM_KW_PWM) {
        (void)gpio_pwm_stop(pin);
    } else if (mode == SYM_KW_DAC) {
        (void)gpio_release_analog(pin);
    }
}

bool gpio_watch_set(int32_t pin, ID callback) {
#ifdef ESP32_BUILD
    return gpio_esp32_watch_set(pin, callback);
#else
    return gpio_host_watch_set(pin, callback);
#endif
}

bool gpio_watch_clear(int32_t pin) {
#ifdef ESP32_BUILD
    return gpio_esp32_watch_clear(pin);
#else
    return gpio_host_watch_clear(pin);
#endif
}

bool gpio_write_digital(int32_t pin, int32_t level) {
#ifdef ESP32_BUILD
    return gpio_esp32_write_digital(pin, level);
#else
    return gpio_host_write_digital(pin, level);
#endif
}

bool gpio_write_analog(int32_t pin, int32_t value) {
#ifdef ESP32_BUILD
    return gpio_esp32_write_analog(pin, value);
#else
    return gpio_host_write_analog(pin, value);
#endif
}

bool gpio_release_analog(int32_t pin) {
#ifdef ESP32_BUILD
    return gpio_esp32_release_analog(pin);
#else
    return gpio_host_release_analog(pin);
#endif
}

ID gpio_read_digital(int32_t pin) {
#ifdef ESP32_BUILD
    return gpio_esp32_read_digital(pin);
#else
    return gpio_host_read_digital(pin);
#endif
}

ID gpio_read_analog(int32_t pin) {
#ifdef ESP32_BUILD
    return gpio_esp32_read_analog(pin);
#else
    return gpio_host_read_analog(pin);
#endif
}

bool gpio_pwm_start_or_update(int32_t pin, int32_t freq_hz, int32_t duty) {
#ifdef ESP32_BUILD
    return gpio_esp32_pwm_start_or_update(pin, freq_hz, duty);
#else
    return gpio_host_pwm_start_or_update(pin, freq_hz, duty);
#endif
}

bool gpio_pwm_stop(int32_t pin) {
#ifdef ESP32_BUILD
    return gpio_esp32_pwm_stop(pin);
#else
    return gpio_host_pwm_stop(pin);
#endif
}

bool gpio_simulate_digital(int32_t pin, int32_t level) {
#ifdef ESP32_BUILD
    return gpio_esp32_simulate_digital(pin, level);
#else
    return gpio_host_simulate_digital(pin, level);
#endif
}

bool gpio_simulate_analog(int32_t pin, int32_t value) {
#ifdef ESP32_BUILD
    return gpio_esp32_simulate_analog(pin, value);
#else
    return gpio_host_simulate_analog(pin, value);
#endif
}

void gpio_poll_drain(void) {
    (void)gpio_runtime_process_input_tick();
#ifdef ESP32_BUILD
    gpio_esp32_poll_drain();
#endif
}

uint32_t gpio_get_event_drop_count(void) {
#ifdef ESP32_BUILD
    return gpio_esp32_get_event_drop_count();
#else
    return 0u;
#endif
}

ID native_gpio_set_pin_mode(ID *args, unsigned int argc) {
    if (argc < 2 || argc > 3) {
        throw_exception_formatted(EXCEPTION_ARITY, __FILE__, __LINE__, 0,
                                  "Wrong number of args (%u) passed to: set-pin-mode!", argc);
        return NULL;
    }

    gpio_runtime_ensure_initialized();

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("set-pin-mode!", args[0], &pin);
    ID mode = args[1];
    ID opts = (argc == 3) ? args[2] : NULL;
    ID previous_entry = gpio_pin_mode_entry(pin);

    if (!mode) {
        gpio_release_mode_resources_for_entry(pin, previous_entry);
        gpio_clear_pin_mode_entry(pin);
        return NULL;
    }

    gpio_release_mode_resources_for_entry(pin, previous_entry);

    if (mode == SYM_KW_PWM) {
        if (!opts || !is_map(opts)) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "set-pin-mode! :pwm requires opts map", __FILE__, __LINE__, 0);
            return NULL;
        }

        ID freq_value = map_get_sentinel(opts, SYM_KW_FREQ, NULL);
        ID duty_value = map_get_sentinel(opts, SYM_KW_DUTY, NULL);
        if (!freq_value) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "set-pin-mode! :pwm requires :freq in opts map", __FILE__, __LINE__, 0);
            return NULL;
        }
        if (!duty_value) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "set-pin-mode! :pwm requires :duty in opts map", __FILE__, __LINE__, 0);
            return NULL;
        }

        int32_t freq_hz = 0;
        int32_t duty = 0;
        GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("set-pin-mode!", "freq", freq_value, 1, INT_MAX, &freq_hz);
        GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("set-pin-mode!", "duty", duty_value, 0, 255, &duty);
        (void)gpio_pwm_start_or_update(pin, freq_hz, duty);

        CljPersistentMap *entry = gpio_make_pwm_pin_mode_entry(freq_hz, duty);
        gpio_store_pin_mode_entry(pin, entry);
        RELEASE(entry);
        return NULL;
    }

    if (mode == SYM_KW_INPUT ||
        mode == SYM_KW_ADC ||
        mode == SYM_KW_DAC ||
        mode == SYM_KW_OUTPUT) {
        CljPersistentMap *entry = gpio_make_pin_mode_entry(mode, opts);
        if (!entry) {
            return NULL;
        }
        gpio_store_pin_mode_entry(pin, entry);
        RELEASE(entry);
        return NULL;
    }

    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "set-pin-mode! unknown mode: %s",
                              is_symbol(mode) ? as_symbol(mode)->cname : "<non-symbol>");
    return NULL;
}

ID native_gpio_pin_mode(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "pin-mode");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("pin-mode", args[0], &pin);
    return gpio_pin_mode_entry(pin);
}

ID native_gpio_pin_read(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "pin-read");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("pin-read", args[0], &pin);
    ID mode = gpio_require_pin_mode_key(pin, "pin-read");
    if (!mode) {
        return NULL;
    }

    if (mode == SYM_KW_INPUT) {
        return gpio_read_digital(pin);
    }
    if (mode == SYM_KW_ADC) {
        return gpio_read_analog(pin);
    }

    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "pin-read: unsupported mode %s for pin %d",
                              is_symbol(mode) ? as_symbol(mode)->cname : "<non-symbol>",
                              (int)pin);
    return NULL;
}

ID native_gpio_pin_write(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "pin-write");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("pin-write", args[0], &pin);
    ID mode = gpio_require_pin_mode_key(pin, "pin-write");
    if (!mode) {
        return NULL;
    }

    if (mode == SYM_KW_OUTPUT) {
        int32_t level = 0;
        GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("pin-write", args[1], &level);
        (void)gpio_write_digital(pin, level);
        return NULL;
    }

    if (mode == SYM_KW_DAC) {
        int32_t value = 0;
        GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("pin-write", "value", args[1], 0, 255, &value);
        (void)gpio_write_analog(pin, value);
        return NULL;
    }

    throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                              "pin-write: unsupported mode %s for pin %d",
                              is_symbol(mode) ? as_symbol(mode)->cname : "<non-symbol>",
                              (int)pin);
    return NULL;
}

ID native_gpio_watch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "watch");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("watch", args[0], &pin);
    ID callback = args[1];

    if (!callback) {
        (void)gpio_watch_clear(pin);
        return NULL;
    }

    if (!is_callable(callback)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "watch: callback must be callable or nil", __FILE__, __LINE__, 0);
        return NULL;
    }

    (void)gpio_watch_set(pin, callback);
    return NULL;
}

ID native_button_watch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 6, "watch-native");

    gpio_runtime_ensure_initialized();

    ID control_id = args[0];
    if (!control_id || !is_symbol(control_id)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "button/watch-native: id must be keyword or symbol", __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t pin = 0;
    int32_t active_level = 0;
    int32_t debounce_ms = 0;
    int32_t hold_ms = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("button/watch-native", args[1], &pin);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("button/watch-native", "active-level", args[2], 0, 1, &active_level);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("button/watch-native", "debounce-ms", args[3], 0, 5000, &debounce_ms);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("button/watch-native", "hold-ms", args[4], 1, 60000, &hold_ms);

    ID callback = args[5];
    if (callback && !is_callable(callback)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "button/watch-native: callback must be callable or nil", __FILE__, __LINE__, 0);
        return NULL;
    }

    GpioButtonWatch *watch = gpio_runtime_find_button_watch(control_id);
    if (watch && watch->active) {
        (void)gpio_runtime_c_callback_remove(watch->pin, gpio_button_raw_callback, watch);
        gpio_runtime_release_button_watch(watch);
    }

    if (!callback) {
        gpio_runtime_input_timer_refresh();
        return NULL;
    }

    if (!watch) {
        watch = gpio_runtime_alloc_button_watch();
    }
    if (!watch) {
        throw_exception(EXCEPTION_RUNTIME, "button/watch-native: no free button slots", __FILE__, __LINE__, 0);
        return NULL;
    }

    ID current_level_id = gpio_read_digital(pin);
    int32_t current_level = 0;
    if (current_level_id && is_fixnum(current_level_id)) {
        current_level = as_fixnum(current_level_id) == 0 ? 0 : 1;
    }

    watch->active = true;
    ASSIGN(watch->control_id, control_id);
    ASSIGN(watch->callback, callback);
    watch->pin = pin;
    watch->debounce_ms = (uint32_t)debounce_ms;
    watch->hold_ms = (uint32_t)hold_ms;
    watch->active_level = (int8_t)active_level;
    watch->raw_level = (int8_t)current_level;
    watch->stable_level = (int8_t)current_level;
    watch->last_change_ms = platform_current_time_ms();
    watch->pressed = (current_level == active_level);
    watch->down_at_ms = watch->pressed ? platform_current_time_ms() : 0u;
    watch->hold_fired = false;

    if (!gpio_runtime_c_callback_add(pin, gpio_button_raw_callback, watch)) {
        gpio_runtime_release_button_watch(watch);
        throw_exception(EXCEPTION_RUNTIME, "button/watch-native: no free GPIO C callback slots", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_runtime_input_timer_refresh();
    return NULL;
}

ID native_sensor_watch(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 11, "watch-native");

    gpio_runtime_ensure_initialized();

    ID sensor_id = args[0];
    ID signal = args[2];
    if (!sensor_id || !is_symbol(sensor_id)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "sensor/watch-native: id must be keyword or symbol", __FILE__, __LINE__, 0);
        return NULL;
    }
    if (!signal || !is_symbol(signal)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "sensor/watch-native: :signal must be symbol or keyword", __FILE__, __LINE__, 0);
        return NULL;
    }
    if (signal != SYM_KW_ANALOG) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "sensor/watch-native: only :analog is supported in phase 1", __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t pin = 0;
    int32_t threshold = -1;
    int32_t hysteresis = 0;
    int32_t stable_ms = 0;
    int32_t outlier_delta_max = -1;
    int32_t sample_period_ms = 0;
    int32_t range_min = 0;
    int32_t range_max = 4095;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("sensor/watch-native", args[1], &pin);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "threshold", args[3], -1, 4095, &threshold);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "hysteresis", args[4], 0, 4095, &hysteresis);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "stable-ms", args[5], 0, 60000, &stable_ms);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "outlier-delta-max", args[6], -1, 4095, &outlier_delta_max);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "sample-period-ms", args[7], 1, 60000, &sample_period_ms);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "range-min", args[8], 0, 4095, &range_min);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("sensor/watch-native", "range-max", args[9], 0, 4095, &range_max);

    if (range_max < range_min) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "sensor/watch-native: range-max must be >= range-min", __FILE__, __LINE__, 0);
        return NULL;
    }

    ID callback = args[10];
    if (callback && !is_callable(callback)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "sensor/watch-native: callback must be callable or nil", __FILE__, __LINE__, 0);
        return NULL;
    }

    GpioSensorWatch *watch = gpio_runtime_find_sensor_watch(sensor_id);
    if (watch && watch->active) {
        gpio_runtime_release_sensor_watch(watch);
    }

    if (!callback) {
        gpio_runtime_input_timer_refresh();
        return NULL;
    }

    if (!watch) {
        watch = gpio_runtime_alloc_sensor_watch();
    }
    if (!watch) {
        throw_exception(EXCEPTION_RUNTIME, "sensor/watch-native: no free sensor slots", __FILE__, __LINE__, 0);
        return NULL;
    }

    watch->active = true;
    ASSIGN(watch->sensor_id, sensor_id);
    ASSIGN(watch->callback, callback);
    ASSIGN(watch->signal, signal);
    watch->pin = pin;
    watch->range_min = range_min;
    watch->range_max = range_max;
    watch->threshold = threshold;
    watch->hysteresis = hysteresis;
    watch->stable_ms = (uint32_t)stable_ms;
    watch->outlier_delta_max = outlier_delta_max;
    watch->sample_period_ms = (uint32_t)sample_period_ms;
    watch->last_sample_ms = 0u;
    watch->last_sample = 0;
    watch->initialized = false;
    watch->active_state = false;
    watch->candidate_valid = false;
    watch->candidate_active = false;
    watch->candidate_since_ms = 0u;
    watch->last_emitted_value = 0;
    watch->last_emitted_valid = false;

    gpio_runtime_input_timer_refresh();
    return NULL;
}

ID native_gpio_simulate(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "simulate!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("simulate!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("simulate!", args[1], &level);
    (void)gpio_simulate_digital(pin, level);
    return NULL;
}

ID native_gpio_write(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "write!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("write!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("write!", args[1], &level);
    (void)gpio_write_digital(pin, level);
    return NULL;
}

ID native_gpio_read(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "read");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("read", args[0], &pin);
    return gpio_read_digital(pin);
}

ID native_gpio_read_analog(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "read-analog");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("read-analog", args[0], &pin);
    return gpio_read_analog(pin);
}

ID native_gpio_pwm(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 3, "pwm!");

    int32_t pin = 0;
    int32_t freq_hz = 0;
    int32_t duty = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("pwm!", args[0], &pin);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("pwm!", "freq-hz", args[1], 1, INT_MAX, &freq_hz);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("pwm!", "duty", args[2], 0, 255, &duty);
    (void)gpio_pwm_start_or_update(pin, freq_hz, duty);
    return NULL;
}

ID native_gpio_pwm_stop(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 1, "pwm-stop!");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("pwm-stop!", args[0], &pin);
    (void)gpio_pwm_stop(pin);
    return NULL;
}

ID native_gpio_simulate_analog(ID *args, unsigned int argc) {
    CHECK_ARITY(argc, 2, "simulate-analog!");

    int32_t pin = 0;
    int32_t value = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("simulate-analog!", args[0], &pin);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("simulate-analog!", "value", args[1], 0, 4095, &value);
    (void)gpio_simulate_analog(pin, value);
    return NULL;
}
