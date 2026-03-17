/*
 * Unity Tests for require functionality and clojure.string namespace in Tiny-CLJ
 * 
 * Tests for require special form and clojure.string functions
 */

#include "tests_common.h"
#include "../meta.h"
#include "map.h"

// ============================================================================
// REQUIRE TESTS
// ============================================================================

TEST(test_require_clojure_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (require 'clojure.string) should load the namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify that clojure.string namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    
    // Verify that functions are available in the namespace
    // Check if blank? is defined
    CljSymbol *blank_sym = intern_symbol_global("blank?");
    TEST_ASSERT_NOT_NULL(blank_sym);
    
    // Try to resolve blank? from clojure.string namespace
    EvalState *temp_st = evalstate_new(false);
    evalstate_set_ns(temp_st, "clojure.string");
    (void)ns_resolve(temp_st, blank_sym);
    evalstate_free(temp_st);
    
    // blank? should be available (either as function or as nil if not yet loaded)
    // We just check that namespace exists and can be queried
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
}

// Test: Verify that native functions are available in clojure.string
TEST(test_require_native_trim_available) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: trim should be available as native function
    CljObject *trim_result = eval_string("(clojure.string/trim \"  hello  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(trim_result);
    TEST_ASSERT_TRUE(TAG(trim_result) == CLJ_STRING);
    CljString *trim_str = as_clj_string(trim_result);
    TEST_ASSERT_EQUAL_STRING("hello", clj_string_data(trim_str));
}

// Test: Verify that require actually loads functions into namespace
TEST(test_require_loads_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL(string_ns);
    TEST_ASSERT_NOT_NULL(string_ns->mappings);
    
    // First verify trim works (native function)
    CljObject *trim_result = eval_string("(clojure.string/trim \"  hello  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(trim_result);
    TEST_ASSERT_TRUE(TAG(trim_result) == CLJ_STRING);
}

// Test: Verify that blank? is loaded and can be resolved
TEST(test_require_blank_resolution) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Verify namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns, "clojure.string namespace should exist after require");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns->mappings, "clojure.string namespace should have mappings");
    
    // Check if blank? is defined in the namespace
    CljSymbol *blank_sym = intern_symbol_global("blank?");
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_sym, "blank? symbol should be interned");
    
    // Try to resolve blank? from clojure.string namespace using ns_resolve
    EvalState *temp_st = evalstate_new(false);
    evalstate_set_ns(temp_st, "clojure.string");
    ID blank_func = ns_resolve(temp_st, blank_sym);
    evalstate_free(temp_st);
    
    // blank? should be available if it was loaded correctly
    TEST_ASSERT_TRUE_MESSAGE(blank_func && blank_func != NOT_FOUND,
                             "blank? should be resolvable from clojure.string namespace");
    
    // Verify it's a function
    if (blank_func) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(blank_func) == CLJ_FUNC || TAG(blank_func) == CLJ_CLOSURE,
                                 "blank? should resolve to a function");
    }
}

