/*
 * Tests for tiny-clj.gpio native function wiring and contract.
 *
 * Scope:
 * - qualified native lookup wiring
 * - namespace resolution after require
 * - host stub behavior for digital, analog, and PWM paths
 * - high-level helpers (pin-mode API, unified watch)
 * - arity and range validation
 */

#include "tests_common.h"
#include "builtins.h"
#include "event_loop.h"
#include "symbol.h"

#define REQ "(require 'tiny-clj.gpio) "

TEST(test_native_lookup_finds_gpio_write) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.gpio/write!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "tiny-clj.gpio/write! symbol should exist");
        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find tiny-clj.gpio/write!");
    });
}

TEST(test_native_lookup_finds_gpio_read) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.gpio/read");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "tiny-clj.gpio/read symbol should exist");
        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find tiny-clj.gpio/read");
    });
}

TEST(test_native_lookup_finds_gpio_read_analog) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.gpio/read-analog");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "tiny-clj.gpio/read-analog symbol should exist");
        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find tiny-clj.gpio/read-analog");
    });
}

TEST(test_native_lookup_finds_gpio_pwm) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.gpio/pwm!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "tiny-clj.gpio/pwm! symbol should exist");
        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find tiny-clj.gpio/pwm!");
    });
}

TEST(test_native_lookup_finds_gpio_pwm_stop) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.gpio/pwm-stop!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "tiny-clj.gpio/pwm-stop! symbol should exist");
        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find tiny-clj.gpio/pwm-stop!");
    });
}

TEST(test_native_lookup_finds_gpio_simulate_analog) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.gpio/simulate-analog!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "tiny-clj.gpio/simulate-analog! symbol should exist");
        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find tiny-clj.gpio/simulate-analog!");
    });
}

TEST(test_gpio_write_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/write!)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/write! should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/write! should resolve to callable function");
    });
}

TEST(test_gpio_read_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/read)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/read should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/read should resolve to callable function");
    });
}

TEST(test_gpio_read_analog_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/read-analog)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/read-analog should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/read-analog should resolve to callable function");
    });
}

TEST(test_gpio_write_call_returns_nil_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/write! 2 1))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/write! with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "tiny-clj.gpio/write! should return nil");
}

TEST(test_gpio_pwm_call_returns_nil_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/pwm! 2 1000 128))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/pwm! with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "tiny-clj.gpio/pwm! should return nil");
}

TEST(test_gpio_pwm_stop_call_returns_nil_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/pwm-stop! 2))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/pwm-stop! with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "tiny-clj.gpio/pwm-stop! should return nil");
}

TEST(test_gpio_read_call_returns_fixnum_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/read 2))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/read with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL_MESSAGE(result, "tiny-clj.gpio/read should return a value");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "tiny-clj.gpio/read should return fixnum");
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_gpio_read_analog_returns_fixnum_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/read-analog 35))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/read-analog with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL_MESSAGE(result, "tiny-clj.gpio/read-analog should return a value");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "tiny-clj.gpio/read-analog should return fixnum");
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_gpio_simulate_updates_read_value_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/simulate! 7 1) (tiny-clj.gpio/read 7))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/simulate! should update host pin state");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "read should return fixnum after simulate!");
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST(test_gpio_simulate_analog_updates_read_analog_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/simulate-analog! 35 1234) (tiny-clj.gpio/read-analog 35))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.gpio/simulate-analog! should update host analog state");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "read-analog should return fixnum after simulate-analog!");
    TEST_ASSERT_EQUAL_INT(1234, as_fixnum(result));
}

TEST(test_gpio_watch_and_simulate_enqueue_callback_event_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            REQ
            "  (def gpio-watch-events (atom [])) "
            "  (tiny-clj.gpio/watch 8 (fn [ev] (swap! gpio-watch-events conj ev) nil)) "
            "  (tiny-clj.gpio/simulate! 8 1) "
            "  (run-next-task) "
            "  (let [events @gpio-watch-events] "
            "    (tiny-clj.gpio/watch 8 nil) "
            "    events))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("watch + simulate! should deliver one event on host");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_vector(result), "watch callback should record vector of events");
    TEST_ASSERT_EQUAL_INT(1, vector_count((CljPersistentVector *)result));

    ID first_event = vector_nth((CljPersistentVector *)result, 0);
    TEST_ASSERT_TRUE_MESSAGE(is_map(first_event), "recorded event should be a map");
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":gpio"),
                          map_get(first_event, intern_symbol_global(":source")));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":digital"),
                          map_get(first_event, intern_symbol_global(":signal")));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":edge"),
                          map_get(first_event, intern_symbol_global(":kind")));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum(map_get(first_event, intern_symbol_global(":pin"))));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(map_get(first_event, intern_symbol_global(":value"))));
}

