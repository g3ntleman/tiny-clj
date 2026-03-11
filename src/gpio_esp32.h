// ESP32 GPIO integration (Tiny-CLJ)
//
// NOTE: This header is included by generic code, but the implementation is
// compiled only for ESP32 builds (ESP32_BUILD).
#ifndef TINY_CLJ_GPIO_ESP32_H
#define TINY_CLJ_GPIO_ESP32_H

#include "object.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef ESP32_BUILD
#include <driver/gpio.h>

typedef struct {
    ID mode_entry;
    ID watcher_callback;
    bool output_mode_configured;
    bool watch_input_irq_configured;
    bool input_irq_handler_installed;
    uint8_t input_irq_consumer_count;
    int16_t pwm_binding_index;
} GpioEsp32PinState;

extern GpioEsp32PinState g_gpio_pin_state[GPIO_NUM_MAX];

static inline bool gpio_esp32_pin_state_valid(int32_t pin) {
    return pin >= 0 && pin < GPIO_NUM_MAX;
}

bool gpio_esp32_watch_set(int32_t pin, ID callback);
bool gpio_esp32_watch_clear(int32_t pin);
bool gpio_esp32_input_irq_consumer_acquire(int32_t pin);
bool gpio_esp32_input_irq_consumer_release(int32_t pin);
bool gpio_esp32_write_digital(int32_t pin, int32_t level);
bool gpio_esp32_write_analog(int32_t pin, int32_t value);
bool gpio_esp32_release_analog(int32_t pin);
ID gpio_esp32_read_digital(int32_t pin);
ID gpio_esp32_read_analog(int32_t pin);
bool gpio_esp32_pwm_start_or_update(int32_t pin, int32_t freq_hz, int32_t duty8);
bool gpio_esp32_pwm_stop(int32_t pin);
bool gpio_esp32_simulate_digital(int32_t pin, int32_t level);
bool gpio_esp32_simulate_analog(int32_t pin, int32_t value);

// Event-loop bridge hooks (ESP32 only).
void gpio_esp32_poll_drain(void);
uint32_t gpio_esp32_get_event_drop_count(void);
void gpio_esp32_runtime_reset_state(void);
#endif

#endif // TINY_CLJ_GPIO_ESP32_H
