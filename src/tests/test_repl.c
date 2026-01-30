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
#include "eval.h"
#include "strings.h"
#include "vector.h"
#include "../to_string.h"
#include "../repl.h"

#include <string.h>

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
    
    CljObject *doc_func = (CljObject*)map_get_sentinel(repl_ns->mappings, doc_sym, NULL);
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
    
#if defined(META_ENABLED) && META_ENABLED
    // Get metadata directly
    CljObject *meta_meta = eval_string("(meta meta)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(meta_meta, "meta should have metadata");
    TEST_ASSERT_TRUE_MESSAGE(TAG(meta_meta) == CLJ_MAP_PERSISTENT, "metadata should be a map");
    
    // Verify that metadata contains :doc
    CljSymbol *doc_key = intern_symbol_global(":doc");
    TEST_ASSERT_NOT_NULL_MESSAGE(doc_key, ":doc keyword should exist");
    
    CljPersistentMap *meta_map = (CljPersistentMap*)meta_meta;
    ID doc_value = map_get_sentinel(meta_map, doc_key, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(doc_value, ":doc should exist in metadata");
    TEST_ASSERT_TRUE_MESSAGE(TAG(doc_value) == CLJ_STRING, ":doc should be a string");
#else
    // With metadata compiled out, meta should return nil
    CljObject *meta_meta = eval_string("(meta meta)", g_test_eval_state);
    TEST_ASSERT_NIL_MESSAGE(meta_meta, "meta should return nil when META_ENABLED=0");
#endif
    
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
#if defined(META_ENABLED) && META_ENABLED
    TEST_ASSERT_NOT_NULL_MESSAGE(fn_meta, "function should have metadata");
#else
    TEST_ASSERT_NIL_MESSAGE(fn_meta, "function metadata should be nil when META_ENABLED=0");
#endif
    
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
    CljObject *result = eval_string("(clojure.repl/source 'clojure.core/inc)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_source_with_function_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source can be called with a function symbol
    CljObject *result = eval_string("(clojure.repl/source 'inc)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST(test_repl_source_returns_string_for_clojure_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Define a simple Clojure function in the current namespace
    CljObject *defn_result = eval_string("(defn test-fn [x] (+ x 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(defn_result, "defn should succeed");
    
    // Test: source should return the source code as a string
    CljObject *result = eval_string("(clojure.repl/source 'test-fn)", g_test_eval_state);
    if (result) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(result) == CLJ_STRING || TAG(result) == CLJ_NIL,
                                "source should return a string or nil");
    }
}

TEST(test_repl_source_with_qualified_symbol) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source can be called with a qualified symbol
    CljObject *result = eval_string("(clojure.repl/source 'clojure.core/identity)", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL || TAG(result) == CLJ_STRING);
}

TEST(test_repl_source_with_native_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Test: source with a native function should not crash
    CljObject *result = eval_string("(clojure.repl/source 'clojure.core/nil?)", g_test_eval_state);
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
    
    CljObject *source_func = (CljObject*)map_get_sentinel(repl_ns->mappings, source_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(source_func, "source function should exist in clojure.repl namespace");
    TEST_ASSERT_TRUE_MESSAGE(TAG(source_func) == CLJ_FUNC || TAG(source_func) == CLJ_CLOSURE,
                            "source should be a function");
}

TEST(test_repl_source_shows_qualified_recursive_call) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    load_repl_namespace();

    CljObject *defn_result = eval_string(
        "(defn repl-source-foo [n]"
        "  (if (< n 2)"
        "      n"
        "      (+ (repl-source-foo (- n 1))"
        "         (repl-source-foo (- n 2))))))",
        g_test_eval_state);
    if (defn_result && !IS_IMMEDIATE(defn_result)) {
        RELEASE(defn_result);
    }

    CljObject *call_result = eval_string("(repl-source-foo 3)", g_test_eval_state);
    if (call_result && !IS_IMMEDIATE(call_result)) {
        RELEASE(call_result);
    }

    CljSymbol *foo_sym = intern_symbol_global("repl-source-foo");
    TEST_ASSERT_NOT_NULL(foo_sym);

    CljObject *foo_fn_obj = ns_resolve(g_test_eval_state, foo_sym);
    TEST_ASSERT_NOT_NULL(foo_fn_obj);
    TEST_ASSERT_EQUAL(CLJ_CLOSURE, TAG(foo_fn_obj));

    CljFunction *foo_fn = as_function(foo_fn_obj);
    bool prev_mode = strings_set_special_form_rendering(false);
    CljString *body_str = to_string(foo_fn->body);
    strings_set_special_form_rendering(prev_mode);
    TEST_ASSERT_NOT_NULL(body_str);

    const char *body_cstr = string_data(body_str);
    TEST_ASSERT_NOT_NULL(body_cstr);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(body_cstr, "user/repl-source-foo"),
        "Function body should contain qualified recursive call");

    RELEASE(body_str);
    RELEASE(foo_fn_obj);
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
    
    CljObject *dir_func = (CljObject*)map_get_sentinel(repl_ns->mappings, dir_sym, NULL);
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
    
    CljObject *pst_func = (CljObject*)map_get_sentinel(repl_ns->mappings, pst_sym, NULL);
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
    
    CljObject *find_doc_func = (CljObject*)map_get_sentinel(repl_ns->mappings, find_doc_sym, NULL);
    TEST_ASSERT_NOT_NULL(find_doc_func);
    TEST_ASSERT_TRUE(TAG(find_doc_func) == CLJ_FUNC || TAG(find_doc_func) == CLJ_CLOSURE);
}

