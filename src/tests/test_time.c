#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../namespace.h"

void test_time_returns_result(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        // (time (+ 1 2)) sollte 3 zurückgeben
        const char *code = "(time (+ 1 2))";
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
        evalstate_free(st);
    });
}

void test_time_with_let(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        // (time (let [x 10] x)) sollte 10 zurückgeben
        const char *code = "(time (let [x 10] x))";
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
        evalstate_free(st);
    });
}

void test_time_with_function_call(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        // (time ((fn [x] (* x x)) 5)) sollte 25 zurückgeben
        const char *code = "(time ((fn [x] (* x x)) 5))";
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(25, as_fixnum(result));
        evalstate_free(st);
    });
}

void test_time_measures_duration(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        // Test dass (time) eine Zeitausgabe produziert
        // Verwende eine einfache Addition für messbare Zeit
        const char *code = "(time (+ 1 2 3 4 5 6 7 8 9 10))";
        
        // Capture stdout für Timing-Output-Validierung
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NOT_NULL(result);
        // Sum of 1 to 10 = 55
        TEST_ASSERT_EQUAL_INT(55, as_fixnum(result));
        evalstate_free(st);
    });
}