// Test: Verify that blank? can be called via qualified symbol
TEST(test_require_blank_call) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
    
    // Test: (clojure.string/blank? "") => true
    CljObject *blank_result = eval_string("(clojure.string/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_result, "blank? should return a result");
    TEST_ASSERT_TRUE_MESSAGE(blank_result == clj_true, "blank? should return true for empty string");
    
    // Test: (clojure.string/blank? "abc") => false
    CljObject *blank_result2 = eval_string("(clojure.string/blank? \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_result2, "blank? should return a result");
    TEST_ASSERT_TRUE_MESSAGE(blank_result2 == clj_false, "blank? should return false for non-empty string");
}

// ============================================================================
// CLOJURE.STRING TESTS (after require)
// ============================================================================

TEST(test_string_blank_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/blank? nil) => true
    CljObject *result1 = eval_string("(clojure.string/blank? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/blank? "") => true
    CljObject *result2 = eval_string("(clojure.string/blank? \"\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
    
    // Test: (clojure.string/blank? "   ") => true
    CljObject *result3 = eval_string("(clojure.string/blank? \"   \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_true);
    
    // Test: (clojure.string/blank? "abc") => false
    CljObject *result4 = eval_string("(clojure.string/blank? \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(result4 == clj_false);
}

TEST(test_string_capitalize_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/capitalize "hello") => "Hello"
    CljObject *result1 = eval_string("(clojure.string/capitalize \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("Hello", clj_string_data(str1));
    
    // Test: (clojure.string/capitalize "HELLO") => "Hello"
    CljObject *result2 = eval_string("(clojure.string/capitalize \"HELLO\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(TAG(result2) == CLJ_STRING);
    CljString *str2 = as_clj_string(result2);
    TEST_ASSERT_EQUAL_STRING("Hello", clj_string_data(str2));
}

TEST(test_string_ends_with_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/ends-with? "hello" "lo") => true
    CljObject *result1 = eval_string("(clojure.string/ends-with? \"hello\" \"lo\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/ends-with? "hello" "x") => false
    CljObject *result2 = eval_string("(clojure.string/ends-with? \"hello\" \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_string_includes_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/includes? "hello" "ell") => true
    CljObject *result1 = eval_string("(clojure.string/includes? \"hello\" \"ell\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/includes? "hello" "xyz") => false
    CljObject *result2 = eval_string("(clojure.string/includes? \"hello\" \"xyz\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

TEST(test_string_index_of_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/index-of "hello" "l" 0) => 2
    CljObject *result1 = eval_string("(clojure.string/index-of \"hello\" \"l\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

TEST(test_string_reverse_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/reverse "abc") => "cba"
    CljObject *result1 = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str1));
}

// ============================================================================
// REVERSE CONFLICT TESTS
// ============================================================================

TEST(test_require_reverse_conflict_clojure_core) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.core/reverse (list 1 2 3)) => (3 2 1)
    // This tests if clojure.core/reverse still works after loading clojure.string
    CljObject *result = eval_string("(clojure.core/reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify first element is 3
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

TEST(test_require_reverse_conflict_clojure_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (clojure.string/reverse "abc") => "cba"
    // This tests if clojure.string/reverse works for strings
    CljObject *result = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(TAG(result) == CLJ_STRING);
    CljString *str = as_clj_string(result);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str));
}

TEST(test_require_reverse_in_let_after_require) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (let [step (fn [coll] (clojure.core/reverse coll))] (step (list 1 2 3)))
    // This tests if clojure.core/reverse works in let bindings after loading clojure.string
    CljObject *result = eval_string("(let [step (fn [coll] (clojure.core/reverse coll))] (step (list 1 2 3)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify first element is 3
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)list->first));
}

TEST(test_require_reverse_in_recursive_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (let [step (fn [coll acc] (if (empty? coll) (clojure.core/reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))
    // This tests if clojure.core/reverse works in recursive functions after loading clojure.string
    CljObject *result = eval_string("(let [step (fn [coll acc] (if (empty? coll) (clojure.core/reverse acc) (step (rest coll) (cons (first coll) acc))))] (step (list 1 2 3) (list)))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(result && is_list_type(TAG(result)));
    
    // Verify result is (1 2 3)
    CljList *list = as_list(result);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_NOT_NULL(list->first);
    TEST_ASSERT_TRUE(is_fixnum((CljValue)list->first));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum((CljValue)list->first));
}

TEST(test_require_both_reverse_functions) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test both reverse functions work correctly
    // clojure.core/reverse for collections
    CljObject *core_result = eval_string("(clojure.core/reverse (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(core_result);
    TEST_ASSERT_TRUE(core_result && is_list_type(TAG(core_result)));
    
    // clojure.string/reverse for strings
    CljObject *string_result = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(string_result);
    TEST_ASSERT_TRUE(TAG(string_result) == CLJ_STRING);
    CljString *str = as_clj_string(string_result);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str));
}

static ID lookup_ns_var(const char *ns_name, const char *var_name) {
    TEST_ASSERT_NOT_NULL(ns_name);
    TEST_ASSERT_NOT_NULL(var_name);
    CljNamespace *ns = ns_find(ns_name);
    TEST_ASSERT_NOT_NULL(ns);
    TEST_ASSERT_NOT_NULL(ns->name);
    TEST_ASSERT_NOT_NULL(ns->mappings);
    CljSymbol *var_sym = intern_symbol(ns->name, var_name);
    TEST_ASSERT_NOT_NULL(var_sym);
    ID value = map_get(ns->mappings, var_sym);
    TEST_ASSERT_TRUE(value != NOT_FOUND);
    return value;
}

TEST(test_require_reload_reloads_only_target_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    register_resolver_source(
        "/libs/test/reload_target.clj",
        "(ns test.reload-target)\n"
        "(defn marker [] :v1)\n");
    register_resolver_source(
        "/libs/test/reload-dep-target.clj",
        "(ns test.reload-dep-target)\n"
        "(defn marker [] :dep-v1)\n");
    register_resolver_source(
        "/libs/test/reload-root.clj",
        "(ns test.reload-root (:require [test.reload-dep-target]))\n"
        "(defn marker [] (test.reload-dep-target/marker))\n");

    (void)eval_string("(require 'test.reload-root)", g_test_eval_state);
    ID dep_before = lookup_ns_var("test.reload-dep-target", "marker");
    ID root_before = lookup_ns_var("test.reload-root", "marker");

    (void)eval_string("(require 'test.reload-root)", g_test_eval_state);
    ID dep_no_reload = lookup_ns_var("test.reload-dep-target", "marker");
    ID root_no_reload = lookup_ns_var("test.reload-root", "marker");
    TEST_ASSERT_EQUAL_PTR(root_before, root_no_reload);
    TEST_ASSERT_EQUAL_PTR(dep_before, dep_no_reload);

    (void)eval_string("(require '[test.reload-root :reload])", g_test_eval_state);
    ID dep_after_reload = lookup_ns_var("test.reload-dep-target", "marker");
    ID root_after_reload = lookup_ns_var("test.reload-root", "marker");

    TEST_ASSERT_TRUE(root_before != root_after_reload);
    TEST_ASSERT_EQUAL_PTR(dep_before, dep_after_reload);
}

TEST(test_require_reload_all_reloads_target_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    register_resolver_source(
        "/libs/test/reload-all-dep.clj",
        "(ns test.reload-all-dep)\n"
        "(defn marker [] :dep)\n");
    register_resolver_source(
        "/libs/test/reload-all-root.clj",
        "(ns test.reload-all-root (:require [test.reload-all-dep]))\n"
        "(defn marker [] (test.reload-all-dep/marker))\n");

    (void)eval_string("(require 'test.reload-all-root)", g_test_eval_state);
    ID dep_before = lookup_ns_var("test.reload-all-dep", "marker");
    ID root_before = lookup_ns_var("test.reload-all-root", "marker");

    (void)eval_string("(require 'test.reload-all-root :reload-all)", g_test_eval_state);
    ID dep_after = lookup_ns_var("test.reload-all-dep", "marker");
    ID root_after = lookup_ns_var("test.reload-all-root", "marker");

    TEST_ASSERT_TRUE(root_before != root_after);
    TEST_ASSERT_EQUAL_PTR(dep_before, dep_after);
}

// ============================================================================
// METADATA TESTS
// ============================================================================

// Test: Verify that trim has metadata (docstring) after require
TEST(test_require_trim_metadata) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
#ifdef DEBUG
#if defined(META_ENABLED) && META_ENABLED
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Resolve trim function
    CljSymbol *trim_sym = intern_symbol(SYM_CLOJURE_STRING, "trim");
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should exist");
    
    ID trim_func = ns_resolve(g_test_eval_state, trim_sym);
    TEST_ASSERT_TRUE_MESSAGE(trim_func && trim_func != NOT_FOUND, "trim function should be resolvable");
    TEST_ASSERT_TRUE_MESSAGE(TAG(trim_func) == CLJ_FUNC, "trim should be a native function");
    
    // Check that metadata exists
    ID trim_meta = meta_get((CljObject*)trim_func);
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_meta, "trim should have metadata after require");
    
    if (trim_meta) {
        // Check that metadata is a map
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_meta) == CLJ_MAP_PERSISTENT, "metadata should be a map");
        
        // Check for :doc key
        CljSymbol *doc_key = intern_symbol_global(":doc");
        TEST_ASSERT_NOT_NULL(doc_key);
        
        ID doc_value = map_get((CljPersistentMap*)trim_meta, doc_key);
        
        TEST_ASSERT_TRUE_MESSAGE(doc_value != NOT_FOUND, 
                                 "trim metadata should have :doc key");
        
        if (doc_value != NOT_FOUND) {
            TEST_ASSERT_TRUE_MESSAGE(TAG(doc_value) == CLJ_STRING, 
                                     ":doc value should be a string");
        }
    }
