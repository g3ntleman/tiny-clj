/*
 * Debug test to understand why :native stubs are not registered when loading clojure.core.clj
 */

#include "tests_common.h"
#include "namespace.h"
#include "symbol.h"
#include "eval.h"
#include "builtins.h"
#include "reader.h"
#include "parser.h"

// Forward declaration
int load_clojure_core(EvalState *st);
extern const char *clojure_core_code;

// ============================================================================
// TEST: Check if defn count is parsed correctly
// ============================================================================
TEST(test_defn_count_parsed_correctly) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Parse "(defn count [coll] :native)"
        const char *code = "(defn count [coll] :native)";
        Reader reader;
        reader_init(&reader, code);
        reader_set_source_name(&reader, "test");
        
        CljValue parsed = parse_from_reader(&reader, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(parsed, "defn count should parse");
        TEST_ASSERT_TRUE_MESSAGE(list_type_matches(TAG(parsed)), "defn count should be a list");
    });
}

// ============================================================================
// TEST: Check if defn count is evaluated correctly
// ============================================================================
TEST(test_defn_count_evaluated_correctly) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to clojure.core namespace
        eval_string("(ns clojure.core)", g_test_eval_state);
        
        // Evaluate "(defn count [coll] :native)"
        const char *code = "(defn count [coll] :native)";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn count should evaluate");
        
        // Check if count is in namespace
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL(clojure_core);
        
        CljSymbol *count_sym = intern_symbol_global("count");
        CljObject *count_value = map_get(clojure_core->mappings, count_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(count_value, "count should be in namespace after defn");
    });
}

// ============================================================================
// TEST: Check what happens when loading clojure.core.clj line by line
// ============================================================================
TEST(test_load_clojure_core_line_by_line) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Switch to clojure.core namespace
        eval_string("(ns clojure.core)", g_test_eval_state);
        
        // Try to evaluate just the defn count line
        const char *code = "(defn count [coll] :native)";
        CljValue result = eval_string(code, g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result, "defn count should evaluate");
        
        // Check if count is in namespace
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL(clojure_core);
        
        CljSymbol *count_sym = intern_symbol_global("count");
        CljObject *count_value = map_get(clojure_core->mappings, count_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(count_value, "count should be in namespace");
    });
}

// ============================================================================
// TEST: Check if defn is defined before count is defined
// ============================================================================
TEST(test_defn_defined_before_count) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Load clojure.core
        load_clojure_core(g_test_eval_state);
        
        // Check if defn is in namespace
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL(clojure_core);
        
        CljSymbol *defn_sym = intern_symbol_global("defn");
        CljObject *defn_value = map_get(clojure_core->mappings, defn_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(defn_value, "defn should be in namespace");
        
        // Check if count is in namespace
        CljSymbol *count_sym = intern_symbol_global("count");
        CljObject *count_value = map_get(clojure_core->mappings, count_sym, NULL);
        // This might fail - that's what we're debugging
        if (!count_value) {
            // Count is not in namespace - this is the problem we're investigating
            TEST_FAIL_MESSAGE("count is not in namespace after loading clojure.core.clj");
        }
    });
}

// ============================================================================
// TEST: Check if there are errors during loading
// ============================================================================
TEST(test_check_loading_errors) {
    WITH_AUTORELEASE_POOL({
        TEST_ASSERT_NOT_NULL(g_test_eval_state);
        init_special_symbols();
        
        // Load clojure.core and check for errors
        int result = load_clojure_core(g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(result > 0, "load_clojure_core should succeed");
        
        // Check if count is in namespace
        CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
        TEST_ASSERT_NOT_NULL(clojure_core);
        
        CljSymbol *count_sym = intern_symbol_global("count");
        CljObject *count_value = map_get(clojure_core->mappings, count_sym, NULL);
        
        // If count is not in namespace, there might have been an error during loading
        if (!count_value) {
            // Try to manually evaluate defn count to see if it works
            eval_string("(ns clojure.core)", g_test_eval_state);
            CljValue eval_result = eval_string("(defn count [coll] :native)", g_test_eval_state);
            TEST_ASSERT_NOT_NULL_MESSAGE(eval_result, "manual defn count should work");
            
            // Check again
            count_value = map_get(clojure_core->mappings, count_sym, NULL);
            TEST_ASSERT_NOT_NULL_MESSAGE(count_value, "count should be in namespace after manual defn");
        }
    });
}