TEST(test_gpio_watch_nil_stops_future_host_events) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            REQ
            "  (def gpio-watch-events-2 (atom [])) "
            "  (tiny-clj.gpio/watch 9 (fn [ev] (swap! gpio-watch-events-2 conj ev) nil)) "
            "  (tiny-clj.gpio/watch 9 nil) "
            "  (tiny-clj.gpio/simulate! 9 1) "
            "  (run-next-task) "
            "  @gpio-watch-events-2)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("(watch pin nil) should prevent future host events");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_vector(result), "event accumulator should remain a vector");
    TEST_ASSERT_EQUAL_INT(0, vector_count((CljPersistentVector *)result));
}

TEST(test_gpio_watch_analog_signal_returns_handle_and_close) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            REQ
            "  (let [gpio-analog-watch "
            "          (tiny-clj.gpio/watch 35 "
            "            (fn [_] nil) "
            "            {:signal :analog :period-ms 1 :threshold 5})] "
            "    (run-next-task) "
            "    (get gpio-analog-watch :timer-id)))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("watch with :signal :analog should create a handle without throwing");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "analog watch should return a timer-id in its handle map");
    event_loop_clear();
}

TEST(test_gpio_watch_analog_signal_delivers_initial_event_with_local_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            REQ
            "  (tiny-clj.gpio/simulate-analog! 35 1234) "
            "  (let [events (atom []) "
            "        w (tiny-clj.gpio/watch 35 "
            "             (fn [ev] (swap! events conj ev) nil) "
            "             {:signal :analog :period-ms 1 :threshold 0 :emit-initial? true})] "
            "    (run-next-task) "
            "    ((get w :close!)) "
            "    @events))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("analog watch should deliver an initial event into a local atom");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_vector(result), "analog watch should record events in a vector");
    TEST_ASSERT_EQUAL_INT(1, vector_count((CljPersistentVector *)result));

    ID first_event = vector_nth((CljPersistentVector *)result, 0);
    TEST_ASSERT_TRUE_MESSAGE(is_map(first_event), "analog watch event should be a map");
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":gpio"),
                          map_get(first_event, intern_symbol_global(":source")));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":analog"),
                          map_get(first_event, intern_symbol_global(":signal")));
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":analog"),
                          map_get(first_event, intern_symbol_global(":kind")));
    TEST_ASSERT_EQUAL_INT(35, as_fixnum(map_get(first_event, intern_symbol_global(":pin"))));
    TEST_ASSERT_EQUAL_INT(1234, as_fixnum(map_get(first_event, intern_symbol_global(":value"))));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(map_get(first_event, intern_symbol_global(":delta"))));

    event_loop_clear();
}

TEST(test_gpio_watch_analog_threshold_filters_small_changes_with_local_atom) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            REQ
            "  (tiny-clj.gpio/simulate-analog! 35 100) "
            "  (let [events (atom []) "
            "        w (tiny-clj.gpio/watch 35 "
            "             (fn [ev] (swap! events conj ev) nil) "
            "             {:signal :analog :period-ms 1 :threshold 5 :emit-initial? true})] "
            "    (run-next-task) "
            "    (tiny-clj.gpio/simulate-analog! 35 103) "
            "    (Thread/sleep 2) "
            "    (run-next-task) "
            "    (tiny-clj.gpio/simulate-analog! 35 108) "
            "    (Thread/sleep 2) "
            "    (run-next-task) "
            "    ((get w :close!)) "
            "    @events))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("analog watch threshold filtering should work with local lexical state");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_vector(result), "analog threshold watch should record events in a vector");
    TEST_ASSERT_EQUAL_INT(2, vector_count((CljPersistentVector *)result));

    ID first_event = vector_nth((CljPersistentVector *)result, 0);
    TEST_ASSERT_TRUE_MESSAGE(is_map(first_event), "first analog event should be a map");
    TEST_ASSERT_EQUAL_INT(100, as_fixnum(map_get(first_event, intern_symbol_global(":value"))));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(map_get(first_event, intern_symbol_global(":delta"))));

    ID second_event = vector_nth((CljPersistentVector *)result, 1);
    TEST_ASSERT_TRUE_MESSAGE(is_map(second_event), "second analog event should be a map");
    TEST_ASSERT_EQUAL_INT(108, as_fixnum(map_get(second_event, intern_symbol_global(":value"))));
    TEST_ASSERT_EQUAL_INT(8, as_fixnum(map_get(second_event, intern_symbol_global(":delta"))));

    event_loop_clear();
}

