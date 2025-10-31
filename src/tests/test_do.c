#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../symbol.h"
#include "../value.h"

// Test empty do returns nil
TEST(test_do_empty) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do)";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NULL(result);
        
    evalstate_free(st);

}

// Test do with single expression
TEST(test_do_single_expr) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do 42)";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
        
    evalstate_free(st);

}

// Test do with multiple expressions returns last
TEST(test_do_multiple_exprs) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do 1 2 3)";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
        
    evalstate_free(st);

}

// Test do with arithmetic expressions
TEST(test_do_with_arithmetic) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do (+ 1 1) (+ 2 2) (+ 3 3))";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(6, as_fixnum(result));
        
    evalstate_free(st);

}

// Test nested do forms
TEST(test_do_nested) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do (do 1 2) (do 3 4))";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(result));
        
    evalstate_free(st);

}

// Test do in if statement
TEST(test_do_in_if) {
    EvalState *st = evalstate_new();
        
    const char *code = "(if true (do (+ 1 1) 10) 20)";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(10, as_fixnum(result));
        
    evalstate_free(st);

}

// Test do in if else branch
TEST(test_do_in_if_else) {
    EvalState *st = evalstate_new();
        
    const char *code = "(if false 1 (do (+ 2 2) 20))";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
        
    evalstate_free(st);

}

// Test do with mixed types
TEST(test_do_mixed_types) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do 42 true nil 99)";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(99, as_fixnum(result));
        
    evalstate_free(st);

}

// Test do returns nil as last expression
TEST(test_do_last_nil) {
    EvalState *st = evalstate_new();
        
    const char *code = "(do 42 nil)";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NULL(result);
        
    evalstate_free(st);

}

// Test do with let binding
TEST(test_do_with_let) {
    EvalState *st = evalstate_new();
        
    const char *code = "(let [x 5] (do (+ x 1) (+ x 2)))";
    CljValue result = eval_string(code, st);
        
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(7, as_fixnum(result));
        
    evalstate_free(st);

}
