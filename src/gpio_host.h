#ifndef TINY_CLJ_GPIO_HOST_H
#define TINY_CLJ_GPIO_HOST_H

#ifndef ESP32_BUILD

#include <stdbool.h>
#include "object.h"
#include <stdint.h>

bool gpio_host_simulate_pin_change(int32_t pin, int32_t level);
bool gpio_host_simulate_analog_change(int32_t pin, int32_t value);
bool gpio_host_watch_set(int32_t pin, ID callback);
bool gpio_host_watch_clear(int32_t pin);
bool gpio_host_write_digital(int32_t pin, int32_t level);
bool gpio_host_write_analog(int32_t pin, int32_t value);
bool gpio_host_release_analog(int32_t pin);
ID gpio_host_read_digital(int32_t pin);
ID gpio_host_read_analog(int32_t pin);
bool gpio_host_pwm_start_or_update(int32_t pin, int32_t freq_hz, int32_t duty);
bool gpio_host_pwm_stop(int32_t pin);
bool gpio_host_simulate_digital(int32_t pin, int32_t level);
bool gpio_host_simulate_analog(int32_t pin, int32_t value);

#endif // !ESP32_BUILD

#endif // TINY_CLJ_GPIO_HOST_H
