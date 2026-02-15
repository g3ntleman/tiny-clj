// Shared GPIO argument parsing helpers (host + ESP32).
#ifndef TINY_CLJ_GPIO_COMMON_H
#define TINY_CLJ_GPIO_COMMON_H

#include "exception.h"
#include "object.h"
#include "value.h"

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

static inline bool gpio_parse_pin_fixnum_or_throw(ID pin_val,
                                                  const char *fn_name,
                                                  const char *file,
                                                  int line,
                                                  int32_t *out_pin) {
    if (!out_pin) return false;
    if (!pin_val || !is_fixnum(pin_val)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, file, line, 0,
                                  "%s: pin must be fixnum", fn_name);
        return false;
    }
    *out_pin = (int32_t)as_fixnum(pin_val);
    return true;
}

static inline bool gpio_parse_fixnum_range_or_throw(ID value,
                                                    const char *fn_name,
                                                    const char *arg_name,
                                                    int32_t min_value,
                                                    int32_t max_value,
                                                    const char *file,
                                                    int line,
                                                    int32_t *out_value) {
    if (!out_value) return false;
    if (!value || !is_fixnum(value)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, file, line, 0,
                                  "%s: %s must be fixnum", fn_name, arg_name);
        return false;
    }
    int32_t parsed = (int32_t)as_fixnum(value);
    if (parsed < min_value || parsed > max_value) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, file, line, 0,
                                  "%s: %s out of range [%d..%d]",
                                  fn_name, arg_name, (int)min_value, (int)max_value);
        return false;
    }
    *out_value = parsed;
    return true;
}

static inline bool gpio_parse_level_fixnum_or_throw(ID level_val,
                                                    const char *fn_name,
                                                    const char *file,
                                                    int line,
                                                    int32_t *out_level) {
    if (!out_level) return false;
    if (!level_val || !is_fixnum(level_val)) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, file, line, 0,
                                  "%s: level must be fixnum", fn_name);
        return false;
    }
    *out_level = (as_fixnum(level_val) == 0) ? 0 : 1;
    return true;
}

#define GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL(fn_name, pin_val, out_pin_ptr) \
    do {                                                                     \
        if (!gpio_parse_pin_fixnum_or_throw((pin_val), (fn_name), __FILE__, __LINE__, (out_pin_ptr))) return NULL; \
    } while (0)

#define GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL(fn_name, level_val, out_level_ptr) \
    do {                                                                            \
        if (!gpio_parse_level_fixnum_or_throw((level_val), (fn_name), __FILE__, __LINE__, (out_level_ptr))) return NULL; \
    } while (0)

#define GPIO_PARSE_FIXNUM_RANGE_OR_RETURN_NULL(fn_name, arg_name, value, min_value, max_value, out_value_ptr) \
    do {                                                                                                       \
        if (!gpio_parse_fixnum_range_or_throw((value), (fn_name), (arg_name), (min_value), (max_value),      \
                                              __FILE__, __LINE__, (out_value_ptr))) return NULL;              \
    } while (0)

#endif // TINY_CLJ_GPIO_COMMON_H
