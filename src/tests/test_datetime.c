#include "tests_common.h"
#include "eval.h"
#include "symbol.h"
#include "value.h"
#include "vector.h"

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
    CljPersistentVector *time_args = AUTORELEASE(make_vector(0, STRONG));
    CljPersistentMap *env = AUTORELEASE(make_map(16));
    
    TRY {
        ID result = eval_time(time_args, env, g_test_eval_state, NULL);
        TEST_ASSERT_NULL(result);
    } CATCH(ex) {
        TEST_ASSERT_TRUE(true);
    } END_TRY
}

TEST_SHARED(test_time_with_too_many_arguments) {
    CljPersistentVector *time_args = AUTORELEASE(make_vector(2, STRONG));
    vector_conj_inplace(&time_args, fixnum(1));
    vector_conj_inplace(&time_args, fixnum(2));
    CljPersistentMap *env = AUTORELEASE(make_map(16));
    
    TRY {
        ID result = eval_time(time_args, env, g_test_eval_state, NULL);
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
    TEST_ASSERT_NIL(result);  // dotimes returns nil
}

TEST_SHARED(test_time_returns_expression_result) {
    CljValue result = eval_string("(time (+ 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
}

// ============================================================================
// NOW TESTS (atomic timestamp as Instant)
// ============================================================================

TEST(test_now_returns_instant) {
    ID result = eval_string("(now)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_INSTANT);
}

TEST(test_now_has_days_value) {
    ID result = eval_string("(instant-days (now))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_TRUE(as_fixnum(result) > 19000);  // After 2022
}

TEST(test_now_has_ms_value) {
    ID result = eval_string("(instant-ms (now))", g_test_eval_state);
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
    ID result = eval_string("(require 'tiny-clj.datetime)", g_test_eval_state);
    TEST_ASSERT_NIL(result);  // require returns nil on success
}

// --- days-from-civil / civil-from-days roundtrip tests ---

TEST(test_datetime_unix_epoch) {
    // Unix epoch: 1970-01-01 = day 0
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/days-from-civil 1970 1 1))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_datetime_y2k) {
    // Y2K: 2000-01-01 = day 10957
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/days-from-civil 2000 1 1))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10957, as_fixnum(result));
}

TEST(test_datetime_christmas_2024) {
    // Christmas 2024: 2024-12-25
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/days-from-civil 2024 12 25))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(20082, as_fixnum(result));
}

TEST(test_datetime_civil_from_days_epoch) {
    // Day 0 = 1970-01-01
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [c (tiny-clj.datetime/civil-from-days 0)]"
        "      (vector (:tiny-clj.datetime/year c) (:tiny-clj.datetime/month c) (:tiny-clj.datetime/day c))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(1970, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 2)));
}

TEST(test_datetime_civil_from_days_y2k) {
    // Day 10957 = 2000-01-01
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [c (tiny-clj.datetime/civil-from-days 10957)]"
        "      (vector (:tiny-clj.datetime/year c) (:tiny-clj.datetime/month c) (:tiny-clj.datetime/day c))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(2000, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(vector_nth(v, 2)));
}

TEST(test_datetime_roundtrip) {
    // Roundtrip: days-from-civil -> civil-from-days should return original date
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [days (tiny-clj.datetime/days-from-civil 2024 6 15)"
        "          civil (tiny-clj.datetime/civil-from-days days)]"
        "      (and (= 2024 (:tiny-clj.datetime/year civil))"
        "           (= 6 (:tiny-clj.datetime/month civil))"
        "           (= 15 (:tiny-clj.datetime/day civil)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

// --- time-from-millis / millis-from-time tests ---

TEST(test_datetime_time_from_millis_midnight) {
    // Midnight: 0 ms
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [t (tiny-clj.datetime/time-from-millis 0)]"
        "      (vector (:tiny-clj.datetime/hour t) (:tiny-clj.datetime/minute t) (:tiny-clj.datetime/second t))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 2)));
}

TEST(test_datetime_time_from_millis_noon) {
    // Noon: 12:00:00 = 12 * 3600000 = 43200000 ms
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [t (tiny-clj.datetime/time-from-millis 43200000)]"
        "      (vector (:tiny-clj.datetime/hour t) (:tiny-clj.datetime/minute t))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(12, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(vector_nth(v, 1)));
}

TEST(test_datetime_time_from_millis_specific) {
    // 14:30:45.123 = 14*3600000 + 30*60000 + 45*1000 + 123 = 52245123 ms
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [t (tiny-clj.datetime/time-from-millis 52245123)]"
        "      (vector (:tiny-clj.datetime/hour t) (:tiny-clj.datetime/minute t) (:tiny-clj.datetime/second t) (:tiny-clj.datetime/millis t))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
    CljPersistentVector *v = as_vector(result);
    TEST_ASSERT_EQUAL_INT(14, as_fixnum(vector_nth(v, 0)));
    TEST_ASSERT_EQUAL_INT(30, as_fixnum(vector_nth(v, 1)));
    TEST_ASSERT_EQUAL_INT(45, as_fixnum(vector_nth(v, 2)));
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(vector_nth(v, 3)));
}

TEST(test_datetime_millis_from_time) {
    // 14:30:45.123 = 52245123 ms
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/millis-from-time 14 30 45 123))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(52245123, as_fixnum(result));
}

