// ESP32 GPIO integration for Tiny-CLJ.
//
// Build constraints:
// - This file must only be compiled for ESP32 targets (see CMake target_sources).
// - Guarded by ESP32_BUILD as an extra safety net.
//
// Design:
// - One shared pin-state slot per ESP32 GPIO pin.
// - Heap-managed fields such as semantic mode and watcher callback live directly in that slot.
// - ISR retains only callback-fn; thread context drains into generic event-loop ingress maps.
//
// Defaults (see plan "answer_questions"):
// - Pin config: GPIO input, interrupt on any edge, no pull-up/down.
// - Debouncing: none (userland can filter).
// - Errors: throw exceptions (IO / illegal-state / arity).
// - Multi-core: rely on ESP-IDF defaults.

#ifdef ESP32_BUILD

#include "gpio.h"
#include "gpio_esp32.h"

#include "exception.h"
#include "map.h"
#include "memory.h"
#include "symbol.h"
#include "value.h"
#include "event_loop.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/dac_oneshot.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_attr.h>
#include <stdatomic.h>

GpioEsp32PinState g_gpio_pin_state[GPIO_NUM_MAX];

// ISR service installed once.
static bool g_gpio_isr_service_installed = false;

// Event ring buffer (ISR -> thread-context input dispatch).
// Fixed-size to avoid heap allocations in ISR.
typedef struct {
    int32_t pin;
    int32_t value;
} GpioEvent;

enum { GPIO_EVENT_RING_CAP = 32 };
enum { GPIO_EVENT_RING_MASK = GPIO_EVENT_RING_CAP - 1 };
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
static adc_oneshot_unit_handle_t g_gpio_adc_unit1 = NULL;
static adc_oneshot_unit_handle_t g_gpio_adc_unit2 = NULL;
static dac_oneshot_handle_t g_gpio_dac_chan0 = NULL;
static dac_oneshot_handle_t g_gpio_dac_chan1 = NULL;
#if defined(ADC_CHANNEL_MAX)
enum { GPIO_ADC_CHANNEL_CAP = ADC_CHANNEL_MAX };
#else
enum { GPIO_ADC_CHANNEL_CAP = 10 };
#endif
static bool g_gpio_adc1_channels_configured[GPIO_ADC_CHANNEL_CAP];
static bool g_gpio_adc2_channels_configured[GPIO_ADC_CHANNEL_CAP];

static inline GpioEsp32PinState *gpio_pin_state_slot(int32_t pin) {
    if (!gpio_esp32_pin_state_valid(pin)) return NULL;
    return &g_gpio_pin_state[pin];
}

static inline void gpio_pwm_bindings_init(void) {
    if (g_pwm_bindings_initialized) return;
    for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
        g_gpio_pin_state[pin].pwm_binding_index = -1;
    }
    for (int i = 0; i < GPIO_PWM_BINDING_CAP; i++) {
        g_pwm_bindings[i].pin = -1;
        g_pwm_bindings[i].channel = (ledc_channel_t)i;
        g_pwm_bindings[i].timer = (ledc_timer_t)(i % GPIO_PWM_TIMER_CAP);
        g_pwm_bindings[i].configured = false;
    }
    g_pwm_bindings_initialized = true;
}

static inline bool gpio_pin_cache_index_valid(int32_t pin) {
    return gpio_esp32_pin_state_valid(pin);
}

static inline ID gpio_watcher_callback_get(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    return slot ? slot->watcher_callback : NULL;
}

static inline void gpio_watcher_callback_set(int32_t pin, ID callback) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) return;
    ASSIGN(slot->watcher_callback, callback);
}

static inline bool gpio_pin_has_runtime_watcher(int32_t pin, GpioEsp32PinState *slot) {
    if (slot) return slot->watcher_registered;
    return gpio_runtime_pin_has_watcher(pin);
}

