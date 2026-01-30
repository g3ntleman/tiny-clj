#include "tests_common.h"
#include "debug.h"

// Test to demonstrate the cond nesting problem
// Expected: (cond true 1 false 2) should return 1
// Problem: cond receives nested structure instead of flat list
TEST(test_cond_simple_nesting_problem) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Simple cond form that should work
    CljObject *result = eval_string("(cond true 1 false 2)", g_test_eval_state);
    
    // Should return 1 (first true condition)
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum(result));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(result));
}

// Test to show the structure that cond receives
TEST(test_cond_structure_debug) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Parse and evaluate a simple cond form
    CljObject *parsed = eval_string("'(cond true 1 false 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed);
    
    (void)parsed;
    
    // The structure should be: (cond true 1 false 2)
    // Not: (cond [List: (true 1 false 2)])
    TEST_ASSERT_TRUE(is_list_type(TAG(parsed)));
    
    CljList *list = as_list(parsed);
    TEST_ASSERT_NOT_NULL(list);
    
    // First element should be 'cond'
    ID first = LIST_FIRST(list);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(is_symbol(first));
    
    // Rest should be (true 1 false 2), not [List: (true 1 false 2)]
    CljList *rest = list_rest_normalized(list);
    TEST_ASSERT_NOT_NULL(rest);
    
    // First element of rest should be 'true' (a boolean), not a list
    ID rest_first = LIST_FIRST(rest);
    TEST_ASSERT_NOT_NULL(rest_first);
    
    // This is where the problem shows: rest_first might be a list instead of true
    if (is_list_type(TAG(rest_first))) {
        TEST_FAIL_MESSAGE("cond rest_first should be a boolean, not a list");
    }
}

// Low-level test that demonstrates the nesting problem directly
// Creates a nested structure: (cond (true 1 false 2)) instead of (cond true 1 false 2)
TEST(test_cond_nesting_low_level) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Create values: true, 1, false, 2
    CljValue true_val = clj_true;
    CljValue one = fixnum(1);
    CljValue false_val = clj_false;
    CljValue two = fixnum(2);
    
    // Create the inner list: (true 1 false 2)
    CljList *inner_list = make_list(true_val, 
                                    make_list(one,
                                             make_list(false_val,
                                                      make_list(two, NULL))));
    
    // Create the nested structure: (cond (true 1 false 2))
    // This simulates the problem where arguments are wrapped in an extra list
    CljList *nested_cond = make_ast_list(SYM_COND, inner_list);
    
    // Test list_rest_normalized: should return the flat list
    CljList *rest_normalized = list_rest_normalized(nested_cond);
    TEST_ASSERT_NOT_NULL(rest_normalized);
    
    // The first element should be true (not a list)
    ID first_elem = LIST_FIRST(rest_normalized);
    TEST_ASSERT_NOT_NULL(first_elem);
    TEST_ASSERT_FALSE(is_list_type(TAG(first_elem)));
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(first_elem));
    
    // Test list_rest_unwrapped: should be identical to rest_normalized here
    CljList *rest_unwrapped = list_rest_unwrapped(nested_cond);
    TEST_ASSERT_NOT_NULL(rest_unwrapped);
    
    // The first element should be true (not a list)
    ID unwrapped_first = LIST_FIRST(rest_unwrapped);
    TEST_ASSERT_NOT_NULL(unwrapped_first);
    TEST_ASSERT_FALSE(is_list_type(TAG(unwrapped_first)));
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(unwrapped_first));
    
    // Verify the structure: (true 1 false 2)
    CljList *node = rest_unwrapped;
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(LIST_FIRST(node)));
    
    node = list_rest_normalized(node);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(LIST_FIRST(node)));
    
    node = list_rest_normalized(node);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(LIST_FIRST(node)));
    
    node = list_rest_normalized(node);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(LIST_FIRST(node)));
}