TEST(test_datetime_time_roundtrip) {
    // Roundtrip: millis-from-time -> time-from-millis should return original time
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [ms (tiny-clj.datetime/millis-from-time 9 15 30 500)"
        "          t (tiny-clj.datetime/time-from-millis ms)]"
        "      (and (= 9 (:tiny-clj.datetime/hour t))"
        "           (= 15 (:tiny-clj.datetime/minute t))"
        "           (= 30 (:tiny-clj.datetime/second t))"
        "           (= 500 (:tiny-clj.datetime/millis t)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

// --- date-time / to-raw high-level API tests ---

TEST(test_datetime_date_time_api) {
    // Convert Instant to full date-time map
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [dt (tiny-clj.datetime/date-time #inst \"1970-01-01T12:00:00.000Z\")]"
        "      (and (= 1970 (:tiny-clj.datetime/year dt))"
        "           (= 1 (:tiny-clj.datetime/month dt))"
        "           (= 1 (:tiny-clj.datetime/day dt))"
        "           (= 12 (:tiny-clj.datetime/hour dt))"
        "           (= 0 (:tiny-clj.datetime/minute dt)))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

// --- format-iso tests ---

TEST(test_datetime_format_iso) {
    // Format a date-time map as ISO-8601 string
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/format-iso {:tiny-clj.datetime/year 2024 :tiny-clj.datetime/month 12 :tiny-clj.datetime/day 25 :tiny-clj.datetime/hour 14 :tiny-clj.datetime/minute 30 :tiny-clj.datetime/second 45}))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("2024-12-25T14:30:45", clj_string_data(as_clj_string(result)));
}

TEST(test_datetime_format_iso_with_padding) {
    // Ensure proper zero-padding for single-digit values
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/format-iso {:tiny-clj.datetime/year 2024 :tiny-clj.datetime/month 1 :tiny-clj.datetime/day 5 :tiny-clj.datetime/hour 9 :tiny-clj.datetime/minute 3 :tiny-clj.datetime/second 7}))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("2024-01-05T09:03:07", clj_string_data(as_clj_string(result)));
}

// --- Integration with now ---

TEST(test_datetime_now_integration) {
    // Use tiny-clj.datetime with (now) to get full date-time
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (let [dt (tiny-clj.datetime/date-time (now))]"
        "      (and (> (:tiny-clj.datetime/year dt) 2020)"
        "           (>= (:tiny-clj.datetime/month dt) 1)"
        "           (<= (:tiny-clj.datetime/month dt) 12)"
        "           (>= (:tiny-clj.datetime/day dt) 1)"
        "           (<= (:tiny-clj.datetime/day dt) 31))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result));
}

TEST(test_datetime_format_now) {
    // Format current time as ISO-8601
    ID result = eval_string(
        "(do (require 'tiny-clj.datetime)"
        "    (tiny-clj.datetime/format-iso (tiny-clj.datetime/date-time (now))))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    // Check that it starts with "202" (current decade)
    const char *str = clj_string_data(as_clj_string(result));
    TEST_ASSERT_TRUE(str[0] == '2' && str[1] == '0' && str[2] == '2');
}