static inline bool gpio_pin_has_runtime_c_callbacks(int32_t pin, GpioEsp32PinState *slot) {
    if (slot) return slot->c_callback_count > 0u;
    return gpio_runtime_pin_has_c_callbacks(pin);
}

static inline void gpio_mark_output_mode_configured(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) return;
    slot->output_mode_configured = true;
    slot->watch_input_irq_configured = false;
}

static inline void gpio_mark_watch_input_irq_configured(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) return;
    slot->watch_input_irq_configured = true;
    slot->output_mode_configured = false;
}

static inline bool gpio_input_irq_handler_installed(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    return slot ? slot->input_irq_handler_installed : false;
}

static inline void gpio_mark_input_irq_handler_installed(int32_t pin, bool installed) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) return;
    slot->input_irq_handler_installed = installed;
}

static inline GpioPwmBinding *gpio_pwm_binding_find_by_pin(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) return NULL;
    int16_t binding_idx = slot->pwm_binding_index;
    if (binding_idx < 0 || binding_idx >= GPIO_PWM_BINDING_CAP) return NULL;
    return &g_pwm_bindings[binding_idx];
}

static inline GpioPwmBinding *gpio_pwm_binding_acquire(int32_t pin) {
    GpioPwmBinding *existing = gpio_pwm_binding_find_by_pin(pin);
    if (existing) return existing;
    for (int i = 0; i < GPIO_PWM_BINDING_CAP; i++) {
        if (g_pwm_bindings[i].pin < 0) {
            g_pwm_bindings[i].pin = pin;
            g_pwm_bindings[i].configured = false;
            GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
            if (slot) {
                slot->pwm_binding_index = (int16_t)i;
            }
            return &g_pwm_bindings[i];
        }
    }
    return NULL;
}

static inline void gpio_pwm_binding_release(GpioPwmBinding *binding) {
    if (!binding) return;
    GpioEsp32PinState *slot = gpio_pin_state_slot(binding->pin);
    if (slot) {
        slot->pwm_binding_index = -1;
    }
    binding->pin = -1;
    binding->configured = false;
}

static bool gpio_ensure_watch_input_irq_configured(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (slot && slot->watch_input_irq_configured) {
        return true;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return false;
    }

    gpio_mark_watch_input_irq_configured(pin);
    return true;
}

static bool gpio_ensure_output_mode_configured(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (slot && slot->output_mode_configured) {
        return true;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (uint64_t)pin),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return false;
    }

    gpio_mark_output_mode_configured(pin);
    return true;
}

static bool gpio_adc_channel_for_pin(int32_t pin, adc_unit_t *out_unit, adc_channel_t *out_channel) {
    if (!out_unit || !out_channel) return false;
    switch (pin) {
        case 36: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_0; return true;
        case 37: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_1; return true;
        case 38: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_2; return true;
        case 39: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_3; return true;
        case 32: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_4; return true;
        case 33: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_5; return true;
        case 34: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_6; return true;
        case 35: *out_unit = ADC_UNIT_1; *out_channel = ADC_CHANNEL_7; return true;
        case 4:  *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_0; return true;
        case 0:  *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_1; return true;
        case 2:  *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_2; return true;
        case 15: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_3; return true;
        case 13: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_4; return true;
        case 12: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_5; return true;
        case 14: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_6; return true;
        case 27: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_7; return true;
        case 25: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_8; return true;
        case 26: *out_unit = ADC_UNIT_2; *out_channel = ADC_CHANNEL_9; return true;
        default: return false;
    }
}

static bool gpio_dac_channel_for_pin(int32_t pin, dac_channel_t *out_channel) {
    if (!out_channel) return false;
    switch (pin) {
        case 25:
            *out_channel = DAC_CHAN_0;
            return true;
        case 26:
            *out_channel = DAC_CHAN_1;
            return true;
        default:
            return false;
    }
}

