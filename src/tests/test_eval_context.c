/*
 * Unit Tests for EvalContext propagation
 * 
 * Tests to verify that ctx is correctly passed through the call chain:
 * eval_body_with_params -> eval_list_with_context -> resolve_list_operator
 */

#include "tests_common.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// Test that reproduces the original problem: recursive function calling itself
// This should trigger the assertion if ctx is NULL when resolve_list_operator is called
// The problem occurs when a function calls itself recursively and ctx is lost
TEST(test_recursive_function_ctx_propagation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a recursive function that calls itself
    // This mimics the join function behavior: (defn my-join [sep coll] ... (my-join sep rest-coll))
    CljObject *result = NULL;
    TRY {
        // Define a recursive function that calls itself
        // This will test if ctx is correctly passed through recursive calls
        eval_string("(defn my-join [sep coll] (if (empty? coll) \"\" (if (empty? (rest coll)) (first coll) (str (first coll) sep (my-join sep (rest coll))))))", g_test_eval_state);
        
        // Call it - this should trigger recursive calls where ctx might be lost
        // If ctx is NULL when resolve_list_operator is called, the assertion will fail
        result = eval_string("(my-join \",\" [\"a\" \"b\" \"c\"])", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "my-join should return a result");
        TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
        CljString *str = as_clj_string(result);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("a,b,c", clj_string_data(str), "my-join should concatenate strings");
    } CATCH(ex) {
        char msg[512];
        test_snprintf(msg, sizeof(msg), "test_recursive_function_ctx_propagation threw exception: %s - %s",
                ex ? ex->type : "unknown", ex ? ex->message : "no message");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
}

// Test that reproduces the original problem with a simpler recursive function
// This tests if ctx is correctly passed through recursive function calls
TEST(test_simple_recursive_function_ctx_propagation) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Define a simple recursive function that calls itself
    // This will test if ctx is correctly passed through recursive calls
    CljObject *result = NULL;
    TRY {
        // Define a simple recursive function
        eval_string("(defn fact [n] (if (<= n 1) 1 (* n (fact (- n 1)))))", g_test_eval_state);
        
        // Call it - this should trigger recursive calls where ctx might be lost
        result = eval_string("(fact 5)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "fact should return a result");
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(120, as_fixnum(result));
    } CATCH(ex) {
        char msg[512];
        test_snprintf(msg, sizeof(msg), "test_simple_recursive_function_ctx_propagation threw exception: %s - %s",
                ex ? ex->type : "unknown", ex ? ex->message : "no message");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
}

// Test that ctx is not NULL when eval_body_with_params is called
TEST(test_eval_body_with_params_ctx_not_null) {
    // Create a symbol for parameter 'x'
    CljSymbol *x_sym = intern_symbol_global("x");
    TEST_ASSERT_NOT_NULL(x_sym);
    
    // Set up CallFrame with parameters
    ID params[] = {x_sym};
    ID values[] = {fixnum(42)};
    CallFrame call_frame;
    frame_init(&call_frame, NULL);
    frame_set_bindings(&call_frame, NULL, params, values, 1);
    
    // Create EvalContext with frame
    EvalContext ctx = {
        .env = NULL,
        .env_stack = NULL,
        .frame = &call_frame,
        .st = g_test_eval_state,
        .recur_args = NULL,
        .recur_arg_count = NULL,
        .recur_param_count = 1
    };
    
    // Test: eval_body_with_params should resolve 'x' to 42
    ID result = eval_body_with_params(x_sym, &ctx);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "eval_body_with_params should return result");
    TEST_ASSERT_EQUAL_INT_MESSAGE(42, as_fixnum(result), "x should resolve to 42");
    
    frame_release(&call_frame);
}

// Regression test: closures must capture the correct lexical binding under shadowing.
// (let [x 1] (let [f (fn [] x)] (let [x 2] (f)))) => 1
TEST(test_closure_capture_shadowing) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    CljObject *result = NULL;
    TRY {
        result = eval_string(
            "(let [x 1]"
            "  (let [f (fn [] x)]"
            "    (let [x 2]"
            "      (f))))",
            g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "expression should return a result");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "result should be a fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, as_fixnum(result), "closure must see captured x=1, not inner x=2");
    } CATCH(ex) {
        char msg[512];
        test_snprintf(msg, sizeof(msg), "test_closure_capture_shadowing threw exception: %s - %s",
                 ex ? ex->type : "unknown", ex ? ex->message : "no message");
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
}
