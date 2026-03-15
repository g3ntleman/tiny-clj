#include "tests_common.h"
#include "../startup_pipeline.h"
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief Invoke shared -main helper while silencing expected stderr output.
 *
 * Used by negative-path tests to keep logs focused on assertion failures.
 */
static bool invoke_main_quiet(EvalState *st,
                              const char *ns_name,
                              int argc,
                              const char **argv,
                              bool bind_command_line_args) {
  fflush(stderr);
  int saved_stderr = dup(STDERR_FILENO);
  int sink_fd = open("/dev/null", O_WRONLY);

  bool redirected = false;
  if (saved_stderr >= 0 && sink_fd >= 0 && dup2(sink_fd, STDERR_FILENO) >= 0) {
    redirected = true;
  }
  if (sink_fd >= 0) {
    close(sink_fd);
  }

  bool ok = tinyclj_startup_invoke_main(st, ns_name, argc, argv, bind_command_line_args);

  fflush(stderr);
  if (redirected) {
    (void)dup2(saved_stderr, STDERR_FILENO);
  }
  if (saved_stderr >= 0) {
    close(saved_stderr);
  }
  return ok;
}

TEST(test_main_invoke_explicit_ns_passes_args_and_command_line_args) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  register_resolver_source(
      "/libs/test/main-entry-explicit.clj",
      "(ns test.main-entry-explicit)\n"
      "(def seen-main-args (atom nil))\n"
      "(def seen-cli-args (atom nil))\n"
      "(defn -main [& args]\n"
      "  (reset! seen-main-args args)\n"
      "  (reset! seen-cli-args *command-line-args*)\n"
      "  nil)\n");

  const char *argv_main[] = {"a", "b"};
  bool ok = tinyclj_startup_invoke_main(g_test_eval_state, "test.main-entry-explicit", 2, argv_main, true);
  TEST_ASSERT_TRUE(ok);

  ID args_ok = eval_string("(= @test.main-entry-explicit/seen-main-args '(\"a\" \"b\"))", g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, args_ok);

  ID cli_ok = eval_string("(= @test.main-entry-explicit/seen-cli-args '(\"a\" \"b\"))", g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, cli_ok);
}

TEST(test_main_invoke_current_ns_mode_uses_active_namespace) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID setup_ok = eval_string(
      "(do "
      "  (ns test.main-entry-current) "
      "  (def invoked-main-entry-current (atom false)) "
      "  (defn -main [& _args] "
      "    (reset! invoked-main-entry-current true) "
      "    nil))",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(setup_ok);

  bool ok = tinyclj_startup_invoke_main(g_test_eval_state, NULL, 0, NULL, true);
  TEST_ASSERT_TRUE(ok);

  ID invoked = eval_string("@test.main-entry-current/invoked-main-entry-current", g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, invoked);
}

TEST(test_main_invoke_returns_false_when_namespace_missing) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  bool ok = invoke_main_quiet(g_test_eval_state, "test.main-entry-missing", 0, NULL, true);
  TEST_ASSERT_FALSE(ok);
}

TEST(test_main_invoke_returns_false_when_main_missing) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  register_resolver_source(
      "/libs/test/main-entry-no-main.clj",
      "(ns test.main-entry-no-main)\n"
      "(def not-main 1)\n");

  bool ok = invoke_main_quiet(g_test_eval_state, "test.main-entry-no-main", 0, NULL, true);
  TEST_ASSERT_FALSE(ok);
}

TEST(test_main_invoke_returns_false_when_main_not_callable) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  register_resolver_source(
      "/libs/test/main-entry-not-callable.clj",
      "(ns test.main-entry-not-callable)\n"
      "(def -main 42)\n");

  bool ok = invoke_main_quiet(g_test_eval_state, "test.main-entry-not-callable", 0, NULL, true);
  TEST_ASSERT_FALSE(ok);
}

TEST(test_startup_language_bootstrap_builtins_is_idempotent) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  TinycljLanguageBootstrapOptions opts = {
      .ensure_builtins = true,
      .load_core = false,
      .load_repl = false,
      .refer_repl = false,
      .core_quiet = true,
  };

  TEST_ASSERT_TRUE(tinyclj_startup_bootstrap_language(g_test_eval_state, &opts));
  TEST_ASSERT_TRUE(tinyclj_startup_bootstrap_language(g_test_eval_state, &opts));
}