static bool gpio_dac_ensure_handle(dac_channel_t channel, dac_oneshot_handle_t *out_handle) {
    if (!out_handle) return false;
    dac_oneshot_handle_t *slot = (channel == DAC_CHAN_0) ? &g_gpio_dac_chan0 : &g_gpio_dac_chan1;
    if (!*slot) {
        dac_oneshot_config_t cfg = {
            .chan_id = channel
        };
        esp_err_t err = dac_oneshot_new_channel(&cfg, slot);
        if (err != ESP_OK) {
            return false;
        }
    }
    *out_handle = *slot;
    return *slot != NULL;
}

static bool gpio_dac_release_channel(dac_channel_t channel) {
    dac_oneshot_handle_t *slot = (channel == DAC_CHAN_0) ? &g_gpio_dac_chan0 : &g_gpio_dac_chan1;
    if (!*slot) {
        return true;
    }
    esp_err_t err = dac_oneshot_del_channel(*slot);
    if (err != ESP_OK) {
        return false;
    }
    *slot = NULL;
    return true;
}

static bool gpio_adc_ensure_unit(adc_unit_t unit, adc_oneshot_unit_handle_t *out_handle) {
    if (!out_handle) return false;
    adc_oneshot_unit_handle_t *slot = (unit == ADC_UNIT_1) ? &g_gpio_adc_unit1 : &g_gpio_adc_unit2;
    if (!*slot) {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE
        };
        esp_err_t err = adc_oneshot_new_unit(&init_cfg, slot);
        if (err != ESP_OK) {
            return false;
        }
    }
    *out_handle = *slot;
    return *slot != NULL;
}

static bool *gpio_adc_channel_configured_slot(adc_unit_t unit, adc_channel_t channel) {
    int channel_idx = (int)channel;
    if (channel_idx < 0 || channel_idx >= GPIO_ADC_CHANNEL_CAP) {
        return NULL;
    }
    return (unit == ADC_UNIT_1)
               ? &g_gpio_adc1_channels_configured[channel_idx]
               : &g_gpio_adc2_channels_configured[channel_idx];
}

static bool gpio_adc_ensure_channel_configured(adc_oneshot_unit_handle_t handle,
                                               adc_unit_t unit,
                                               adc_channel_t channel) {
    bool *configured = gpio_adc_channel_configured_slot(unit, channel);
    if (!configured) {
        return false;
    }
    if (*configured) {
        return true;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12
    };
    esp_err_t err = adc_oneshot_config_channel(handle, channel, &chan_cfg);
    if (err != ESP_OK) {
        return false;
    }

    *configured = true;
    return true;
}

/** Push one raw edge event from ISR into ring. Returns false if ring full. */
static inline bool gpio_event_ring_push_from_isr(int32_t pin, int32_t value) {
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_acquire);
    uint32_t next_w = (w + 1u) & GPIO_EVENT_RING_MASK;
    if (next_w == r) return false; // full

    g_gpio_events[w].pin = pin;
    g_gpio_events[w].value = value;
    atomic_store_explicit(&g_gpio_event_w, next_w, memory_order_release);
    return true;
}

/** Pop next event from ring into *out. Returns false if empty. */
static inline bool gpio_event_ring_pop(GpioEvent *out) {
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_acquire);
    if (r == w) return false;

    *out = g_gpio_events[r];
    atomic_store_explicit(&g_gpio_event_r, (r + 1u) & GPIO_EVENT_RING_MASK, memory_order_release);
    return true;
}

/** ISR-side signal only: request one drain in thread context. */
static inline void gpio_request_drain_from_isr(void) {
    atomic_store_explicit(&g_gpio_drain_requested, true, memory_order_release);
}

