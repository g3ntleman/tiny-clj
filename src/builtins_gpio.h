#ifndef TINY_CLJ_BUILTINS_GPIO_H
#define TINY_CLJ_BUILTINS_GPIO_H

#include "builtins.h"
#include "symbol.h"

ID native_gpio_watch(ID *args, unsigned int argc);
ID native_gpio_simulate(ID *args, unsigned int argc);
ID native_gpio_write(ID *args, unsigned int argc);
ID native_gpio_read(ID *args, unsigned int argc);
ID native_gpio_read_analog(ID *args, unsigned int argc);
ID native_gpio_set_pin_mode(ID *args, unsigned int argc);
ID native_gpio_pin_mode(ID *args, unsigned int argc);
ID native_gpio_pin_read(ID *args, unsigned int argc);
ID native_gpio_pin_write(ID *args, unsigned int argc);
ID native_gpio_pwm(ID *args, unsigned int argc);
ID native_gpio_pwm_stop(ID *args, unsigned int argc);
ID native_gpio_simulate_analog(ID *args, unsigned int argc);
ID native_button_watch(ID *args, unsigned int argc);
ID native_sensor_watch(ID *args, unsigned int argc);

BuiltinFn builtins_gpio_native_function_lookup(CljSymbol *symbol);

#endif
