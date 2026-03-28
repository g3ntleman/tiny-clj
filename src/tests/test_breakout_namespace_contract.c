#include "test_breakout_helpers.h"

TEST(test_breakout_namespace_contract_files_are_scoped_under_tiny_breakout) {
    const char *files[] = {"core.clj", "scene.clj", "audio.clj", "levels.clj"};
    for (unsigned int i = 0; i < (unsigned int)(sizeof(files) / sizeof(files[0])); i++) {
        size_t len = 0;
        char *src = read_breakout_source(files[i], &len);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "(ns tiny-breakout."),
                                     "every breakout file must declare tiny-breakout.* namespace");
        CLJ_FREE(src);
    }
}

TEST(test_breakout_namespace_contract_forbids_non_tiny_dependencies) {
    const char *files[] = {"core.clj", "scene.clj", "audio.clj", "levels.clj"};
    for (unsigned int i = 0; i < (unsigned int)(sizeof(files) / sizeof(files[0])); i++) {
        size_t len = 0;
        char *src = read_breakout_source(files[i], &len);
        TEST_ASSERT_TRUE(len > 0);

        TEST_ASSERT_NULL_MESSAGE(strstr(src, "[tiny-db."),
                                 "tiny-breakout namespaces must not require tiny-db.*");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "[clojure."),
                                 "tiny-breakout namespaces must not require clojure.* helper namespaces");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "[user"),
                                 "tiny-breakout namespaces must not require user namespaces");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-fx.startup"),
                                 "tiny-breakout namespaces must not require tiny-fx.startup");
        TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-fx.game-demo"),
                                 "tiny-breakout namespaces must not require tiny-fx.game-demo");

        CLJ_FREE(src);
    }
}

TEST(test_breakout_namespace_contract_forbids_native_bindings_in_game_namespaces) {
    const char *files[] = {"core.clj", "scene.clj", "audio.clj", "levels.clj", "runtime.clj"};
    for (unsigned int i = 0; i < (unsigned int)(sizeof(files) / sizeof(files[0])); i++) {
        size_t len = 0;
        char *src = read_breakout_source(files[i], &len);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_NULL_MESSAGE(strstr(src, ":native"),
                                 "tiny-breakout namespaces must not declare :native bindings");
        CLJ_FREE(src);
    }
}

TEST(test_breakout_namespace_contract_deployment_routes_through_tiny_breakout) {
    size_t len = 0;
    char *src = read_deployment_source(&len);
    TEST_ASSERT_TRUE(len > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(src, "[tiny-breakout.runtime"),
                                 "deployment namespace should require tiny-breakout.runtime");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-fx.game-demo"),
                             "deployment namespace must not route breakout through tiny-fx.game-demo");
    TEST_ASSERT_NULL_MESSAGE(strstr(src, "tiny-fx.startup"),
                             "deployment namespace must not route breakout through tiny-fx.startup");

    CLJ_FREE(src);
}
