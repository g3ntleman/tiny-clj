/*
 * Low-level tests for eval_body with vectors
 * 
 * Tests whether eval_body correctly evaluates vectors with symbols
 * in different contexts (let, for*, etc.)
 */

#include "tests_common.h"
#include "../eval.h"
#include "../vector.h"
#include "../map.h"
#include "../symbol.h"
#include "../env_stack.h"

// ============================================================================
// TEST: eval_body with vector containing symbols and env_stack
// ============================================================================

TEST_SHARED(test_eval_body_vector_with_env_stack) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a vector [x y] where x and y are symbols
    CljSymbol *x_sym = intern_symbol_global("x");
    CljSymbol *y_sym = intern_symbol_global("y");
    
    CljVector *vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(vec, vector_conj(vec, (ID)x_sym));
    ASSIGN(vec, vector_conj(vec, (ID)y_sym));
    
    // Create env_stack with bindings: x=1, y=2
    CljVector *env_stack = NULL;
    CljMap *x_binding = map_assoc(map_empty(), (ID)x_sym, fixnum(1));
    CljMap *y_binding = map_assoc(map_empty(), (ID)y_sym, fixnum(2));
    env_stack_push_inplace(&env_stack, x_binding);
    env_stack_push_inplace(&env_stack, y_binding);
    RELEASE(x_binding);
    RELEASE(y_binding);
    
    // Create EvalContext with env_stack
    EvalContext ctx = {
        .env = NULL,
        .env_stack = env_stack,
        .frame = NULL,
        .st = g_test_eval_state,
        .recur_args = NULL,
        .recur_arg_count = NULL,
        .recur_param_count = 0
    };
    
    // Evaluate vector
    ID result = eval_body((ID)vec, NULL, g_test_eval_state, &ctx);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    
    CljVector *result_vec = (CljVector*)result;
    TEST_ASSERT_EQUAL_INT(2, vector_count(result_vec));
    
    // Check first element (should be 1, not symbol x)
    ID first = vector_nth(result_vec, 0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first));
    
    // Check second element (should be 2, not symbol y)
    ID second = vector_nth(result_vec, 1);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_TRUE(is_fixnum(second));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(second));
    
    RELEASE(env_stack);
}

// ============================================================================
// TEST: eval_body with vector containing symbols and base_env
// ============================================================================

TEST_SHARED(test_eval_body_vector_with_base_env) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a vector [x y] where x and y are symbols
    CljSymbol *x_sym = intern_symbol_global("x");
    CljSymbol *y_sym = intern_symbol_global("y");
    
    CljVector *vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(vec, vector_conj(vec, (ID)x_sym));
    ASSIGN(vec, vector_conj(vec, (ID)y_sym));
    
    // Create base_env with bindings: x=1, y=2
    CljMap *base_env = map_assoc(map_empty(), (ID)x_sym, fixnum(1));
    ASSIGN(base_env, map_assoc(base_env, (ID)y_sym, fixnum(2)));
    
    // Evaluate vector using eval_body with base_env
    EvalContext ctx = {
        .env = base_env,
        .env_stack = NULL,
        .frame = NULL,
        .st = g_test_eval_state,
        .recur_args = NULL,
        .recur_arg_count = NULL,
        .recur_param_count = 0
    };
    ID result = eval_body((ID)vec, base_env, g_test_eval_state, &ctx);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    
    CljVector *result_vec = (CljVector*)result;
    TEST_ASSERT_EQUAL_INT(2, vector_count(result_vec));
    
    // Check first element (should be 1, not symbol x)
    ID first = vector_nth(result_vec, 0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first));
    
    // Check second element (should be 2, not symbol y)
    ID second = vector_nth(result_vec, 1);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_TRUE(is_fixnum(second));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(second));
    
    RELEASE(base_env);
}

// ============================================================================
// TEST: eval_body with nested vector containing symbols
// ============================================================================

