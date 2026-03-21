#include "tests_common.h"

TEST(test_macro_ns_asset_regression_name_returns_namespace_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string("(name 'tiny-fx.sound-demos)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("tiny-fx.sound-demos", clj_string_data(as_clj_string(result)));
}

TEST(test_macro_ns_asset_regression_str_with_nth_matches_clojure_character_behavior) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string(
        "(let [s (name 'tiny-fx.sound-demos)] "
        "  (loop [i 0 out \"\"] "
        "    (if (< i (count s)) "
        "      (recur (+ i 1) (str out (nth s i))) "
        "      out)))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("tiny-fx.sound-demos",
                             clj_string_data(as_clj_string(result)));
}

TEST(test_macro_ns_asset_regression_macro_like_prefix_matches_clojure_path_building) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = eval_string(
        "(let [s (name 'tiny-fx.sound-demos) "
        "      ns-path (loop [i 0 out \"\"] "
        "                (if (< i (count s)) "
        "                  (let [ch (nth s i)] "
        "                    (recur (+ i 1) (str out (if (= ch \\.) \"/\" ch)))) "
        "                  out))] "
        "  (str \"/assets/\" ns-path \"/minuet-in-g.edn\"))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("/assets/tiny-fx/sound-demos/minuet-in-g.edn",
                             clj_string_data(as_clj_string(result)));
}

TEST(test_macro_ns_asset_regression_current_ns_symbol_available_like_clojure) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string("(str *ns*)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Clojure/JVM-compatible behavior: *ns* should be available");
    } END_TRY

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    TEST_ASSERT_TRUE_MESSAGE(string_length(result) > 0,
                             "*ns* string should not be empty");
}

TEST(test_macro_ns_asset_regression_current_ns_path_building_like_clojure) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID result = NULL;
    TRY {
        result = eval_string(
            "(let [s (str *ns*) "
            "      ns-path (loop [i 0 out \"\"] "
            "                (if (< i (count s)) "
            "                  (let [ch (nth s i)] "
            "                    (recur (+ i 1) (str out (if (= ch \\.) \"/\" ch)))) "
            "                  out))] "
            "  (str \"/assets/\" ns-path \"/x.edn\"))",
            g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Clojure/JVM-compatible behavior: ns-name/*ns* path building should work");
    } END_TRY

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
}