/** Promote pending ISR events into C callbacks and Clojure watches. */
void gpio_esp32_poll_drain(void) {
    bool requested = atomic_load_explicit(&g_gpio_drain_requested, memory_order_acquire);
    uint32_t r = atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&g_gpio_event_w, memory_order_acquire);
    if (!requested && r == w) {
        return;
    }

    atomic_store_explicit(&g_gpio_drain_requested, false, memory_order_release);

    GpioEvent ev;
    while (gpio_event_ring_pop(&ev)) {
        GpioEsp32PinState *slot = gpio_pin_state_slot(ev.pin);
        gpio_runtime_dispatch_c_callbacks(ev.pin, ev.value);
        if (gpio_pin_has_runtime_watcher(ev.pin, slot) &&
            !gpio_runtime_enqueue_watch_event(ev.pin, ev.value)) {
            atomic_fetch_add_explicit(&g_gpio_event_drop_count, 1u, memory_order_relaxed);
        }
    }

    if (atomic_load_explicit(&g_gpio_event_r, memory_order_relaxed) !=
        atomic_load_explicit(&g_gpio_event_w, memory_order_acquire)) {
        atomic_store_explicit(&g_gpio_drain_requested, true, memory_order_release);
    }
}

uint32_t gpio_esp32_get_event_drop_count(void) {
    return atomic_load_explicit(&g_gpio_event_drop_count, memory_order_relaxed);
}

void gpio_esp32_runtime_reset_state(void) {
    for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
        if (g_gpio_pin_state[pin].input_irq_handler_installed) {
            (void)gpio_isr_handler_remove((gpio_num_t)pin);
        }
        ASSIGN(g_gpio_pin_state[pin].mode_entry, NULL);
        ASSIGN(g_gpio_pin_state[pin].watcher_callback, NULL);
        g_gpio_pin_state[pin].output_mode_configured = false;
        g_gpio_pin_state[pin].watch_input_irq_configured = false;
        g_gpio_pin_state[pin].input_irq_handler_installed = false;
        g_gpio_pin_state[pin].watcher_registered = false;
        g_gpio_pin_state[pin].input_irq_consumer_count = 0u;
        g_gpio_pin_state[pin].c_callback_count = 0u;
        g_gpio_pin_state[pin].pwm_binding_index = -1;
    }

    for (int i = 0; i < GPIO_PWM_BINDING_CAP; i++) {
        g_pwm_bindings[i].pin = -1;
        g_pwm_bindings[i].channel = (ledc_channel_t)i;
        g_pwm_bindings[i].timer = (ledc_timer_t)(i % GPIO_PWM_TIMER_CAP);
        g_pwm_bindings[i].configured = false;
    }
    g_pwm_bindings_initialized = false;
    if (g_gpio_dac_chan0) {
        (void)dac_oneshot_del_channel(g_gpio_dac_chan0);
        g_gpio_dac_chan0 = NULL;
    }
    if (g_gpio_dac_chan1) {
        (void)dac_oneshot_del_channel(g_gpio_dac_chan1);
        g_gpio_dac_chan1 = NULL;
    }
    g_gpio_isr_service_installed = false;
    atomic_store_explicit(&g_gpio_event_w, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_gpio_event_r, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_gpio_drain_requested, false, memory_order_relaxed);
    atomic_store_explicit(&g_gpio_event_drop_count, 0u, memory_order_relaxed);
}

