// Host GPIO stubs for tiny-clj.gpio/*.
//
// Keeps the host/runtime wiring lightweight while sharing argument validation
// with ESP32 code paths.

#ifndef ESP32_BUILD

#include "gpio.h"
#include "gpio_host.h"

#include <limits.h>

bool gpio_host_simulate_pin_change(int32_t pin, int32_t level)
{
    gpio_runtime_store_digital_level(pin, level);
    gpio_runtime_dispatch_c_callbacks(pin, level);
    return gpio_runtime_enqueue_watch_event(pin, level);
}

bool gpio_host_simulate_analog_change(int32_t pin, int32_t value)
{
    gpio_runtime_store_analog_level(pin, value);
    return true;
}

bool gpio_host_watch_set(int32_t pin, ID callback) {
    return gpio_runtime_watch_set(pin, callback);
}

bool gpio_host_watch_clear(int32_t pin) {
    return gpio_runtime_watch_clear(pin);
}

bool gpio_host_write_digital(int32_t pin, int32_t level) {
    (void)pin;
    gpio_runtime_store_digital_level(pin, level);
    return true;
}

bool gpio_host_write_analog(int32_t pin, int32_t value) {
    gpio_runtime_store_analog_level(pin, value);
    return true;
}

bool gpio_host_release_analog(int32_t pin) {
    (void)pin;
    return true;
}

ID gpio_host_read_digital(int32_t pin) {
    return gpio_runtime_read_digital_level(pin);
}

ID gpio_host_read_analog(int32_t pin) {
    return gpio_runtime_read_analog_level(pin);
}

bool gpio_host_pwm_start_or_update(int32_t pin, int32_t freq_hz, int32_t duty) {
    (void)pin;
    (void)freq_hz;
    (void)duty;
    return true;
}

bool gpio_host_pwm_stop(int32_t pin) {
    (void)pin;
    return true;
}

bool gpio_host_simulate_digital(int32_t pin, int32_t level) {
    return gpio_host_simulate_pin_change(pin, level);
}

bool gpio_host_simulate_analog(int32_t pin, int32_t value) {
    return gpio_host_simulate_analog_change(pin, value);
}

#endif // !ESP32_BUILD