TEST(test_gpio_watch_defaults_to_digital_signal_when_opts_omitted) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            REQ
            "  (def gpio-watch-default-events (atom [])) "
            "  (tiny-clj.gpio/watch 10 (fn [ev] (swap! gpio-watch-default-events conj ev) nil)) "
            "  (tiny-clj.gpio/simulate! 10 1) "
            "  (run-next-task) "
            "  (let [events @gpio-watch-default-events] "
            "    (tiny-clj.gpio/watch 10 nil) "
            "    events))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("watch without opts should stay on the digital path");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_vector(result), "default watch should record vector of events");
    TEST_ASSERT_EQUAL_INT(1, vector_count((CljPersistentVector *)result));
}

TEST(test_gpio_watch_analog_nil_callback_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string(
            "(do "
            REQ
            "  (tiny-clj.gpio/watch 35 nil {:signal :analog :period-ms 1}))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("analog watch removal should stay explicit and throw on nil callback");
}

TEST(test_gpio_pwm_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/pwm! 2 1000))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/pwm! should throw on invalid arity");
}

TEST(test_gpio_pwm_stop_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/pwm-stop!))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/pwm-stop! should throw on invalid arity");
}

TEST(test_gpio_write_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/write! 2))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/write! should throw on invalid arity");
}

TEST(test_gpio_pwm_validates_duty_range) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/pwm! 2 1000 300))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/pwm! should throw on out-of-range duty");
}

TEST(test_gpio_read_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/read))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/read should throw on invalid arity");
}

TEST(test_gpio_read_analog_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/read-analog))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/read-analog should throw on invalid arity");
}

TEST(test_gpio_simulate_analog_validates_range) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/simulate-analog! 35 5000))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("tiny-clj.gpio/simulate-analog! should throw on out-of-range value");
}

TEST(test_set_pin_mode_stores_output_mode_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 2 :output) (tiny-clj.gpio/pin-mode 2))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("set-pin-mode! should store mode metadata without throwing");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_map(result), "pin-mode should return a map for configured pins");
    TEST_ASSERT_EQUAL_PTR(SYM_KW_OUTPUT,
                          map_get(result, SYM_KW_MODE));
}

TEST(test_set_pin_mode_nil_removes_mode_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 2 :output) "
            "           (tiny-clj.gpio/set-pin-mode! 2 nil) "
            "           (tiny-clj.gpio/pin-mode 2))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("set-pin-mode! nil should clear mode metadata without throwing");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "pin-mode should return nil after mode removal");
}

TEST(test_set_pin_mode_pwm_stores_freq_and_duty_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 18 :pwm {:freq 1000 :duty 128}) "
            "           (tiny-clj.gpio/pin-mode 18))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("set-pin-mode! :pwm should store metadata without throwing");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_map(result), "pin-mode should return a map for PWM-configured pins");
    TEST_ASSERT_EQUAL_PTR(SYM_KW_PWM,
                          map_get(result, SYM_KW_MODE));
    TEST_ASSERT_EQUAL_INT(1000, as_fixnum(map_get(result, SYM_KW_FREQ)));
    TEST_ASSERT_EQUAL_INT(128, as_fixnum(map_get(result, SYM_KW_DUTY)));
}

TEST(test_set_pin_mode_pwm_validates_opts) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 2 :pwm {:freq 1000}))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("set-pin-mode! :pwm without :duty should throw");
}

TEST(test_set_pin_mode_unknown_mode_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 2 :bogus))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("set-pin-mode! with unknown mode should throw");
}

TEST(test_pin_mode_returns_nil_for_unconfigured_pin) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/pin-mode 99))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-mode should return nil for unconfigured pins");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "pin-mode should return nil when no mode was set");
}

TEST(test_pin_write_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/pin-write)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/pin-write should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/pin-write should resolve to callable function");
    });
}

TEST(test_pin_read_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/pin-read)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/pin-read should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/pin-read should resolve to callable function");
    });
}

TEST(test_set_pin_mode_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/set-pin-mode!)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/set-pin-mode! should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/set-pin-mode! should resolve to callable function");
    });
}

TEST(test_pin_mode_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/pin-mode)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/pin-mode should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/pin-mode should resolve to callable function");
    });
}

TEST(test_pin_pwm_resolves_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID resolved = eval_string("(do " REQ " tiny-clj.gpio/pin-pwm!)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "tiny-clj.gpio/pin-pwm! should resolve after require");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "tiny-clj.gpio/pin-pwm! should resolve to callable function");
    });
}

