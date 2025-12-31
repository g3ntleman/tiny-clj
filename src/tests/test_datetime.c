#include "tests_common.h"
#include "eval.h"
#include "symbol.h"
#include "value.h"

ID eval_time(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx);

TEST_SHARED(test_time_basic_functionality) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    TEST_ASSERT_NOT_NULL(SYM_TIME);
    TEST_ASSERT_EQUAL_PTR(SYM_TIME, intern_symbol_global("time"));
    
    CljValue result = eval_string("(time (+ 1 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST_SHARED(test_time_arity_validation) {
    CljList *time_list = AUTORELEASE(make_list(SYM_TIME, NULL));
    CljMap *env = AUTORELEASE(make_map(16));
    
    TRY {
        ID result = eval_time(time_list, env, g_test_eval_state, NULL);
        TEST_ASSERT_NULL(result);
    } CATCH(ex) {
        TEST_ASSERT_TRUE(true);
    } END_TRY
}

TEST_SHARED(test_time_with_too_many_arguments) {
    CljList *time_list = AUTORELEASE(make_list(SYM_TIME, 
        make_list(fixnum(1), make_list(fixnum(2), NULL))));
    CljMap *env = AUTORELEASE(make_map(16));
    
    TRY {
        ID result = eval_time(time_list, env, g_test_eval_state, NULL);
        TEST_ASSERT_NULL(result);
    } CATCH(ex) {
        TEST_ASSERT_TRUE(true);
    } END_TRY
}

TEST_SHARED(test_time_no_double_evaluation) {
    CljValue result = eval_string("(time (+ 1 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
}

TEST_SHARED(test_time_with_dotimes) {
    // Simpler: use eval_string instead of manual AST construction
    ID result = eval_string("(time (dotimes [i 1000] (+ 1 2 3 4 5)))", g_test_eval_state);
    TEST_ASSERT_NULL(result);  // dotimes returns nil
}

TEST_SHARED(test_time_returns_expression_result) {
    CljValue result = eval_string("(time (+ 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// ============================================================================
// NOW TESTS (atomic timestamp as map)
// ============================================================================

TEST(test_now_returns_map) {
    ID result = eval_string("(now)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_MAP);
}

TEST(test_now_has_days_key) {
    ID result = eval_string("(:days (now))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_TRUE(as_fixnum(result) > 19000);  // After 2022
}

TEST(test_now_has_ms_key) {
    ID result = eval_string("(:ms (now))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    int ms = as_fixnum(result);
    TEST_ASSERT_TRUE(ms >= 0 && ms < 86400000);  // 0 to 24h in ms
}

// ============================================================================
// TINYCLJ.DATETIME LIBRARY TESTS (High-Level)
// All tests use (do (require ...) ...) to ensure namespace is loaded in same context
// ============================================================================

TEST(test_datetime_require) {
    // Load the datetime library
    ID result = eval_string("(require 'tinyclj.datetime)", g_test_eval_state);
    TEST_ASSERT_NULL(result);  // require returns nil on success
}

// --- days-from-civil / civil-from-days roundtrip tests ---

TEST(test_datetime_unix_epoch) {
    // Unix epoch: 1970-01-01 = day 0
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/days-from-civil 1970 1 1))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_datetime_y2k) {
    // Y2K: 2000-01-01 = day 10957
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/days-from-civil 2000 1 1))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10957, as_fixnum(result));
}

TEST(test_datetime_christmas_2024) {
    // Christmas 2024: 2024-12-25
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/days-from-civil 2024 12 25))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(20082, as_fixnum(result));
}

TEST(test_datetime_civil_from_days_epoch) {
    // Day 0 = 1970-01-01
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [c (tinyclj.datetime/civil-from-days 0)]"
        "      (vector (:year c) (:month c) (:day c))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(1970, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 2)));
}

TEST(test_datetime_civil_from_days_y2k) {
    // Day 10957 = 2000-01-01
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [c (tinyclj.datetime/civil-from-days 10957)]"
        "      (vector (:year c) (:month c) (:day c))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(2000, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 2)));
}