/** ISR: read pin level, push raw edge event, schedule drain. */
static void IRAM_ATTR gpio_isr_handler(void *arg) {
    int32_t pin = (int32_t)(intptr_t)arg;
    if (!gpio_pin_cache_index_valid(pin)) return;
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    int32_t value = gpio_get_level((gpio_num_t)pin);

    ID callback_fn = slot ? slot->watcher_callback : gpio_watcher_callback_get(pin);
    if ((!callback_fn || IS_IMMEDIATE(callback_fn)) &&
        !gpio_pin_has_runtime_c_callbacks(pin, slot)) {
        return;
    }

    if (!gpio_event_ring_push_from_isr(pin, value)) {
        atomic_fetch_add_explicit(&g_gpio_event_drop_count, 1u, memory_order_relaxed);
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

static bool gpio_ensure_input_irq_handler_installed(int32_t pin) {
    if (gpio_input_irq_handler_installed(pin)) {
        return true;
    }
    if (!gpio_ensure_watch_input_irq_configured(pin)) {
        return false;
    }
    gpio_ensure_isr_service();
    esp_err_t err = gpio_isr_handler_add((gpio_num_t)pin, gpio_isr_handler, (void*)(intptr_t)pin);
    if (err != ESP_OK) {
        return false;
    }
    gpio_mark_input_irq_handler_installed(pin, true);
    return true;
}

bool gpio_esp32_input_irq_consumer_acquire(int32_t pin) {
    if (!gpio_pin_cache_index_valid(pin) || !GPIO_IS_VALID_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "watch: invalid pin", __FILE__, __LINE__, 0);
        return false;
    }
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) {
        return false;
    }
    if (!gpio_ensure_input_irq_handler_installed(pin)) {
        throw_exception(EXCEPTION_RUNTIME, "watch: gpio input irq setup failed", __FILE__, __LINE__, 0);
        return false;
    }
    if (slot->input_irq_consumer_count < UINT8_MAX) {
        slot->input_irq_consumer_count++;
    }
    return true;
}

bool gpio_esp32_input_irq_consumer_release(int32_t pin) {
    GpioEsp32PinState *slot = gpio_pin_state_slot(pin);
    if (!slot) {
        return false;
    }
    if (slot->input_irq_consumer_count > 0) {
        slot->input_irq_consumer_count--;
    }
    if (slot->input_irq_consumer_count == 0 && slot->input_irq_handler_installed) {
        (void)gpio_isr_handler_remove((gpio_num_t)pin);
        gpio_mark_input_irq_handler_installed(pin, false);
    }
    return true;
}

/**
 * @brief Remove a GPIO edge-interrupt watcher and ISR handler for one pin.
 *
 * @param pin GPIO pin number
 * @return true
 *
 * No-op if the pin currently has no watcher.
 */
bool gpio_esp32_watch_clear(int32_t pin) {
    ID callback = gpio_watcher_callback_get(pin);
    if (callback) {
        gpio_watcher_callback_set(pin, NULL);
        (void)gpio_runtime_watch_clear(pin);
        (void)gpio_esp32_input_irq_consumer_release(pin);
    }
    return true;
}

/**
 * @brief Register a GPIO edge-interrupt watcher for one pin.
 *
 * @param pin GPIO pin number
 * @param callback callable callback receiving {:source :gpio :signal :digital :kind :edge :pin N :value 0|1}
 * @return true on success, false after throwing on ESP-IDF failure
 *
 * Configures the pin as input with ANYEDGE interrupt, installs ISR handler,
 * and stores the watcher in g_gpio_watchers. Replaces any existing watcher
 * for the same pin.
 */
bool gpio_esp32_watch_set(int32_t pin, ID callback) {
    if (!gpio_pin_cache_index_valid(pin) || !GPIO_IS_VALID_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "watch: invalid pin", __FILE__, __LINE__, 0);
        return false;
    }

    if (gpio_watcher_callback_get(pin)) {
        gpio_watcher_callback_set(pin, NULL);
        (void)gpio_runtime_watch_clear(pin);
        (void)gpio_esp32_input_irq_consumer_release(pin);
    }
    gpio_watcher_callback_set(pin, callback);
    if (!gpio_runtime_watch_set(pin, callback)) {
        gpio_watcher_callback_set(pin, NULL);
        return false;
    }

    if (!gpio_esp32_input_irq_consumer_acquire(pin)) {
        gpio_watcher_callback_set(pin, NULL);
        (void)gpio_runtime_watch_clear(pin);
        return false;
    }

    return true;
}

/**
 * @brief Set a GPIO pin output level.
 * @param pin valid output pin
 * @param level 0 or 1
 * @return true on success, false after throwing on validation/runtime errors
 */