TEST(test_gpio_high_and_low_constants_resolve_after_require) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        ID high = eval_string("(do " REQ " tiny-clj.gpio/HIGH)", g_test_eval_state);
        ID low = eval_string("(do " REQ " tiny-clj.gpio/LOW)", g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(high), "tiny-clj.gpio/HIGH should resolve to fixnum");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(low), "tiny-clj.gpio/LOW should resolve to fixnum");
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(high));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(low));
    });
}

TEST(test_pin_write_accepts_high_and_low_symbols_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ
            "  (tiny-clj.gpio/set-pin-mode! 4 :output) "
            "  (tiny-clj.gpio/pin-write 4 tiny-clj.gpio/HIGH) "
            "  (tiny-clj.gpio/read 4))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-write with HIGH should not throw");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "read should return fixnum after pin-write HIGH");
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));

    TRY {
        result = eval_string(
            "(do " REQ
            "  (tiny-clj.gpio/set-pin-mode! 4 :output) "
            "  (tiny-clj.gpio/pin-write 4 tiny-clj.gpio/LOW) "
            "  (tiny-clj.gpio/read 4))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-write with LOW should not throw");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "read should return fixnum after pin-write LOW");
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_pin_read_dispatches_via_input_mode) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ
            "  (tiny-clj.gpio/simulate! 7 1) "
            "  (tiny-clj.gpio/set-pin-mode! 7 :input) "
            "  (tiny-clj.gpio/pin-read 7))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-read should use digital read when mode is :input");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "pin-read should return fixnum in :input mode");
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST(test_pin_read_dispatches_via_adc_mode) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ
            "  (tiny-clj.gpio/simulate-analog! 35 1234) "
            "  (tiny-clj.gpio/set-pin-mode! 35 :adc) "
            "  (tiny-clj.gpio/pin-read 35))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-read should use analog read when mode is :adc");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "pin-read should return fixnum in :adc mode");
    TEST_ASSERT_EQUAL_INT(1234, as_fixnum(result));
}

TEST(test_pin_read_without_mode_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/pin-read 7))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("pin-read without configured mode should throw");
}

TEST(test_pin_write_dispatches_via_output_mode) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ
            "  (tiny-clj.gpio/set-pin-mode! 4 :output) "
            "  (tiny-clj.gpio/pin-write 4 tiny-clj.gpio/HIGH) "
            "  (tiny-clj.gpio/read 4))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-write should use digital write when mode is :output");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "digital write path should still update host state");
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

TEST(test_pin_write_without_mode_throws) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(do " REQ " (tiny-clj.gpio/pin-write 4 tiny-clj.gpio/HIGH))", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("pin-write without configured mode should throw");
}

TEST(test_set_pin_mode_adc_stores_mode_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 35 :adc) (tiny-clj.gpio/pin-mode 35))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("set-pin-mode! :adc should store mode metadata");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_map(result), "pin-mode should return map for :adc mode");
    TEST_ASSERT_EQUAL_PTR(SYM_KW_ADC,
                          map_get(result, SYM_KW_MODE));
}

TEST(test_set_pin_mode_dac_stores_mode_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 25 :dac) (tiny-clj.gpio/pin-mode 25))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("set-pin-mode! :dac should store mode metadata");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_map(result), "pin-mode should return map for :dac mode");
    TEST_ASSERT_EQUAL_PTR(SYM_KW_DAC,
                          map_get(result, SYM_KW_MODE));
}

TEST(test_set_pin_mode_nil_clears_dac_mode_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ " (tiny-clj.gpio/set-pin-mode! 25 :dac) "
            "           (tiny-clj.gpio/set-pin-mode! 25 nil) "
            "           (tiny-clj.gpio/pin-mode 25))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("set-pin-mode! nil should clear :dac mode metadata without throwing");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "pin-mode should return nil after clearing :dac mode");
}

TEST(test_pin_write_dispatches_via_dac_mode) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do " REQ
            "  (tiny-clj.gpio/set-pin-mode! 25 :dac) "
            "  (tiny-clj.gpio/pin-write 25 128) "
            "  (tiny-clj.gpio/read-analog 25))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-write should use analog write when mode is :dac");
        return;
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "dac write path should update analog-readable state");
    TEST_ASSERT_EQUAL_INT(128, as_fixnum(result));
}

TEST(test_pin_pwm_dispatches_to_pwm_builtin_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(do " REQ " (tiny-clj.gpio/pin-pwm! 2 1000 128))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("pin-pwm! should dispatch to pwm! without throwing");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "pin-pwm! should return nil");
}