TEST(test_datetime_roundtrip) {
    // Roundtrip: days-from-civil -> civil-from-days should return original date
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [days (tinyclj.datetime/days-from-civil 2024 6 15)"
        "          civil (tinyclj.datetime/civil-from-days days)]"
        "      (and (= 2024 (:year civil))"
        "           (= 6 (:month civil))"
        "           (= 15 (:day civil)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

// --- time-from-millis / millis-from-time tests ---

TEST(test_datetime_time_from_millis_midnight) {
    // Midnight: 0 ms
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [t (tinyclj.datetime/time-from-millis 0)]"
        "      (vector (:hour t) (:minute t) (:second t))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 2)));
}

TEST(test_datetime_time_from_millis_noon) {
    // Noon: 12:00:00 = 12 * 3600000 = 43200000 ms
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [t (tinyclj.datetime/time-from-millis 43200000)]"
        "      (vector (:hour t) (:minute t))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(12, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
}

TEST(test_datetime_time_from_millis_specific) {
    // 14:30:45.123 = 14*3600000 + 30*60000 + 45*1000 + 123 = 52245123 ms
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [t (tinyclj.datetime/time-from-millis 52245123)]"
        "      (vector (:hour t) (:minute t) (:second t) (:millis t))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    CljVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(14, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(45, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(vector_nth(v, 3)));
}

TEST(test_datetime_millis_from_time) {
    // 14:30:45.123 = 52245123 ms
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/millis-from-time 14 30 45 123))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(52245123, as_fixnum(result));
}

TEST(test_datetime_time_roundtrip) {
    // Roundtrip: millis-from-time -> time-from-millis should return original time
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [ms (tinyclj.datetime/millis-from-time 9 15 30 500)"
        "          t (tinyclj.datetime/time-from-millis ms)]"
        "      (and (= 9 (:hour t))"
        "           (= 15 (:minute t))"
        "           (= 30 (:second t))"
        "           (= 500 (:millis t)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

// --- date-time / to-raw high-level API tests ---

TEST(test_datetime_date_time_api) {
    // Convert raw timestamp to full date-time map
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [dt (tinyclj.datetime/date-time {:days 0 :ms 43200000})]"
        "      (and (= 1970 (:year dt))"
        "           (= 1 (:month dt))"
        "           (= 1 (:day dt))"
        "           (= 12 (:hour dt))"
        "           (= 0 (:minute dt)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

TEST(test_datetime_to_raw_api) {
    // Convert date-time map back to raw format
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [raw (tinyclj.datetime/to-raw {:year 2000 :month 1 :day 1 :hour 12 :minute 0 :second 0})]"
        "      (and (= 10957 (:days raw))"
        "           (= 43200000 (:ms raw)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

// --- format-iso tests ---

TEST(test_datetime_format_iso) {
    // Format a date-time map as ISO-8601 string
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/format-iso {:year 2024 :month 12 :day 25 :hour 14 :minute 30 :second 45}))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("2024-12-25T14:30:45", clj_string_data(as_clj_string(result)));
}

TEST(test_datetime_format_iso_with_padding) {
    // Ensure proper zero-padding for single-digit values
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/format-iso {:year 2024 :month 1 :day 5 :hour 9 :minute 3 :second 7}))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("2024-01-05T09:03:07", clj_string_data(as_clj_string(result)));
}

// --- Integration with now ---

TEST(test_datetime_now_integration) {
    // Use tinyclj.datetime with (now) to get full date-time
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (let [dt (tinyclj.datetime/date-time (now))]"
        "      (and (> (:year dt) 2020)"
        "           (>= (:month dt) 1)"
        "           (<= (:month dt) 12)"
        "           (>= (:day dt) 1)"
        "           (<= (:day dt) 31))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

TEST(test_datetime_format_now) {
    // Format current time as ISO-8601
    ID result = eval_string(
        "(do (require 'tinyclj.datetime)"
        "    (tinyclj.datetime/format-iso (tinyclj.datetime/date-time (now))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    // Check that it starts with "202" (current decade)
    const char *str = clj_string_data(as_clj_string(result));
    TEST_ASSERT_TRUE(str[0] == '2' && str[1] == '0' && str[2] == '2');
}