bool gpio_esp32_write_digital(int32_t pin, int32_t level) {
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "write!: invalid output pin", __FILE__, __LINE__, 0);
        return false;
    }

    // Force output mode before writing to survive mode drift across watchers/reconfigurations.
    if (!gpio_ensure_output_mode_configured(pin)) {
        throw_exception(EXCEPTION_RUNTIME, "write!: gpio_set_direction failed", __FILE__, __LINE__, 0);
        return false;
    }

    esp_err_t err = gpio_set_level((gpio_num_t)pin, (uint32_t)level);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "write!: gpio_set_level failed", __FILE__, __LINE__, 0);
        return false;
    }

    return true;
}

/**
 * @brief Set a DAC-capable pin to a raw 8-bit analog output value.
 * @param pin DAC-capable GPIO pin
 * @param value raw DAC output value 0..255
 * @return true on success, false after throwing on validation/runtime errors
 */
bool gpio_esp32_write_analog(int32_t pin, int32_t value) {
    if (value < 0 || value > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pin-write: dac value out of range", __FILE__, __LINE__, 0);
        return false;
    }

    dac_channel_t channel = DAC_CHAN_0;
    if (!gpio_dac_channel_for_pin(pin, &channel)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pin-write: pin is not DAC-capable on ESP32", __FILE__, __LINE__, 0);
        return false;
    }

    dac_oneshot_handle_t handle = NULL;
    if (!gpio_dac_ensure_handle(channel, &handle)) {
        throw_exception(EXCEPTION_RUNTIME, "pin-write: dac_oneshot_new_channel failed", __FILE__, __LINE__, 0);
        return false;
    }

    esp_err_t err = dac_oneshot_output_voltage(handle, (uint8_t)value);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "pin-write: dac_oneshot_output_voltage failed", __FILE__, __LINE__, 0);
        return false;
    }

    return true;
}

bool gpio_esp32_release_analog(int32_t pin) {
    dac_channel_t channel = DAC_CHAN_0;
    if (!gpio_dac_channel_for_pin(pin, &channel)) {
        return true;
    }
    if (!gpio_dac_release_channel(channel)) {
        throw_exception(EXCEPTION_RUNTIME, "set-pin-mode!: dac_oneshot_del_channel failed", __FILE__, __LINE__, 0);
        return false;
    }
    return true;
}

/**
 * @brief Read a GPIO pin level.
 * @param pin valid GPIO pin
 * @return fixnum 0|1
 */
ID gpio_esp32_read_digital(int32_t pin) {
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "read: invalid pin", __FILE__, __LINE__, 0);
        return NULL;
    }

    int level = gpio_get_level((gpio_num_t)pin);
    return fixnum((level == 0) ? 0 : 1);
}

/**
 * @brief Read an analog GPIO/ADC pin as a raw 12-bit ADC value.
 *
 * @param pin GPIO pin number mapped to ADC1 or ADC2 on ESP32
 * @return fixnum raw ADC value in the range 0..4095
 *
 * Uses the ESP-IDF oneshot ADC API with 12 dB attenuation and 12-bit width.
 * The public contract intentionally returns raw values, not millivolts.
 * Throws on invalid non-ADC pins or ADC driver failures.
 */
