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

TEST_SHARED(test_string_blank) {
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

TEST_SHARED(test_string_capitalize) {
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

TEST_SHARED(test_string_ends_with) {
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
// STARTS-WITH? TESTS
// ============================================================================

TEST_SHARED(test_string_starts_with) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    load_clojure_string_namespace();

    CljObject *result1 = eval_string("(clojure.string/starts-with? \"hello\" \"he\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);

    CljObject *result2 = eval_string("(clojure.string/starts-with? \"hello\" \"lo\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_false);

    CljObject *result3 = eval_string("(clojure.string/starts-with? nil \"he\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 == clj_false);
}

// ============================================================================
// ESCAPE TESTS
// ============================================================================

TEST_SHARED(test_string_escape) {
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

TEST_SHARED(test_string_includes) {
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
// TRIML/TRIMR/TRIM-NEWLINE TESTS
// ============================================================================

TEST_SHARED(test_string_trim_left_right_newline) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();

    CljObject *triml = eval_string("(clojure.string/triml \"  hi\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(triml);
    TEST_ASSERT_TRUE(TAG(triml) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("hi", clj_string_data(as_clj_string(triml)));

    CljObject *trimr = eval_string("(clojure.string/trimr \"hi  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(trimr);
    TEST_ASSERT_TRUE(TAG(trimr) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("hi", clj_string_data(as_clj_string(trimr)));

    CljObject *trim_newline = eval_string("(clojure.string/trim-newline \"hi\\n\\r\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(trim_newline);
    TEST_ASSERT_TRUE(TAG(trim_newline) == CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("hi", clj_string_data(as_clj_string(trim_newline)));
}

// ============================================================================
// JOIN2 / SPLIT-LINES REGRESSION TESTS
// ============================================================================

TEST_SHARED(test_string_join2_matches_join_behavior) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();

    CljObject *result1 = eval_string("(= (clojure.string/join2 \",\" '(\"a\" \"b\" \"c\")) \"a,b,c\")",
                                     g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(result1 == clj_true);

    CljObject *result2 = eval_string("(= (clojure.string/join2 nil '(\"a\" \"b\")) \"ab\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 == clj_true);
}

TEST_SHARED(test_string_split_lines_handles_lf_and_crlf) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();

    CljObject *lf = eval_string("(= (clojure.string/split-lines \"a\\nb\") [\"a\" \"b\"])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(lf);
    TEST_ASSERT_TRUE(lf == clj_true);

    CljObject *crlf = eval_string("(= (clojure.string/split-lines \"a\\r\\nb\") [\"a\" \"b\"])",
                                  g_test_eval_state);
    TEST_ASSERT_NOT_NULL(crlf);
    TEST_ASSERT_TRUE(crlf == clj_true);
}

// ============================================================================
// INDEX-OF TESTS
// ============================================================================

TEST_SHARED(test_string_index_of) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();

    // Basic hit at start
    CljObject *r1 = eval_string("(clojure.string/index-of \"hello\" \"h\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_TRUE(is_fixnum(r1));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(r1));

    // Hit in the middle (first occurrence returned)
    CljObject *r2 = eval_string("(clojure.string/index-of \"hello\" \"l\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_TRUE(is_fixnum(r2));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(r2));

    // Hit at end
    CljObject *r3 = eval_string("(clojure.string/index-of \"hello\" \"o\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_TRUE(is_fixnum(r3));
    TEST_ASSERT_EQUAL_INT(4, as_fixnum(r3));

    // Not found → nil
    CljObject *r4 = eval_string("(clojure.string/index-of \"hello\" \"z\" 0)", g_test_eval_state);
    TEST_ASSERT_NULL(r4);

    // from-index skips earlier occurrence
    CljObject *r5 = eval_string("(clojure.string/index-of \"hello\" \"l\" 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r5);
    TEST_ASSERT_TRUE(is_fixnum(r5));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(r5));

    // Multi-char substring
    CljObject *r6 = eval_string("(clojure.string/index-of \"abcabc\" \"bc\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r6);
    TEST_ASSERT_TRUE(is_fixnum(r6));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(r6));

    // Empty needle → 0
    CljObject *r7 = eval_string("(clojure.string/index-of \"hello\" \"\" 0)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(r7);
    TEST_ASSERT_TRUE(is_fixnum(r7));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(r7));

    // Search in empty string → nil for non-empty needle
    CljObject *r8 = eval_string("(clojure.string/index-of \"\" \"a\" 0)", g_test_eval_state);
    TEST_ASSERT_NULL(r8);
}

// ============================================================================
// REVERSE TESTS (clojure.string/reverse for strings)
// ============================================================================

TEST_SHARED(test_string_reverse) {
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

// ============================================================================
// PAD-LEFT TESTS
// ============================================================================

TEST(test_string_pad_left_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();
    
    // Test: (clojure.string/pad-left "5" 3 "0") => "005"
    CljObject *result1 = eval_string("(clojure.string/pad-left \"5\" 3 \"0\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("005", clj_string_data(str1));
}

TEST(test_string_pad_left_no_padding_needed) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();
    
    // Test: String already at width - no padding
    CljObject *result1 = eval_string("(clojure.string/pad-left \"abc\" 3 \"0\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(TAG(result1) == CLJ_STRING);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("abc", clj_string_data(str1));
    
    // Test: String longer than width - unchanged
    CljObject *result2 = eval_string("(clojure.string/pad-left \"abcdef\" 3 \"0\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    CljString *str2 = as_clj_string(result2);
    TEST_ASSERT_EQUAL_STRING("abcdef", clj_string_data(str2));
}

TEST(test_string_pad_left_empty_string) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    load_clojure_string_namespace();
    
    // Test: Padding empty string
    CljObject *result1 = eval_string("(clojure.string/pad-left \"\" 3 \"x\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    CljString *str1 = as_clj_string(result1);
    TEST_ASSERT_EQUAL_STRING("xxx", clj_string_data(str1));
}
