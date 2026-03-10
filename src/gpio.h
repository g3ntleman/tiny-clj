#ifndef TINY_CLJ_GPIO_H
#define TINY_CLJ_GPIO_H

#include "object.h"

#include <stdbool.h>
#include <stdint.h>

void gpio_runtime_reset_state(void);

bool gpio_runtime_watch_set(int32_t pin, ID callback);
bool gpio_runtime_watch_clear(int32_t pin);
bool gpio_runtime_enqueue_watch_event(int32_t pin, int32_t level);

void gpio_runtime_store_digital_level(int32_t pin, int32_t level);
void gpio_runtime_store_analog_level(int32_t pin, int32_t value);

ID gpio_runtime_read_digital_level(int32_t pin);
ID gpio_runtime_read_analog_level(int32_t pin);

bool gpio_watch_set(int32_t pin, ID callback);
bool gpio_watch_clear(int32_t pin);
bool gpio_write_digital(int32_t pin, int32_t level);
bool gpio_write_analog(int32_t pin, int32_t value);
bool gpio_release_analog(int32_t pin);
ID gpio_read_digital(int32_t pin);
ID gpio_read_analog(int32_t pin);
bool gpio_pwm_start_or_update(int32_t pin, int32_t freq_hz, int32_t duty);
bool gpio_pwm_stop(int32_t pin);
bool gpio_simulate_digital(int32_t pin, int32_t level);
bool gpio_simulate_analog(int32_t pin, int32_t value);
void gpio_poll_drain(void);
uint32_t gpio_get_event_drop_count(void);

#endif
