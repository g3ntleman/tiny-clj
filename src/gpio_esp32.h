// ESP32 GPIO integration (Tiny-CLJ)
//
// NOTE: This header is included by generic code, but the implementation is
// compiled only for ESP32 builds (ESP32_BUILD).
#ifndef TINY_CLJ_GPIO_ESP32_H
#define TINY_CLJ_GPIO_ESP32_H

#include "object.h"
#include <stdint.h>

#ifdef ESP32_BUILD
ID native_gpio_watch(ID *args, unsigned int argc);
ID native_gpio_unwatch(ID *args, unsigned int argc);
ID native_gpio_simulate(ID *args, unsigned int argc);
ID native_gpio_write(ID *args, unsigned int argc);
ID native_gpio_read(ID *args, unsigned int argc);
ID native_gpio_pwm(ID *args, unsigned int argc);
ID native_gpio_pwm_stop(ID *args, unsigned int argc);

// Event-loop bridge hooks (ESP32 only).
void gpio_esp32_poll_drain(void);
uint32_t gpio_esp32_get_event_drop_count(void);
#endif

#endif // TINY_CLJ_GPIO_ESP32_H
