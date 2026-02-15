// Host GPIO stubs for clojure.core/gpio-*.
//
// Keeps the host/runtime wiring lightweight while sharing argument validation
// with ESP32 code paths.

#ifndef ESP32_BUILD

#include "gpio_common.h"
#include "validation.h"

ID native_gpio_watch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-watch");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-watch", args[0], &pin);
    (void)pin;
    return fixnum(1);
}

ID native_gpio_unwatch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "gpio-unwatch");
    (void)args;
    return NULL;
}

ID native_gpio_simulate(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-simulate!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-simulate!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("gpio-simulate!", args[1], &level);
    (void)pin;
    (void)level;
    return NULL;
}

ID native_gpio_write(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-write!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-write!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("gpio-write!", args[1], &level);
    (void)pin;
    (void)level;
    return NULL;
}

ID native_gpio_read(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "gpio-read");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-read", args[0], &pin);
    (void)pin;
    return fixnum(0);
}

ID native_gpio_pwm(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 3, "gpio-pwm!");

    int32_t pin = 0;
    int32_t freq_hz = 0;
    int32_t duty = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-pwm!", args[0], &pin);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("gpio-pwm!", "freq-hz", args[1], 1, INT_MAX, &freq_hz);
    GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL("gpio-pwm!", "duty", args[2], 0, 255, &duty);
    (void)pin;
    (void)freq_hz;
    (void)duty;
    return NULL;
}

ID native_gpio_pwm_stop(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "gpio-pwm-stop!");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-pwm-stop!", args[0], &pin);
    (void)pin;
    return NULL;
}

#endif // !ESP32_BUILD
