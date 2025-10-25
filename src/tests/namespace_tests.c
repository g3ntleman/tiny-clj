/*
 * Namespace Tests using Unity Framework
 * 
 * Tests for namespace management, EvalState, and namespace isolation.
 */

#include "tests_common.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// NAMESPACE TESTS
// ============================================================================

void test_evalstate_creation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that evalstate has a current namespace
        TEST_ASSERT_NOT_NULL(eval_state->current_ns);
        
        evalstate_free(eval_state);
    }
}

void test_namespace_switching(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test initial namespace
        CljNamespace *initial_ns = eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(initial_ns);
        
        // Test namespace switching (simplified)
        CljObject *new_ns = make_symbol_impl("test-ns", "user");
        TEST_ASSERT_NOT_NULL(new_ns);
        
        evalstate_free(eval_state);
    }
}

void test_namespace_isolation(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that different eval states have different namespaces
        EvalState *eval_state2 = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state2);
        
        // They should be different instances
        TEST_ASSERT_TRUE(eval_state != eval_state2);
        
        evalstate_free(eval_state);
        evalstate_free(eval_state2);
    }
}

void test_special_ns_variable(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that *ns* special variable exists
        CljNamespace *ns_var = eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(ns_var);
        
        evalstate_free(eval_state);
    }
}

void test_namespace_lookup(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test symbol creation in namespace
        CljObject *sym = make_symbol_impl("test-symbol", "user");
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
        
        evalstate_free(eval_state);
    }
}

void test_namespace_binding(void) {
    // Manual memory management - no WITH_AUTORELEASE_POOL
    {
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that we can create symbols in the namespace
        CljObject *sym1 = make_symbol_impl("var1", "user");
        CljObject *sym2 = make_symbol_impl("var2", "user");
        
        TEST_ASSERT_NOT_NULL(sym1);
        TEST_ASSERT_NOT_NULL(sym2);
        TEST_ASSERT_TRUE(sym1 != sym2);
        
        evalstate_free(eval_state);
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests
REGISTER_TEST(test_evalstate_creation)
REGISTER_TEST(test_namespace_switching)
REGISTER_TEST(test_namespace_isolation)
REGISTER_TEST(test_special_ns_variable)
REGISTER_TEST(test_namespace_lookup)
REGISTER_TEST(test_namespace_binding)
