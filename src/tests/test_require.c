/*
 * Unity Tests for require functionality and clojure.string namespace in Tiny-CLJ
 * 
 * Tests for require special form and clojure.string functions
 */

#include "tests_common.h"
#include "strings.h"  // For to_cstring
#include <sys/time.h>

// Forward declaration
int load_clojure_core(EvalState *st);

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

// Debug test: Check if trim is in namespace mappings after require
TEST(test_require_debug_trim_in_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace
    (void)eval_string("(require 'clojure.string)", g_test_eval_state);
    
    // Check if namespace exists
    CljNamespace *string_ns = ns_find("clojure.string");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns, "clojure.string namespace should exist");
    TEST_ASSERT_NOT_NULL_MESSAGE(string_ns->mappings, "clojure.string namespace should have mappings");
    
    // Check if trim symbol exists
    CljSymbol *trim_sym = intern_symbol_global("trim");
    TEST_ASSERT_NOT_NULL_MESSAGE(trim_sym, "trim symbol should be interned");
    
    // Check if trim is in namespace mappings
    static CljObject not_found_sentinel = { .type = CLJ_NIL, .rc = SINGLETON_RC };
    ID trim_func = map_get(string_ns->mappings, trim_sym, (ID)&not_found_sentinel);
    
    if (trim_func == (ID)&not_found_sentinel) {
        TEST_FAIL_MESSAGE("trim not found in clojure.string namespace mappings - stubs may not have been executed");
    } else {
        TEST_ASSERT_NOT_NULL_MESSAGE(trim_func, "trim should be in namespace mappings");
        TEST_ASSERT_TRUE_MESSAGE(TAG(trim_func) == CLJ_FUNC, "trim should be a native function");
    }
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
    TEST_ASSERT_NOT_NULL_MESSAGE(blank_func, "blank? should be resolvable from clojure.string namespace");
    
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
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("Hello", clj_string_data(str1));
    
    // Test: (clojure.string/capitalize "HELLO") => "Hello"
    CljObject *result2 = eval_string("(clojure.string/capitalize \"HELLO\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 && TAG(result2) == CLJ_STRING);
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
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
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
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify first element is 3
    CljList *list = as_list((ID)result);
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
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_STRING);
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
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify first element is 3
    CljList *list = as_list((ID)result);
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
    TEST_ASSERT_TRUE(result && TAG(result) == CLJ_LIST);
    
    // Verify result is (1 2 3)
    CljList *list = as_list((ID)result);
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
    TEST_ASSERT_TRUE(core_result && TAG(core_result) == CLJ_LIST);
    
    // clojure.string/reverse for strings
    CljObject *string_result = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(string_result);
    TEST_ASSERT_TRUE(string_result && TAG(string_result) == CLJ_STRING);
    CljString *str = as_clj_string(string_result);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str));
}

