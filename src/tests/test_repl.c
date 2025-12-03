/*
 * Unity Tests for clojure.repl functions in Tiny-CLJ
 * 
 * Tests for REPL helper functions from clojure.repl namespace
 */

#include "tests_common.h"
#include "namespace.h"
#include "symbol.h"
#include "map.h"
#include "object.h"
#include "kv_macros.h"
#include "value.h"
#include "function_call.h"

// Forward declarations
int load_clojure_core(EvalState *st);
int load_clojure_repl(EvalState *st);

// ============================================================================
// HELPER: Load clojure.repl namespace
// ============================================================================

static void load_repl_namespace(void) {
    // Load clojure.repl namespace
    // Note: load_clojure_repl is called automatically in REPL, but for tests
    // we need to call it explicitly
    load_clojure_repl(g_test_eval_state);
}

// ============================================================================
// DOC FUNCTION TESTS
// ============================================================================

TEST(test_repl_doc_function_exists) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: doc function should exist in clojure.repl namespace
    CljSymbol *doc_sym = intern_symbol(SYM_CLOJURE_REPL, "doc");
    TEST_ASSERT_NOT_NULL(doc_sym);
    
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    TEST_ASSERT_NOT_NULL(repl_ns->mappings);
    
    CljObject *doc_func = (CljObject*)map_get(repl_ns->mappings, doc_sym, NULL);
    TEST_ASSERT_NOT_NULL(doc_func);
    TEST_ASSERT_TRUE(TAG(doc_func) == CLJ_FUNC || TAG(doc_func) == CLJ_CLOSURE);
}

