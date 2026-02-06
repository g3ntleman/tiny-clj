// ESP32 GPIO integration (Tiny-CLJ)
//
// NOTE: This header is included by generic code, but the implementation is
// compiled only for ESP32 builds (ESP32_BUILD).
#ifndef TINY_CLJ_GPIO_ESP32_H
#define TINY_CLJ_GPIO_ESP32_H

#include "object.h"

#ifdef ESP32_BUILD
ID native_gpio_watch(ID *args, unsigned int argc);
ID native_gpio_unwatch(ID *args, unsigned int argc);
ID native_gpio_simulate(ID *args, unsigned int argc);
#endif

#endif // TINY_CLJ_GPIO_ESP32_H

