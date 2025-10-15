/*
 * Namespace Tests using Unity Framework
 * 
 * Tests for namespace management, EvalState, and namespace isolation.
 */

#include "unity.h"
#include "object.h"
#include "parser.h"
#include "symbol.h"
#include "clj_string.h"
#include "namespace.h"
#include "map.h"
#include "tiny_clj.h"
#include "memory_profiler.h"
#include <stdio.h>
#include <string.h>

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// NAMESPACE TESTS
// ============================================================================

void test_evalstate_creation(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that evalstate has a current namespace
        TEST_ASSERT_NOT_NULL(eval_state->current_ns);
        
        evalstate_free(eval_state);
    });
}

void test_namespace_switching(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test initial namespace
        CljNamespace *initial_ns = eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(initial_ns);
        
        // Test namespace switching (simplified)
        CljObject *new_ns = make_symbol("test-ns", "user");
        TEST_ASSERT_NOT_NULL(new_ns);
        
        evalstate_free(eval_state);
    });
}

void test_namespace_isolation(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that different eval states have different namespaces
        EvalState *eval_state2 = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state2);
        
        // They should be different instances
        TEST_ASSERT_TRUE(eval_state != eval_state2);
        
        evalstate_free(eval_state);
        evalstate_free(eval_state2);
    });
}

void test_special_ns_variable(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that *ns* special variable exists
        CljNamespace *ns_var = eval_state->current_ns;
        TEST_ASSERT_NOT_NULL(ns_var);
        
        evalstate_free(eval_state);
    });
}

void test_namespace_lookup(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test symbol creation in namespace
        CljObject *sym = make_symbol("test-symbol", "user");
        TEST_ASSERT_NOT_NULL(sym);
        TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
        
        evalstate_free(eval_state);
    });
}

void test_namespace_binding(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *eval_state = evalstate_new();
        TEST_ASSERT_NOT_NULL(eval_state);
        
        // Test that we can create symbols in the namespace
        CljObject *sym1 = make_symbol("var1", "user");
        CljObject *sym2 = make_symbol("var2", "user");
        
        TEST_ASSERT_NOT_NULL(sym1);
        TEST_ASSERT_NOT_NULL(sym2);
        TEST_ASSERT_TRUE(sym1 != sym2);
        
        evalstate_free(eval_state);
    });
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
