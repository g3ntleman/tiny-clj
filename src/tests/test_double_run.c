#include "tests_common.h"
#include "../eval.h"
extern const char *g_expr_for_basic_list_comprehension;

TEST(test_double_run_loop) {
  WITH_MEMORY_PROFILING({
    ID result1 = eval_string(g_expr_for_basic_list_comprehension, g_test_eval_state);
    RELEASE(result1);
  });
}
REGISTER_TEST(test_double_run_loop)
