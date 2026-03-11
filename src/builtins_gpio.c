/*
 * Native lookup table for tiny-clj.gpio.
 *
 * Keeps the GPIO :native stub wiring out of builtins.c while preserving the
 * same qualified symbol contract for the tiny-clj.gpio namespace.
 */

#include <stdio.h>
#include <string.h>

#include "builtins_gpio.h"

typedef struct {
    CljSymbol *clojure_symbol;
    BuiltinFn native_func;
} BuiltinsGpioNativeFunctionEntry;

static StaticSymbolData sym_gpio_watch_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/watch"}};
static StaticSymbolData sym_gpio_watch_native_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/watch-native"}};
static StaticSymbolData sym_gpio_simulate_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/simulate!"}};
static StaticSymbolData sym_gpio_write_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/write!"}};
static StaticSymbolData sym_gpio_read_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/read"}};
static StaticSymbolData sym_gpio_read_analog_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/read-analog"}};
static StaticSymbolData sym_gpio_set_pin_mode_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/set-pin-mode!"}};
static StaticSymbolData sym_gpio_pin_mode_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/pin-mode"}};
static StaticSymbolData sym_gpio_pin_read_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/pin-read"}};
static StaticSymbolData sym_gpio_pin_write_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/pin-write"}};
static StaticSymbolData sym_gpio_pin_pwm_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/pin-pwm!"}};
static StaticSymbolData sym_gpio_pwm_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/pwm!"}};
static StaticSymbolData sym_gpio_pwm_stop_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/pwm-stop!"}};
static StaticSymbolData sym_gpio_simulate_analog_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.gpio/simulate-analog!"}};
static StaticSymbolData sym_button_watch_native_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.button/watch-native"}};
static StaticSymbolData sym_sensor_watch_native_qualified_data = {
    .sym = {.base = {.type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE},
            .ns_name = NULL,
            .unqualified = NULL,
            .cname = "tiny-clj.sensor/watch-native"}};

static const BuiltinsGpioNativeFunctionEntry builtins_gpio_native_function_table[] = {
    {&sym_gpio_watch_qualified_data.sym, native_gpio_watch},
    {&sym_gpio_watch_native_qualified_data.sym, native_gpio_watch},
    {&sym_gpio_simulate_qualified_data.sym, native_gpio_simulate},
    {&sym_gpio_write_qualified_data.sym, native_gpio_write},
    {&sym_gpio_read_qualified_data.sym, native_gpio_read},
    {&sym_gpio_read_analog_qualified_data.sym, native_gpio_read_analog},
    {&sym_gpio_set_pin_mode_qualified_data.sym, native_gpio_set_pin_mode},
    {&sym_gpio_pin_mode_qualified_data.sym, native_gpio_pin_mode},
    {&sym_gpio_pin_read_qualified_data.sym, native_gpio_pin_read},
    {&sym_gpio_pin_write_qualified_data.sym, native_gpio_pin_write},
    {&sym_gpio_pin_pwm_qualified_data.sym, native_gpio_pwm},
    {&sym_gpio_pwm_qualified_data.sym, native_gpio_pwm},
    {&sym_gpio_pwm_stop_qualified_data.sym, native_gpio_pwm_stop},
    {&sym_gpio_simulate_analog_qualified_data.sym, native_gpio_simulate_analog},
    {&sym_button_watch_native_qualified_data.sym, native_button_watch},
    {&sym_sensor_watch_native_qualified_data.sym, native_sensor_watch},
    {NULL, NULL}};

BuiltinFn builtins_gpio_native_function_lookup(CljSymbol *symbol) {
    if (!symbol) {
        return NULL;
    }

    const char *cname = symbol->cname;
    const char *ns_name = symbol->ns_name ? symbol->ns_name->cname : NULL;
    char qualified_name[128] = {0};
    if (ns_name && cname) {
        (void)snprintf(qualified_name, sizeof(qualified_name), "%s/%s", ns_name, cname);
    }

    for (int i = 0; builtins_gpio_native_function_table[i].clojure_symbol != NULL; i++) {
        CljSymbol *table_sym = builtins_gpio_native_function_table[i].clojure_symbol;
        if (!table_sym) {
            continue;
        }

        if (table_sym == symbol) {
            return builtins_gpio_native_function_table[i].native_func;
        }

        if (!cname || !table_sym->cname) {
            continue;
        }

        const char *table_ns = table_sym->ns_name ? table_sym->ns_name->cname : NULL;
        if (ns_name) {
            if (table_ns && strcmp(table_ns, ns_name) == 0 && strcmp(table_sym->cname, cname) == 0) {
                return builtins_gpio_native_function_table[i].native_func;
            }
            if (!table_ns && strcmp(table_sym->cname, qualified_name) == 0) {
                return builtins_gpio_native_function_table[i].native_func;
            }
        } else if (strcmp(table_sym->cname, cname) == 0) {
            return builtins_gpio_native_function_table[i].native_func;
        }
    }

    return NULL;
}
