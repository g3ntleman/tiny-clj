/*
 * Unity Tests for clojure.string functions in Tiny-CLJ
 * 
 * Tests for string manipulation functions from clojure.string namespace
 */

#include "tests_common.h"
#include "namespace.h"
#include "symbol.h"
#include "map.h"
#include "object.h"
#include "kv_macros.h"

// Forward declaration
int load_clojure_core(EvalState *st);

// ============================================================================
// HELPER: Load clojure.string namespace
// ============================================================================

static void load_clojure_string_namespace(void) {
    // Load clojure.string namespace using eval_string (required for test isolation)
    // NOTE: native_require creates its own EvalState, which breaks test isolation.
    // Using eval_string ensures the namespace is loaded in the correct EvalState context.
    // CRITICAL: This must be called before any clojure.string function tests.
    // NOTE: Each test runs in isolation, so we can't cache the namespace loading.
    CljObject *req_result = eval_string("(require 'clojure.string)", g_test_eval_state);
    (void)req_result; // require returns nil
}

// ============================================================================
// BLANK? TESTS
// ============================================================================

TEST(test_string_blank) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
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

// ============================================================================
// CAPITALIZE TESTS
// ============================================================================

TEST(test_string_capitalize) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
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

// ============================================================================
// ENDS-WITH? TESTS
// ============================================================================

TEST(test_string_ends_with) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/ends-with? "hello" "lo") => true
    CljObject *result1 = eval_string("(clojure.string/ends-with? \"hello\" \"lo\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/ends-with? "hello" "x") => false
    CljObject *result2 = eval_string("(clojure.string/ends-with? \"hello\" \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

// ============================================================================
// ESCAPE TESTS
// ============================================================================

TEST(test_string_escape) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/escape "abc" {}) => "abc"
    CljObject *result1 = eval_string("(clojure.string/escape \"abc\" {})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("abc", clj_string_data(str1));
}

// ============================================================================
// INCLUDES? TESTS
// ============================================================================

TEST(test_string_includes) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/includes? "hello" "ell") => true
    CljObject *result1 = eval_string("(clojure.string/includes? \"hello\" \"ell\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);
    
    // Test: (clojure.string/includes? "hello" "xyz") => false
    CljObject *result2 = eval_string("(clojure.string/includes? \"hello\" \"xyz\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);
}

// ============================================================================
// INDEX-OF TESTS
// ============================================================================

TEST(test_string_index_of) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/index-of "hello" "l" 0) => 2
    CljObject *result1 = eval_string("(clojure.string/index-of \"hello\" \"l\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result1));
}

// ============================================================================
// REVERSE TESTS (clojure.string/reverse for strings)
// ============================================================================

TEST(test_string_reverse) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Load clojure.string namespace first
    load_clojure_string_namespace();
    
    // Test: (clojure.string/reverse "abc") => "cba"
    CljObject *result1 = eval_string("(clojure.string/reverse \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 && TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("cba", clj_string_data(str1));
}

// ============================================================================
// TESTS FOR REVERSE CONFLICTS
// ============================================================================






