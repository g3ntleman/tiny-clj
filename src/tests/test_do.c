#include "tests_common.h"
#include "../tiny_clj.h"
#include "memory.h"
#include "../symbol.h"
#include "value.h"

// Test empty do returns nil
TEST_SHARED(test_do_empty) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do)";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NULL(result);
  });
}

// Test do with single expression
TEST_SHARED(test_do_single_expr) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do 42)";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
  });
}

// Test do with multiple expressions returns last
TEST_SHARED(test_do_multiple_exprs) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do 1 2 3)";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
  });
}

// Test do with arithmetic expressions
TEST_SHARED(test_do_with_arithmetic, 0) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do (+ 1 1) (+ 2 2) (+ 3 3))";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
  });
}

// Test nested do forms
TEST_SHARED(test_do_nested) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do (do 1 2) (do 3 4))";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(result));
  });
}

// Test do in if statement
TEST_SHARED(test_do_in_if, 0) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(if true (do (+ 1 1) 10) 20)";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
  });
}

// Test do in if else branch
TEST_SHARED(test_do_in_if_else, 0) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(if false 1 (do (+ 2 2) 20))";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
  });
}

// Test do with mixed types
TEST_SHARED(test_do_mixed_types) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do 42 true nil 99)";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(result));
  });
}

// Test do returns nil as last expression
TEST_SHARED(test_do_last_nil) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(do 42 nil)";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NULL(result);
  });
}

// Test do with let binding
TEST_SHARED(test_do_with_let, 0) {
  WITH_AUTORELEASE_POOL({
    const char *code = "(let [x 5] (do (+ x 1) (+ x 2)))";
    CljValue result = eval_string(code, g_test_eval_state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(7, as_fixnum(result));
  });
}
