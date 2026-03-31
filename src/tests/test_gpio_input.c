/*
 * Tests for tiny-clj.board, tiny-clj.button, and tiny-clj.sensor.
 */

#include "tests_common.h"
#include "builtins.h"
#include "strings.h"
#include "symbol.h"
#include "to_string.h"

#define BUTTON_REQ "(require 'tiny-clj.gpio) (require 'tiny-clj.board) (require 'tiny-clj.button) "
#define SENSOR_REQ "(require 'tiny-clj.gpio) (require 'tiny-clj.board) (require 'tiny-clj.sensor) "

TEST(test_native_lookup_finds_button_watch_native) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.button/watch-native");
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_NOT_NULL(native_function_lookup(sym));
    });
}

TEST(test_native_lookup_finds_sensor_watch_native) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("tiny-clj.sensor/watch-native");
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_NOT_NULL(native_function_lookup(sym));
    });
}

TEST(test_button_watch_emits_down_up_and_click_events) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            BUTTON_REQ
            "  (def button-events-a (atom [])) "
            "  (tiny-clj.button/watch :demo/launch (fn [ev] (swap! button-events-a conj (:kind ev)) nil)) "
            "  (tiny-clj.gpio/simulate! 1 1) "
            "  (Thread/sleep 30) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.gpio/simulate! 1 0) "
            "  (Thread/sleep 30) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.button/watch :demo/launch nil) "
            "  @button-events-a)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.button/watch should handle simulated click path");
        return;
    } END_TRY

    CljString *rendered = to_string(result);
    TEST_ASSERT_NOT_NULL(rendered);
    const char *data = string_data(rendered);
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/down"));
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/up"));
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/click"));
}

TEST(test_button_watch_emits_hold_event) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            BUTTON_REQ
            "  (def button-events-b (atom [])) "
            "  (tiny-clj.button/watch :demo/launch "
            "                        (fn [ev] (swap! button-events-b conj (:kind ev)) nil) "
            "                        {:hold-ms 30}) "
            "  (tiny-clj.gpio/simulate! 1 1) "
            "  (Thread/sleep 30) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (Thread/sleep 45) "
            "  (dotimes [_ 16] (run-next-task)) "
            "  (tiny-clj.button/watch :demo/launch nil) "
            "  @button-events-b)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.button/watch should emit hold after hold-ms");
        return;
    } END_TRY

    CljString *rendered = to_string(result);
    TEST_ASSERT_NOT_NULL(rendered);
    const char *data = string_data(rendered);
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/down"));
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/hold"));
}

TEST(test_button_watch_does_not_use_periodic_gpio_input_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID canceled = NULL;
    TRY {
        canceled = eval_string(
            "(do "
            BUTTON_REQ
            "  (tiny-clj.button/watch :demo/launch (fn [_] nil)) "
            "  (let [v (cancel-timer :gpio-input-runtime)] "
            "    (tiny-clj.button/watch :demo/launch nil) "
            "    v))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("button/watch should not require the periodic :gpio-input-runtime timer");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(canceled);
    TEST_ASSERT_TRUE(is_special(canceled));
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(canceled));
}

TEST(test_sensor_watch_emits_change_events) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            SENSOR_REQ
            "  (def sensor-events-a (atom [])) "
            "  (tiny-clj.gpio/simulate-analog! 35 1000) "
            "  (tiny-clj.sensor/watch :battery (fn [ev] (swap! sensor-events-a conj (:kind ev)) nil) "
            "                         {:sample-period-ms 5}) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.gpio/simulate-analog! 35 1040) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.sensor/watch :battery nil) "
            "  @sensor-events-a)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.sensor/watch should emit change events on analog updates");
        return;
    } END_TRY

    CljString *rendered = to_string(result);
    TEST_ASSERT_NOT_NULL(rendered);
    const char *data = string_data(rendered);
    TEST_ASSERT_NOT_NULL(strstr(data, ":sensor/change"));
}

