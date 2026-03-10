#include "gpio.h"

#include "builtins_gpio.h"
#include "event_loop.h"
#include "function.h"
#include "gpio_common.h"
#include "map.h"
#include "memory.h"
#include "symbol.h"
#include "value.h"
#ifdef ESP32_BUILD
#include <driver/gpio.h>
#include "gpio_esp32.h"
#else
#include "gpio_host.h"
#endif

#include <limits.h>

static CljPersistentMap *g_gpio_watchers = NULL;
static CljPersistentMap *g_gpio_pin_levels = NULL;
static CljPersistentMap *g_gpio_analog_levels = NULL;
static CljPersistentMap *g_gpio_pin_modes = NULL;
static CljSymbol *KW_SOURCE = NULL;
static CljSymbol *KW_KIND = NULL;
static CljSymbol *KW_SIGNAL = NULL;
static CljSymbol *KW_PIN = NULL;
static CljSymbol *KW_VALUE = NULL;
static CljSymbol *KW_GPIO = NULL;
static CljSymbol *KW_EDGE = NULL;
static int32_t g_next_watcher_id = 1;

static inline void gpio_runtime_ensure_initialized(void) {
    if (g_gpio_watchers) return;
    g_gpio_watchers = make_map(4, STRONG);
    g_gpio_pin_levels = make_map(8, STRONG);
    g_gpio_analog_levels = make_map(8, STRONG);
    g_gpio_pin_modes = make_map(8, STRONG);
    KW_SOURCE = intern_symbol_global(":source");
    KW_KIND = intern_symbol_global(":kind");
    KW_SIGNAL = SYM_KW_SIGNAL;
    KW_PIN = intern_symbol_global(":pin");
    KW_VALUE = intern_symbol_global(":value");
    KW_GPIO = intern_symbol_global(":gpio");
    KW_EDGE = intern_symbol_global(":edge");
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
#ifdef ESP32_BUILD
    gpio_esp32_runtime_reset_state();
#endif
    KW_SOURCE = NULL;
    KW_KIND = NULL;
    KW_SIGNAL = NULL;
    KW_PIN = NULL;
    KW_VALUE = NULL;
    KW_GPIO = NULL;
    KW_EDGE = NULL;
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
    return true;
}

bool gpio_runtime_watch_clear(int32_t pin) {
    gpio_runtime_ensure_initialized();
    if (map_get((ID)g_gpio_watchers, fixnum(pin)) == NOT_FOUND) {
        return false;
    }
    map_remove_inplace(&g_gpio_watchers, fixnum(pin));
    return true;
}

bool gpio_runtime_enqueue_watch_event(int32_t pin, int32_t level) {
    gpio_runtime_ensure_initialized();

    ID watcher_map = map_get((ID)g_gpio_watchers, fixnum(pin));
    if (!watcher_map || watcher_map == NOT_FOUND) {
        return false;
    }

    ID callback = map_get_sentinel(watcher_map, (ID)SYM_KW_CALLBACK_FN, NULL);
    if (!callback || callback == NOT_FOUND) {
        return false;
    }

    ID event_map = gpio_runtime_make_watch_event(pin, level);
    if (!event_map) {
        return false;
    }

    bool enqueued = event_loop_enqueue_ingress_call(callback, event_map);
    RELEASE(event_map);
    return enqueued;
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
