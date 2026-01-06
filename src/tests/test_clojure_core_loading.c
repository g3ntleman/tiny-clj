/*
 * Test whether clojure.core.clj is fully loaded and functions are registered
 * 
 * NOTE: clojure.core is already loaded in setUp() via evalstate_reset()
 */

#include "tests_common.h"
#include "namespace.h"
#include "symbol.h"
#include "eval.h"
#include "builtins.h"

// ============================================================================
// TEST: clojure.core namespace exists (already loaded in setUp)
// ============================================================================
TEST(test_clojure_core_namespace_exists) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Check if namespace exists (should be loaded in setUp)
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core namespace should exist");
        TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core->mappings, "clojure.core mappings should exist");
    });
}

// ============================================================================
// TEST: count is in clojure.core (already loaded in setUp)
// ============================================================================
TEST(test_count_in_clojure_core_after_loading) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Check if count is in namespace (should be loaded in setUp)
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL(clojure_core);
        
        CljSymbol *count_sym = intern_symbol_global("count");
        CljObject *count_value = map_get_sentinel(clojure_core->mappings, count_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(count_value, "count should be in clojure.core namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(count_value) == CLJ_FUNC, "count should be a function");
    });
}

// ============================================================================
// TEST: first is in clojure.core (already loaded in setUp)
// ============================================================================
TEST(test_first_in_clojure_core_after_loading) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Check if first is in namespace (should be loaded in setUp)
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL(clojure_core);
        
        CljSymbol *first_sym = intern_symbol_global("first");
        CljObject *first_value = map_get_sentinel(clojure_core->mappings, first_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(first_value, "first should be in clojure.core namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(first_value) == CLJ_FUNC, "first should be a function");
    });
}

// ============================================================================
// TEST: count can be resolved (already loaded in setUp)
// ============================================================================
TEST(test_count_resolved_after_loading_core) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to user namespace
        eval_string("(ns user)", g_test_eval_state);
        
        // Try to resolve count
        CljValue count_resolved = eval_string("count", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(count_resolved, "count should be resolvable");
        TEST_ASSERT_TRUE_MESSAGE(TAG(count_resolved) == CLJ_FUNC, "count should be a function");
    });
}

// ============================================================================
// TEST: count can be called (already loaded in setUp)
// ============================================================================
TEST(test_count_callable_after_loading_core) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to user namespace
        eval_string("(ns user)", g_test_eval_state);
        
        // Try to call count
        CljValue result = eval_string("(count [1 2 3])", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "count should be callable");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "count result should be a number");
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    });
}
