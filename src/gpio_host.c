// Host GPIO stubs for clojure.core/gpio-*.
//
// Keeps the host/runtime wiring lightweight while sharing argument validation
// with ESP32 code paths.

#ifndef ESP32_BUILD

#include "gpio_host.h"
#include "event_loop.h"
#include "function.h"
#include "gpio_common.h"
#include "map.h"
#include "symbol.h"
#include "value.h"

static CljPersistentMap *g_gpio_watchers = NULL;
static CljPersistentMap *g_gpio_pin_levels = NULL;
static CljSymbol *KW_SOURCE = NULL;
static CljSymbol *KW_KIND = NULL;
static CljSymbol *KW_PIN = NULL;
static CljSymbol *KW_VALUE = NULL;
static CljSymbol *KW_GPIO = NULL;
static CljSymbol *KW_EDGE = NULL;
static int32_t g_next_watcher_id = 1;

static inline void gpio_host_ensure_initialized(void)
{
    if (g_gpio_watchers) return;
    g_gpio_watchers = make_map(4, STRONG);
    g_gpio_pin_levels = make_map(8, STRONG);
    KW_SOURCE = intern_symbol_global(":source");
    KW_KIND = intern_symbol_global(":kind");
    KW_PIN = intern_symbol_global(":pin");
    KW_VALUE = intern_symbol_global(":value");
    KW_GPIO = intern_symbol_global(":gpio");
    KW_EDGE = intern_symbol_global(":edge");
}

static inline void gpio_host_store_level(int32_t pin, int32_t level)
{
    gpio_host_ensure_initialized();
    map_assoc_inplace(&g_gpio_pin_levels, fixnum(pin), fixnum(level == 0 ? 0 : 1));
}

static ID gpio_host_make_watch_event(int32_t pin, int32_t level)
{
    gpio_host_ensure_initialized();
    return make_map_from_kv(4,
                            KW_SOURCE, KW_GPIO,
                            KW_KIND, KW_EDGE,
                            KW_PIN, fixnum(pin),
                            KW_VALUE, fixnum(level == 0 ? 0 : 1));
}

static bool gpio_host_enqueue_watch_event(int32_t pin, int32_t level)
{
    gpio_host_ensure_initialized();

    ID watcher_map = map_get((ID)g_gpio_watchers, fixnum(pin));
    if (!watcher_map || watcher_map == NOT_FOUND) {
        return false;
    }

    ID callback = map_get_sentinel(watcher_map, (ID)SYM_KW_CALLBACK_FN, NULL);
    if (!callback || callback == NOT_FOUND) {
        return false;
    }

    ID event_map = gpio_host_make_watch_event(pin, level);
    if (!event_map) {
        return false;
    }

    bool enqueued = event_loop_enqueue_ingress_call(callback, event_map);
    RELEASE(event_map);
    return enqueued;
}

bool gpio_host_simulate_pin_change(int32_t pin, int32_t level)
{
    gpio_host_store_level(pin, level);
    return gpio_host_enqueue_watch_event(pin, level);
}

void gpio_host_reset_state(void)
{
    ASSIGN(g_gpio_watchers, NULL);
    ASSIGN(g_gpio_pin_levels, NULL);
    KW_SOURCE = NULL;
    KW_KIND = NULL;
    KW_PIN = NULL;
    KW_VALUE = NULL;
    KW_GPIO = NULL;
    KW_EDGE = NULL;
    g_next_watcher_id = 1;
}

ID native_gpio_watch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-watch");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-watch", args[0], &pin);
    ID callback = args[1];
    if (!is_callable(callback)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "gpio-watch: callback must be callable", __FILE__, __LINE__, 0);
        return NULL;
    }

    gpio_host_ensure_initialized();
    ID pin_key = fixnum(pin);
    if (map_get((ID)g_gpio_watchers, pin_key) != NOT_FOUND) {
        throw_exception(EXCEPTION_RUNTIME, "gpio-watch: pin already watched", __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t watcher_id = g_next_watcher_id++;
    CljPersistentMap *watcher_map = make_map_from_kv(3,
                                                     (ID)KW_PIN, pin_key,
                                                     (ID)SYM_KW_CALLBACK_FN, callback,
                                                     (ID)SYM_KW_WATCHER_ID, fixnum(watcher_id));
    map_assoc_inplace(&g_gpio_watchers, pin_key, watcher_map);
    RELEASE(watcher_map);
    return fixnum(watcher_id);
}

ID native_gpio_unwatch(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "gpio-unwatch");

    ID wid_val = args[0];
    if (!wid_val || !is_fixnum(wid_val) || !g_gpio_watchers) {
        return NULL;
    }

    int32_t wid = (int32_t)as_fixnum(wid_val);
    ID found_pin_key = NULL;
    MAP_FOR_EACH(g_gpio_watchers, k, v) {
        ID stored_wid = map_get_sentinel(v, (ID)SYM_KW_WATCHER_ID, NOT_FOUND);
        if (stored_wid != NOT_FOUND && stored_wid && is_fixnum(stored_wid) && (int32_t)as_fixnum(stored_wid) == wid) {
            found_pin_key = k;
            break;
        }
    }
    if (found_pin_key) {
        map_remove_inplace(&g_gpio_watchers, found_pin_key);
    }
    return NULL;
}

ID native_gpio_simulate(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-simulate!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-simulate!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("gpio-simulate!", args[1], &level);
    (void)gpio_host_simulate_pin_change(pin, level);
    return NULL;
}

ID native_gpio_write(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 2, "gpio-write!");

    int32_t pin = 0;
    int32_t level = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-write!", args[0], &pin);
    GPIO_PARSE_LEVEL_FIXNUM_OR_RETURN_NULL("gpio-write!", args[1], &level);
    gpio_host_store_level(pin, level);
    return NULL;
}

ID native_gpio_read(ID *args, unsigned int argc)
{
    CHECK_ARITY(argc, 1, "gpio-read");

    int32_t pin = 0;
    GPIO_PARSE_PIN_FIXNUM_OR_RETURN_NULL("gpio-read", args[0], &pin);
    gpio_host_ensure_initialized();
    ID value = map_get((ID)g_gpio_pin_levels, fixnum(pin));
    if (!value || value == NOT_FOUND) {
        return fixnum(0);
    }
    return value;
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