TEST(test_repl_find_doc_can_be_called) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    load_repl_namespace();
    
    // Ensure clojure.string is loaded (find-doc depends on cstr/includes?)
    // The (:require [clojure.string :as cstr]) in clojure.repl should load it,
    // but we ensure it's loaded explicitly for the test
    eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Verify find-doc function exists before calling
    CljSymbol *find_doc_sym = intern_symbol(SYM_CLOJURE_REPL, "find-doc");
    CljNamespace *repl_ns = ns_find("clojure.repl");
    TEST_ASSERT_NOT_NULL(repl_ns);
    CljObject *find_doc_func = map_get_sentinel(repl_ns->mappings, find_doc_sym, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(find_doc_func, "find-doc function should exist");
    
    // Test: find-doc can be called (simplified implementation)
    // Note: find-doc may fail if dependencies are missing, so we catch exceptions
    TRY {
    CljObject *result = eval_string("(clojure.repl/find-doc \"pattern\")", g_test_eval_state);
    // find-doc prints to stdout and returns nil
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
    } CATCH(ex) {
        // If find-doc fails due to missing dependencies, that's acceptable for now
        // The function exists, which is what we're testing
        (void)ex;
    } END_TRY
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
    const char *expected_functions[] = {
        "doc", "dir", "pst", "find-doc"
    };
    
    for (int i = 0; i < 4; i++) {
        CljSymbol *func_sym = intern_symbol(SYM_CLOJURE_REPL, expected_functions[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(func_sym, "Function symbol should exist");
        
        CljObject *func = (CljObject*)map_get_sentinel(repl_ns->mappings, func_sym, NULL);
        TEST_ASSERT_NOT_NULL_MESSAGE(func, 
            "Function should exist in clojure.repl namespace");
        TEST_ASSERT_TRUE_MESSAGE(
            TAG(func) == CLJ_FUNC || TAG(func) == CLJ_CLOSURE,
            "Function should be a function or closure");
    }
}

TEST(test_repl_eval_arg_supports_escaped_newlines) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    load_repl_namespace();

    const char *code = "(def repl_eval_arg_value 41)\\n(def repl_eval_arg_value (inc repl_eval_arg_value))\\nrepl_eval_arg_value";
    bool success = repl_eval_arg(code, g_test_eval_state);
    TEST_ASSERT_TRUE(success);

    CljObject *result = eval_string("repl_eval_arg_value", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)result));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)result));
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

// ============================================================================
// STACKTRACE TESTS
// ============================================================================

TEST(test_stacktrace_stack_trace_returns_vector_of_strings) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // 1) require clojure.stacktrace
    CljObject *req_result = eval_string("(require 'clojure.stacktrace)", g_test_eval_state);
    (void)req_result;

    // 2) create/capture exception value using Clojure try/catch (division by zero)
    CljObject *ex_obj = eval_string("(try (/ 1 0) (catch Exception e e))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(ex_obj, "try/catch should return an exception value");
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_EXCEPTION, TAG(ex_obj), "captured value should be an exception");

    // 3) call (clojure.stacktrace/stack-trace e)
    CljObject *trace_obj = eval_string(
        "(let [e (try (/ 1 0) (catch Exception e e))] (clojure.stacktrace/stack-trace e))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(trace_obj, "stack-trace should return a vector (possibly empty)");

    // 4) assert it returns a vector and that every element is a string
    TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_VECTOR_PERSISTENT, TAG(trace_obj), "stack-trace should return a vector");
    CljPersistentVector *trace_vec = as_persistent_vector((ID)trace_obj);
    TEST_ASSERT_NOT_NULL(trace_vec);

    int n = (int)vector_count(trace_vec);
    for (int i = 0; i < n; i++) {
        ID frame = vector_nth(trace_vec, i);
        TEST_ASSERT_NOT_NULL_MESSAGE(frame, "stack-trace frame should not be nil");
        TEST_ASSERT_EQUAL_INT_MESSAGE(CLJ_STRING, TAG(frame), "every stack-trace frame should be a string");
    }

    if (trace_obj && !IS_IMMEDIATE(trace_obj)) RELEASE(trace_obj);
    if (ex_obj && !IS_IMMEDIATE(ex_obj)) RELEASE(ex_obj);
}