TEST_SHARED(test_eval_body_nested_vector_with_env_stack) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create a nested vector [[x] [y]] where x and y are symbols
    CljSymbol *x_sym = intern_symbol_global("x");
    CljSymbol *y_sym = intern_symbol_global("y");
    
    CljVector *inner_vec1 = make_vector(1, CLJ_VECTOR);
    ASSIGN(inner_vec1, vector_conj(inner_vec1, (ID)x_sym));
    
    CljVector *inner_vec2 = make_vector(1, CLJ_VECTOR);
    ASSIGN(inner_vec2, vector_conj(inner_vec2, (ID)y_sym));
    
    CljVector *outer_vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(outer_vec, vector_conj(outer_vec, (ID)inner_vec1));
    ASSIGN(outer_vec, vector_conj(outer_vec, (ID)inner_vec2));
    
    // Create env_stack with bindings: x=1, y=2
    CljVector *env_stack = NULL;
    CljMap *x_binding = map_assoc(map_empty(), (ID)x_sym, fixnum(1));
    CljMap *y_binding = map_assoc(map_empty(), (ID)y_sym, fixnum(2));
    env_stack_push_inplace(&env_stack, x_binding);
    env_stack_push_inplace(&env_stack, y_binding);
    RELEASE(x_binding);
    RELEASE(y_binding);
    
    // Create EvalContext with env_stack
    EvalContext ctx = {
        .env = NULL,
        .env_stack = env_stack,
        .frame = NULL,
        .st = g_test_eval_state,
        .recur_args = NULL,
        .recur_arg_count = NULL,
        .recur_param_count = 0
    };
    
    // Evaluate vector
    ID result = eval_body((ID)outer_vec, NULL, g_test_eval_state, &ctx);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR);
    
    CljVector *result_vec = (CljVector*)result;
    TEST_ASSERT_EQUAL_INT(2, vector_count(result_vec));
    
    // Check first inner vector (should be [1], not [x])
    ID first_inner = vector_nth(result_vec, 0);
    TEST_ASSERT_NOT_NULL(first_inner);
    TEST_ASSERT_TRUE(TAG(first_inner) == CLJ_VECTOR);
    CljVector *first_vec = (CljVector*)first_inner;
    TEST_ASSERT_EQUAL_INT(1, vector_count(first_vec));
    ID first_elem = vector_nth(first_vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(first_elem));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first_elem));
    
    // Check second inner vector (should be [2], not [y])
    ID second_inner = vector_nth(result_vec, 1);
    TEST_ASSERT_NOT_NULL(second_inner);
    TEST_ASSERT_TRUE(TAG(second_inner) == CLJ_VECTOR);
    CljVector *second_vec = (CljVector*)second_inner;
    TEST_ASSERT_EQUAL_INT(1, vector_count(second_vec));
    ID second_elem = vector_nth(second_vec, 0);
    TEST_ASSERT_TRUE(is_fixnum(second_elem));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(second_elem));
    
    RELEASE(env_stack);
}

// ============================================================================
// TEST: Direct test of for* thunk executor with vector body
// ============================================================================

TEST_SHARED(test_for_star_thunk_executor_vector_body) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // This test simulates what happens in for* thunk executor
    // Create a vector [x y] where x and y are symbols
    CljSymbol *x_sym = intern_symbol_global("x");
    CljSymbol *y_sym = intern_symbol_global("y");
    
    CljVector *vec = make_vector(2, CLJ_VECTOR);
    ASSIGN(vec, vector_conj(vec, (ID)x_sym));
    ASSIGN(vec, vector_conj(vec, (ID)y_sym));
    
    // Simulate for* binding: create env_stack with x=1, y=2
    CljVector *env_stack = NULL;
    CljMap *x_binding = map_assoc(map_empty(), (ID)x_sym, fixnum(1));
    CljMap *y_binding = map_assoc(map_empty(), (ID)y_sym, fixnum(2));
    env_stack_push_inplace(&env_stack, x_binding);
    env_stack_push_inplace(&env_stack, y_binding);
    RELEASE(x_binding);
    RELEASE(y_binding);
    
    // Create EvalContext exactly as for* does
    EvalContext inner_ctx = {
        .env = NULL,
        .env_stack = env_stack,
        .frame = NULL,
        .st = g_test_eval_state,
        .recur_args = NULL,
        .recur_arg_count = NULL,
        .recur_param_count = 0
    };
    
    // Evaluate body exactly as for* does
    ID body_result = eval_body((ID)vec, NULL, g_test_eval_state, &inner_ctx);
    
    // Check result
    TEST_ASSERT_NOT_NULL(body_result);
    TEST_ASSERT_TRUE(TAG(body_result) == CLJ_VECTOR);
    
    CljVector *result_vec = (CljVector*)body_result;
    TEST_ASSERT_EQUAL_INT(2, vector_count(result_vec));
    
    // Check first element (should be 1, not symbol x)
    ID first = vector_nth(result_vec, 0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_fixnum(first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(first));
    
    // Check second element (should be 2, not symbol y)
    ID second = vector_nth(result_vec, 1);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_TRUE(is_fixnum(second));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(second));
    
    RELEASE(env_stack);
}
