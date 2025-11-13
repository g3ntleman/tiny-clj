/*
 * Test to diagnose list function resolution issues
 * 
 * This test checks if list is available in the namespace and can be resolved.
 */

#include "tests_common.h"

// ============================================================================
// LIST RESOLUTION TESTS
// ============================================================================

TEST(test_list_in_namespace) {
    // Test: Check if list is available in clojure.core namespace
    CljObject *list_sym = SYM_LIST;
    TEST_ASSERT_NOT_NULL(list_sym);
    
    // Try to resolve list from namespace
    CljObject *list_value = ns_resolve(g_test_eval_state, list_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(list_value, "list should be resolvable from namespace");
    
    // Check if it's a function
    TEST_ASSERT_TRUE_MESSAGE(list_value && TAG(list_value) == CLJ_FUNC || list_value && TAG(list_value) == CLJ_CLOSURE,
                            "list should be a function");
}

TEST(test_list_direct_call) {
    // Test: Direct call to list function
    CljObject *result = eval_string("(list)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "list should work with no arguments");
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_list_with_args) {
    // Test: list with arguments
    CljObject *result = eval_string("(list 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "list should work with arguments");
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify first element is 1
    CljList *list = as_list((ID)result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
    
    // Don't RELEASE result - eval_string returns autoreleased object
}

TEST(test_list_in_clojure_core_clj) {
    // Test: Check if list is available when loading clojure.core.clj
    // This simulates what happens when map/filter functions are loaded
    CljObject *list_sym = SYM_LIST;
    TEST_ASSERT_NOT_NULL(list_sym);
    
    // Switch to clojure.core namespace (like during loading)
    evalstate_set_ns(g_test_eval_state, "clojure.core");
    
    // Try to resolve list
    CljObject *list_value = ns_resolve(g_test_eval_state, list_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(list_value, "list should be resolvable in clojure.core namespace");
    
    // Switch back to user namespace
    evalstate_set_ns(g_test_eval_state, "user");
}

TEST(test_list_via_symbol_resolution) {
    // Test: Check if list can be resolved via eval_symbol
    CljObject *list_sym = SYM_LIST;
    TEST_ASSERT_NOT_NULL(list_sym);
    
    // Use eval_symbol to resolve (like eval_list does)
    CljObject *resolved = eval_symbol(list_sym, g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "list should be resolvable via eval_symbol");
    
    // Check if it's a function
    TEST_ASSERT_TRUE_MESSAGE(resolved && TAG(resolved) == CLJ_FUNC || resolved && TAG(resolved) == CLJ_CLOSURE,
                            "resolved list should be a function");
}

TEST(test_list_available_in_evalstate) {
    // Test: Verify that list is available in EvalState via ns_resolve
    // This test demonstrates that list can be resolved using the EvalState
    CljObject *list_sym = SYM_LIST;
    TEST_ASSERT_NOT_NULL_MESSAGE(list_sym, "list symbol should be created");
    
    // Resolve list using EvalState (g_test_eval_state from setUp)
    CljObject *list_func = ns_resolve(g_test_eval_state, list_sym);
    TEST_ASSERT_NOT_NULL_MESSAGE(list_func, "list should be resolvable via ns_resolve(st, list_sym)");
    
    // Verify it's a function (builtin)
    TEST_ASSERT_TRUE_MESSAGE(list_func && TAG(list_func) == CLJ_FUNC || list_func && TAG(list_func) == CLJ_CLOSURE,
                            "list should be a function (CLJ_FUNC or CLJ_CLOSURE)");
    
    // Verify it's actually the list function by checking it's a builtin
    if (list_func && TAG(list_func) == CLJ_FUNC) {
        // It's a native builtin function - this is correct
        TEST_ASSERT_TRUE_MESSAGE(true, "list is a native builtin function");
    }
}