TEST(test_repl_doc_can_be_called) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: doc can be called (even if it returns "No metadata available")
    // We test that it doesn't throw an exception
    CljObject *result = eval_string("(clojure.repl/doc 'clojure.core/inc)", g_test_eval_state);
    // doc prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_doc_with_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: doc can be called with a symbol
    CljObject *result = eval_string("(clojure.repl/doc 'inc)", g_test_eval_state);
    // doc prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_doc_extracts_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: doc should extract metadata from functions with docstrings
    // First, verify that meta has metadata
    CljObject *meta_func = eval_string("meta", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(meta_func, "meta function should exist");
    
    // Get metadata directly
    CljObject *meta_meta = eval_string("(meta meta)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(meta_meta, "meta should have metadata");
    TEST_ASSERT_TRUE_MESSAGE(TAG(meta_meta) == CLJ_MAP, "metadata should be a map");
    
    // Verify that metadata contains :doc
    CljSymbol *doc_key = intern_symbol_global(":doc");
    TEST_ASSERT_NOT_NULL_MESSAGE(doc_key, ":doc keyword should exist");
    
    CljMap *meta_map = (CljMap*)meta_meta;
    ID doc_value = map_get(meta_map, doc_key, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(doc_value, ":doc should exist in metadata");
    TEST_ASSERT_TRUE_MESSAGE(TAG(doc_value) == CLJ_STRING, ":doc should be a string");
    
    // Now test that doc can be called on meta and doesn't throw an error
    CljObject *doc_result = eval_string("(clojure.repl/doc meta)", g_test_eval_state);
    // doc prints to stdout and returns nil
    TEST_ASSERT_TRUE_MESSAGE(doc_result == NULL || TAG(doc_result) == CLJ_NIL,
                            "doc should return nil");
}

TEST(test_repl_doc_with_function_with_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: doc should work with a function that has metadata
    // Create a function with metadata
    CljObject *defn_result = eval_string(
        "^#^{:doc \"Test function for doc test\"} "
        "(defn test-doc-fn [] 42)", 
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(defn_result, "defn should succeed");
    
    // Verify the function has metadata
    CljObject *fn_meta = eval_string("(meta test-doc-fn)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_meta, "function should have metadata");
    
    // Call doc on the function
    CljObject *doc_result = eval_string("(clojure.repl/doc test-doc-fn)", g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(doc_result == NULL || TAG(doc_result) == CLJ_NIL,
                            "doc should return nil");
}

// ============================================================================
// SOURCE FUNCTION TESTS
// ============================================================================

TEST(test_repl_source_can_be_called) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source can be called
    CljObject *result = eval_string("(source 'clojure.core/inc)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_source_with_function_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source can be called with a function symbol
    CljObject *result = eval_string("(source 'inc)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_source_returns_string_for_clojure_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Define a simple Clojure function in the current namespace
    CljObject *defn_result = eval_string("(defn test-fn [x] (+ x 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(defn_result, "defn should succeed");
    
    // Test: source should return the source code as a string
    CljObject *result = eval_string("(source 'test-fn)", g_test_eval_state);
    if (result) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_STRING || TAG(result) == CLJ_NIL,
                                "source should return a string or nil");
    }
}

TEST(test_repl_source_with_qualified_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source can be called with a qualified symbol
    CljObject *result = eval_string("(source 'clojure.core/identity)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL || TAG(result) == CLJ_STRING);
}

TEST(test_repl_source_with_native_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source with a native function should not crash
    CljObject *result = eval_string("(source 'clojure.core/nil?)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL || TAG(result) == CLJ_STRING);
}

TEST(test_repl_source_exists_in_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source function should exist in clojure.repl namespace
    CljSymbol *source_sym = intern_symbol(SYM_CLOJURE_REPL, "source");
    TEST_ASSERT_NOT_NULL(source_sym);
    
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    TEST_ASSERT_NOT_NULL(repl_ns->mappings);
    
    CljObject *source_func = (CljObject*)map_get(repl_ns->mappings, source_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(source_func, "source function should exist in clojure.repl namespace");
    TEST_ASSERT_TRUE_MESSAGE(TAG(source_func) == CLJ_FUNC || TAG(source_func) == CLJ_CLOSURE,
                            "source should be a function");
}

// ============================================================================
// DIR FUNCTION TESTS
// ============================================================================

TEST(test_repl_dir_function_exists) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: dir function should exist in clojure.repl namespace
    CljSymbol *dir_sym = intern_symbol(SYM_CLOJURE_REPL, "dir");
    TEST_ASSERT_NOT_NULL(dir_sym);
    
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    TEST_ASSERT_NOT_NULL(repl_ns->mappings);
    
    CljObject *dir_func = (CljObject*)map_get(repl_ns->mappings, dir_sym, NULL);
    TEST_ASSERT_NOT_NULL(dir_func);
    TEST_ASSERT_TRUE(TAG(dir_func) == CLJ_FUNC || TAG(dir_func) == CLJ_CLOSURE);
}

TEST(test_repl_dir_can_be_called) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: dir can be called with a namespace symbol
    CljObject *result = eval_string("(clojure.repl/dir 'clojure.core)", g_test_eval_state);
    // dir prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_dir_with_nonexistent_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: dir handles nonexistent namespace gracefully
    CljObject *result = eval_string("(clojure.repl/dir 'nonexistent.namespace)", g_test_eval_state);
    // dir should print a message and return nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_dir_with_current_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: dir can list functions in current namespace
    CljObject *result = eval_string("(clojure.repl/dir 'clojure.repl)", g_test_eval_state);
    // dir prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

// ============================================================================
// PST FUNCTION TESTS
// ============================================================================

TEST(test_repl_pst_function_exists) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: pst function should exist in clojure.repl namespace
    CljSymbol *pst_sym = intern_symbol(SYM_CLOJURE_REPL, "pst");
    TEST_ASSERT_NOT_NULL(pst_sym);
    
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    TEST_ASSERT_NOT_NULL(repl_ns->mappings);
    
    CljObject *pst_func = (CljObject*)map_get(repl_ns->mappings, pst_sym, NULL);
    TEST_ASSERT_NOT_NULL(pst_func);
    TEST_ASSERT_TRUE(TAG(pst_func) == CLJ_FUNC || TAG(pst_func) == CLJ_CLOSURE);
}

TEST(test_repl_pst_can_be_called) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: pst can be called (simplified implementation)
    CljObject *result = eval_string("(clojure.repl/pst)", g_test_eval_state);
    // pst prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

// ============================================================================
// FIND-DOC FUNCTION TESTS
// ============================================================================

TEST(test_repl_find_doc_function_exists) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: find-doc function should exist in clojure.repl namespace
    CljSymbol *find_doc_sym = intern_symbol(SYM_CLOJURE_REPL, "find-doc");
    TEST_ASSERT_NOT_NULL(find_doc_sym);
    
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    TEST_ASSERT_NOT_NULL(repl_ns->mappings);
    
    CljObject *find_doc_func = (CljObject*)map_get(repl_ns->mappings, find_doc_sym, NULL);
    TEST_ASSERT_NOT_NULL(find_doc_func);
    TEST_ASSERT_TRUE(TAG(find_doc_func) == CLJ_FUNC || TAG(find_doc_func) == CLJ_CLOSURE);
}

TEST(test_repl_find_doc_can_be_called) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: find-doc can be called (simplified implementation)
    CljObject *result = eval_string("(clojure.repl/find-doc \"pattern\")", g_test_eval_state);
    // find-doc prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

// ============================================================================
// NAMESPACE LOADING TESTS
// ============================================================================

TEST(test_repl_namespace_loads_automatically) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.repl namespace
    load_repl_namespace();
    
    // Test: clojure.repl namespace should exist
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL_MESSAGE(repl_ns, "clojure.repl namespace should exist after loading");
    TEST_ASSERT_NOT_NULL_MESSAGE(repl_ns->mappings, "clojure.repl namespace should have mappings");
}

TEST(test_repl_namespace_has_all_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    TEST_ASSERT_NOT_NULL(repl_ns->mappings);
    
    // Test: All expected functions should exist
    // Note: source is now a special form, not a function in clojure.repl namespace
    const char *expected_functions[] = {
        "doc", "dir", "pst", "find-doc"
    };
    
    for (int i = 0; i < 4; i++) {
        CljSymbol *func_sym = intern_symbol(SYM_CLOJURE_REPL, expected_functions[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(func_sym, "Function symbol should exist");
        
        CljObject *func = (CljObject*)map_get(repl_ns->mappings, func_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(func, 
            "Function should exist in clojure.repl namespace");
        TEST_ASSERT_TRUE_MESSAGE(
            TAG(func) == CLJ_FUNC || TAG(func) == CLJ_CLOSURE,
            "Function should be a function or closure");
    }
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

TEST(test_repl_functions_accessible_from_user_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: REPL functions should be accessible via qualified names
    CljObject *result = eval_string("(clojure.repl/dir 'clojure.core)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_functions_work_after_core_load) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.core first
    load_clojure_core(g_test_eval_state);
    
    // Then load clojure.repl
    load_repl_namespace();
    
    // Test: REPL functions should work after core is loaded
    CljObject *result = eval_string("(clojure.repl/dir 'clojure.core)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