#endif // META_ENABLED
#endif // DEBUG
}

// Test: Verify that trim metadata can be accessed via meta function
TEST(test_require_trim_meta_function) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
#ifdef DEBUG
#if defined(META_ENABLED) && META_ENABLED
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Test: (meta clojure.string/trim) should return metadata map
    CljObject *meta_result = eval_string("(meta clojure.string/trim)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(meta_result, "meta should return a result");
    
    if (meta_result) {
        TEST_ASSERT_TRUE_MESSAGE(TAG(meta_result) == CLJ_MAP_PERSISTENT, 
                                 "meta result should be a map");
        
        // Check for :doc key
        CljObject *doc_result = eval_string("(get (meta clojure.string/trim) :doc)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(doc_result, ":doc should exist in metadata");
        
        if (doc_result) {
            TEST_ASSERT_TRUE_MESSAGE(TAG(doc_result) == CLJ_STRING, 
                                     ":doc value should be a string");
        }
    }
#endif // META_ENABLED
#endif // DEBUG
}

static void assert_registered_source_eval_throws(const char *path,
                                                 const char *source,
                                                 const char *expr,
                                                 const char *failure_message) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_NOT_NULL(expr);
    TEST_ASSERT_NOT_NULL(failure_message);

    register_resolver_source(path, source);

    TRY {
        (void)eval_string(expr, g_test_eval_state);
        TEST_FAIL_MESSAGE(failure_message);
    } CATCH(ex) {
        TEST_ASSERT_NOT_NULL(ex);
    } END_TRY
}

TEST(test_require_throws_when_namespace_has_failing_top_level_form) {
    assert_registered_source_eval_throws(
        "/libs/test/bad_require.clj",
        "(ns test.bad-require)\n"
        "(def before 1)\n"
        "(missing-top-level-symbol)\n"
        "(def after 2)\n",
        "(require 'test.bad-require)",
        "require should throw when a namespace top-level form fails");
}

TEST(test_load_file_throws_when_top_level_form_fails) {
    assert_registered_source_eval_throws(
        "/libs/test/bad-load-file.clj",
        "(ns test.bad-load-file)\n"
        "(def before 1)\n"
        "(missing-top-level-symbol)\n"
        "(def after 2)\n",
        "(load-file \"/libs/test/bad-load-file.clj\")",
        "load-file should throw when a top-level form fails");
}

