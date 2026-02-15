/*
 * Tests for gpio-write!/gpio-read/gpio-pwm! native function wiring and contract.
 *
 * Test-first scope:
 * - symbol/native lookup wiring
 * - clojure.core resolution
 * - host stub behavior (write returns nil, read returns fixnum, pwm returns nil)
 * - arity validation
 */

#include "tests_common.h"
#include "builtins.h"
#include "symbol.h"

TEST(test_native_lookup_finds_gpio_write) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("gpio-write!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "gpio-write! symbol should exist");

        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find gpio-write!");
    });
}

TEST(test_native_lookup_finds_gpio_read) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("gpio-read");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "gpio-read symbol should exist");

        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find gpio-read");
    });
}

TEST(test_native_lookup_finds_gpio_pwm) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("gpio-pwm!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "gpio-pwm! symbol should exist");

        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find gpio-pwm!");
    });
}

TEST(test_native_lookup_finds_gpio_pwm_stop) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        CljSymbol *sym = intern_symbol_global("gpio-pwm-stop!");
        TEST_ASSERT_NOT_NULL_MESSAGE(sym, "gpio-pwm-stop! symbol should exist");

        BuiltinFn native_fn = native_function_lookup(sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_fn, "native_function_lookup should find gpio-pwm-stop!");
    });
}

TEST(test_gpio_write_resolves_in_user_namespace) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        ID resolved = eval_string("gpio-write!", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "gpio-write! should resolve from user namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "gpio-write! should resolve to callable function");
    });
}

TEST(test_gpio_read_resolves_in_user_namespace) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        ID resolved = eval_string("gpio-read", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "gpio-read should resolve from user namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "gpio-read should resolve to callable function");
    });
}

TEST(test_gpio_pwm_resolves_in_user_namespace) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        ID resolved = eval_string("gpio-pwm!", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "gpio-pwm! should resolve from user namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "gpio-pwm! should resolve to callable function");
    });
}

TEST(test_gpio_pwm_stop_resolves_in_user_namespace) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();

        ID resolved = eval_string("gpio-pwm-stop!", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "gpio-pwm-stop! should resolve from user namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(resolved) == CLJ_FUNC || TAG(resolved) == CLJ_CLOSURE,
                                 "gpio-pwm-stop! should resolve to callable function");
    });
}

TEST(test_gpio_write_call_returns_nil_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(gpio-write! 2 1)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("gpio-write! with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "gpio-write! should return nil");
}

TEST(test_gpio_pwm_call_returns_nil_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(gpio-pwm! 2 1000 128)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("gpio-pwm! with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "gpio-pwm! should return nil");
}

TEST(test_gpio_pwm_stop_call_returns_nil_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(gpio-pwm-stop! 2)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("gpio-pwm-stop! with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NIL_MESSAGE(result, "gpio-pwm-stop! should return nil");
}

TEST(test_gpio_read_call_returns_fixnum_on_host) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(gpio-read 2)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("gpio-read with valid args should not throw");
        return;
    } END_TRY

    TEST_ASSERT_NOT_NULL_MESSAGE(result, "gpio-read should return a value");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "gpio-read should return fixnum");
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(result));
}

TEST(test_gpio_pwm_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(gpio-pwm! 2 1000)", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("gpio-pwm! should throw on invalid arity");
}

TEST(test_gpio_pwm_stop_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(gpio-pwm-stop!)", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("gpio-pwm-stop! should throw on invalid arity");
}

TEST(test_gpio_write_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(gpio-write! 2)", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("gpio-write! should throw on invalid arity");
}

TEST(test_gpio_pwm_validates_duty_range) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(gpio-pwm! 2 1000 300)", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("gpio-pwm! should throw on out-of-range duty");
}

TEST(test_gpio_read_validates_arity) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    TRY {
        (void)eval_string("(gpio-read)", g_test_eval_state);
    } CATCH(ex) {
        TEST_PASS();
        return;
    } END_TRY

    TEST_FAIL_MESSAGE("gpio-read should throw on invalid arity");
}
