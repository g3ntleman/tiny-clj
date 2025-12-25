/*
 * Low-level test for native function lookup and registration
 * 
 * Tests whether :native stubs are correctly registered in the namespace
 */

#include "tests_common.h"
#include "builtins.h"
#include "namespace.h"
#include "symbol.h"
#include "eval.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// TEST: native_function_lookup finds count
// ============================================================================
TEST(test_native_lookup_finds_count) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        
        // Get count symbol (unqualified)
        CljSymbol *count_sym = intern_symbol_global("count");
        TEST_ASSERT_NOT_NULL_MESSAGE(count_sym, "count symbol should exist");
        
        // Try to find native function
        BuiltinFn native_count = native_function_lookup(count_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_count, "native_function_lookup should find count");
    });
}

// ============================================================================
// TEST: native_function_lookup finds count with qualified symbol
// ============================================================================
TEST(test_native_lookup_finds_count_qualified) {
    WITH_AUTORELEASE_POOL({
        init_special_symbols();
        
        // Get clojure.core namespace symbol
        CljSymbol *clojure_core_sym = intern_symbol_global("clojure.core");
        TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core_sym, "clojure.core symbol should exist");
        
        // Get qualified count symbol
        CljSymbol *count_sym = intern_symbol(clojure_core_sym, "count");
        TEST_ASSERT_NOT_NULL_MESSAGE(count_sym, "qualified count symbol should exist");
        
        // Try to find native function
        BuiltinFn native_count = native_function_lookup(count_sym);
        TEST_ASSERT_NOT_NULL_MESSAGE(native_count, "native_function_lookup should find qualified count");
    });
}

// ============================================================================
// TEST: defn with :native registers function in namespace
// ============================================================================
TEST(test_defn_native_registers_in_namespace) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to clojure.core namespace
        eval_string("(ns clojure.core)", g_test_eval_state);
        
        // Define count as :native stub
        const char *code = "(defn count [coll] :native)";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn with :native should succeed");
        
        // Check if count is in clojure.core namespace
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core, "clojure.core namespace should exist");
        TEST_ASSERT_NOT_NULL_MESSAGE(clojure_core->mappings, "clojure.core mappings should exist");
        
        CljSymbol *count_sym = intern_symbol_global("count");
        CljObject *count_value = map_get(clojure_core->mappings, count_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(count_value, "count should be in clojure.core namespace");
        TEST_ASSERT_TRUE_MESSAGE(TAG(count_value) == CLJ_FUNC, "count should be a function");
    });
}

// ============================================================================
// TEST: count can be resolved after defn :native
// ============================================================================
TEST(test_count_resolved_after_defn_native) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to clojure.core namespace
        eval_string("(ns clojure.core)", g_test_eval_state);
        
        // Define count as :native stub
        eval_string("(defn count [coll] :native)", g_test_eval_state);
        
        // Switch to user namespace
        eval_string("(ns user)", g_test_eval_state);
        
        // Try to resolve count (should find it in clojure.core)
        CljValue count_resolved = eval_string("count", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(count_resolved, "count should be resolvable");
        TEST_ASSERT_TRUE_MESSAGE(TAG(count_resolved) == CLJ_FUNC, "count should be a function");
    });
}

// ============================================================================
// TEST: count can be called after defn :native
// ============================================================================
TEST(test_count_callable_after_defn_native) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to clojure.core namespace
        eval_string("(ns clojure.core)", g_test_eval_state);
        
        // Define count as :native stub
        eval_string("(defn count [coll] :native)", g_test_eval_state);
        
        // Switch to user namespace
        eval_string("(ns user)", g_test_eval_state);
        
        // Try to call count
        CljValue result = eval_string("(count [1 2 3])", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "count should be callable");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "count result should be a number");
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(result));
    });
}

