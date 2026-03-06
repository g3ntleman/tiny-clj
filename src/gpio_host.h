#ifndef TINY_CLJ_GPIO_HOST_H
#define TINY_CLJ_GPIO_HOST_H

#ifndef ESP32_BUILD

#include <stdbool.h>
#include <stdint.h>

bool gpio_host_simulate_pin_change(int32_t pin, int32_t level);

#endif // !ESP32_BUILD

#endif // TINY_CLJ_GPIO_HOST_H
