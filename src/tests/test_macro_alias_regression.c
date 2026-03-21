#include "tests_common.h"

static void register_macro_alias_regression_sources(void) {
    register_resolver_source(
        "/libs/test/macro-provider.clj",
        "(ns test.macro-provider)\n"
        "(defmacro emit-ok [x] (list 'str \"ok-\" x))\n");

    register_resolver_source(
        "/libs/test/macro-qualified-user.clj",
        "(ns test.macro-qualified-user)\n"
        "(require 'test.macro-provider)\n"
        "(defn run [] (test.macro-provider/emit-ok \"x\"))\n");

    register_resolver_source(
        "/libs/test/macro-alias-user.clj",
        "(ns test.macro-alias-user\n"
        "  (:require [test.macro-provider :as mp]))\n"
        "(defn run [] (mp/emit-ok \"x\"))\n");
}

TEST(test_macro_alias_regression_control_qualified_macro_call_works) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    register_macro_alias_regression_sources();

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do (require 'test.macro-qualified-user) (test.macro-qualified-user/run))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("control test failed: qualified macro call should work");
    } END_TRY

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("ok-x", clj_string_data(as_clj_string(result)));
}

TEST(test_macro_alias_regression_alias_macro_call_matches_qualified_call) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    register_macro_alias_regression_sources();

    ID result = NULL;
    TRY {
        result = eval_string(
            "(do (require 'test.macro-alias-user) (test.macro-alias-user/run))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE(
            "regression: macro call via namespace alias in ns :require should work like in Clojure");
    } END_TRY

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_STRING,
                             "regression: alias-based macro call should return string result");
    TEST_ASSERT_EQUAL_STRING("ok-x", clj_string_data(as_clj_string(result)));
}