TEST(test_sensor_watch_emits_threshold_active_and_inactive_events) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            SENSOR_REQ
            "  (def sensor-events-b (atom [])) "
            "  (tiny-clj.gpio/simulate-analog! 36 2200) "
            "  (tiny-clj.sensor/watch :trigger (fn [ev] (swap! sensor-events-b conj (:kind ev)) nil)) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.gpio/simulate-analog! 36 2500) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (Thread/sleep 25) "
            "  (dotimes [_ 16] (run-next-task)) "
            "  (tiny-clj.gpio/simulate-analog! 36 2200) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (Thread/sleep 25) "
            "  (dotimes [_ 16] (run-next-task)) "
            "  (tiny-clj.sensor/watch :trigger nil) "
            "  @sensor-events-b)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.sensor/watch should emit threshold state transitions");
        return;
    } END_TRY

    CljString *rendered = to_string(result);
    TEST_ASSERT_NOT_NULL(rendered);
    const char *data = string_data(rendered);
    TEST_ASSERT_NOT_NULL(strstr(data, ":sensor/threshold-crossed"));
    TEST_ASSERT_NOT_NULL(strstr(data, ":sensor/active"));
    TEST_ASSERT_NOT_NULL(strstr(data, ":sensor/inactive"));
}

TEST(test_sensor_watch_uses_periodic_gpio_input_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID canceled = NULL;
    TRY {
        canceled = eval_string(
            "(do "
            SENSOR_REQ
            "  (tiny-clj.gpio/simulate-analog! 35 1000) "
            "  (tiny-clj.sensor/watch :battery (fn [_] nil) {:sample-period-ms 5}) "
            "  (let [v (cancel-timer :gpio-input-runtime)] "
            "    (tiny-clj.sensor/watch :battery nil) "
            "    v))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("sensor/watch should keep using periodic :gpio-input-runtime timer");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL(canceled);
    TEST_ASSERT_TRUE(is_special(canceled));
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(canceled));
}

TEST(test_event_on_button_descriptor_subscription_receives_button_events) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            "  (require 'tiny-clj.gpio) "
            "  (require 'tiny-clj.event) "
            "  (def event-clicks-a (atom [])) "
            "  (tiny-clj.event/on {:source :button :id :demo/launch} "
            "                     (fn [ev] (swap! event-clicks-a conj (:kind ev)) nil)) "
            "  (tiny-clj.gpio/simulate! 1 1) "
            "  (Thread/sleep 30) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.gpio/simulate! 1 0) "
            "  (Thread/sleep 30) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.event/on {:source :button :id :demo/launch} nil) "
            "  @event-clicks-a)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.event/on should support descriptor-based button subscriptions");
        return;
    } END_TRY

    CljString *rendered = to_string(result);
    TEST_ASSERT_NOT_NULL(rendered);
    const char *data = string_data(rendered);
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/down"));
    TEST_ASSERT_NOT_NULL(strstr(data, ":button/click"));
}

TEST(test_event_on_sensor_short_form_emits_active_event) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do "
            "  (require 'tiny-clj.gpio) "
            "  (require 'tiny-clj.event) "
            "  (def event-sensor-a (atom [])) "
            "  (tiny-clj.gpio/simulate-analog! 36 2200) "
            "  (tiny-clj.event/on :sensor :trigger "
            "                     (fn [ev] (swap! event-sensor-a conj (:kind ev)) nil)) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (tiny-clj.gpio/simulate-analog! 36 2500) "
            "  (Thread/sleep 10) "
            "  (dotimes [_ 8] (run-next-task)) "
            "  (Thread/sleep 25) "
            "  (dotimes [_ 16] (run-next-task)) "
            "  (tiny-clj.event/on {:source :sensor :id :trigger} nil) "
            "  @event-sensor-a)",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("tiny-clj.event/on should support short-form sensor subscriptions");
        return;
    } END_TRY

    CljString *rendered = to_string(result);
    TEST_ASSERT_NOT_NULL(rendered);
    const char *data = string_data(rendered);
    TEST_ASSERT_NOT_NULL(strstr(data, ":sensor/active"));
}