ID gpio_esp32_read_analog(int32_t pin) {
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    if (!gpio_adc_channel_for_pin(pin, &unit, &channel)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "read-analog: pin is not ADC-capable on ESP32", __FILE__, __LINE__, 0);
        return NULL;
    }

    adc_oneshot_unit_handle_t handle = NULL;
    if (!gpio_adc_ensure_unit(unit, &handle)) {
        throw_exception(EXCEPTION_RUNTIME, "read-analog: adc_oneshot_new_unit failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    if (!gpio_adc_ensure_channel_configured(handle, unit, channel)) {
        throw_exception(EXCEPTION_RUNTIME, "read-analog: adc_oneshot_config_channel failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(handle, channel, &raw);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "read-analog: adc_oneshot_read failed", __FILE__, __LINE__, 0);
        return NULL;
    }

    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;
    return fixnum((int32_t)raw);
}

/**
 * @brief Configure PWM output on a GPIO pin via LEDC.
 *
 * @param pin valid output GPIO
 * @param freq_hz 1..100000 Hz
 * @param duty8 0..255, mapped to 10-bit LEDC resolution
 * @return true on success, false after throwing on validation/runtime errors
 *
 * Acquires a LEDC channel/timer binding for the pin. Reconfigurable:
 * calling again with different freq/duty updates the running PWM.
 */
bool gpio_esp32_pwm_start_or_update(int32_t pin, int32_t freq_hz, int32_t duty8) {
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pwm!: invalid output pin", __FILE__, __LINE__, 0);
        return false;
    }

    if (freq_hz < 1 || freq_hz > GPIO_PWM_MAX_FREQ_HZ) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pwm!: freq-hz out of range", __FILE__, __LINE__, 0);
        return false;
    }
    if (duty8 < 0 || duty8 > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pwm!: duty out of range", __FILE__, __LINE__, 0);
        return false;
    }

    gpio_pwm_bindings_init();
    GpioPwmBinding *binding = gpio_pwm_binding_acquire(pin);
    if (!binding) {
        throw_exception(EXCEPTION_RUNTIME, "pwm!: no free PWM channel", __FILE__, __LINE__, 0);
        return false;
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
        throw_exception(EXCEPTION_RUNTIME, "pwm!: ledc_timer_config failed", __FILE__, __LINE__, 0);
        return false;
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
            throw_exception(EXCEPTION_RUNTIME, "pwm!: ledc_channel_config failed", __FILE__, __LINE__, 0);
            return false;
        }
        binding->configured = true;
    }

    uint32_t duty10 = ((uint32_t)duty8 * 1023u + 127u) / 255u;
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, binding->channel, duty10);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "pwm!: ledc_set_duty failed", __FILE__, __LINE__, 0);
        return false;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, binding->channel);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "pwm!: ledc_update_duty failed", __FILE__, __LINE__, 0);
        return false;
    }

    return true;
}

/**
 * @brief Stop PWM output on a GPIO pin.
 *
 * @param pin valid output GPIO with an active LEDC binding
 * @return true on success, false after throwing on validation/runtime errors
 *
 * Stops the LEDC channel, drives pin low, and releases the channel binding.
 * No-op if pin has no active PWM binding.
 */
bool gpio_esp32_pwm_stop(int32_t pin) {
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO((gpio_num_t)pin)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "pwm-stop!: invalid output pin", __FILE__, __LINE__, 0);
        return false;
    }

    gpio_pwm_bindings_init();
    GpioPwmBinding *binding = gpio_pwm_binding_find_by_pin(pin);
    if (!binding) return true;

    esp_err_t err = ledc_stop(LEDC_LOW_SPEED_MODE, binding->channel, 0);
    if (err != ESP_OK) {
        throw_exception(EXCEPTION_RUNTIME, "pwm-stop!: ledc_stop failed", __FILE__, __LINE__, 0);
        return false;
    }
    err = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    if (err == ESP_OK) {
        (void)gpio_set_level((gpio_num_t)pin, 0u);
    }

    gpio_pwm_binding_release(binding);
    return true;
}

/** No-op on ESP32 (macOS tests use simulate! for mock). */
bool gpio_esp32_simulate_digital(int32_t pin, int32_t level) {
    (void)pin;
    (void)level;
    return true;
}

/**
 * @brief No-op on ESP32 for the host-only analog simulation helper.
 *
 * @param pin GPIO pin number
 * @param value raw analog value
 * @return true
 */
bool gpio_esp32_simulate_analog(int32_t pin, int32_t value) {
    (void)pin;
    (void)value;
    return true;
}

#endif // ESP32_BUILD
