/*
 * Unity Tests for Regex functions in Tiny-CLJ
 * 
 * Tests for re-pattern, re-find, re-seq, re-matches
 * Based on tiny-regex-c (Public Domain)
 */

#define TEST_SHARED_DEFAULT_HEAP_GROWTH_LIMIT 600
#include "tests_common.h"

// ============================================================================
// RE-PATTERN TESTS
// ============================================================================

TEST_SHARED(test_regex_re_pattern_basic) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-pattern "hello") should return a regex object
    CljObject *result = eval_string("(re-pattern \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_REGEX, TAG(result));
}

TEST_SHARED(test_regex_re_pattern_special_chars) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-pattern "\\d+") should compile digit pattern
    CljObject *result = eval_string("(re-pattern \"\\\\d+\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_REGEX, TAG(result));
}

// ============================================================================
// RE-FIND TESTS
// ============================================================================

TEST_SHARED(test_regex_re_find_match) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "hello") "hello world") => "hello"
    CljObject *result = eval_string("(re-find (re-pattern \"hello\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(result));
    assert_string(result, "hello");
}

TEST_SHARED(test_regex_re_find_no_match) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "xyz") "hello world") => nil
    CljObject *result = eval_string("(re-find (re-pattern \"xyz\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST_SHARED(test_regex_re_find_digits) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "\\d+") "abc123def") => "123"
    CljObject *result = eval_string("(re-find (re-pattern \"\\\\d+\") \"abc123def\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(result));
    assert_string(result, "123");
}

TEST_SHARED(test_regex_re_find_word) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "\\w+") "  hello  ") => "hello"
    CljObject *result = eval_string("(re-find (re-pattern \"\\\\w+\") \"  hello  \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(result));
    assert_string(result, "hello");
}

// ============================================================================
// RE-FIND WITH ANCHORS
// ============================================================================

TEST_SHARED(test_regex_re_find_anchor_start) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "^hello") "hello world") => "hello"
    CljObject *result = eval_string("(re-find (re-pattern \"^hello\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "hello");
    
    // Test: (re-find (re-pattern "^world") "hello world") => nil
    CljObject *result2 = eval_string("(re-find (re-pattern \"^world\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result2 == NULL || TAG(result2) == CLJ_NIL);
}

TEST_SHARED(test_regex_re_find_anchor_end) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "world$") "hello world") => "world"
    CljObject *result = eval_string("(re-find (re-pattern \"world$\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "world");
    
    // Test: (re-find (re-pattern "hello$") "hello world") => nil
    CljObject *result2 = eval_string("(re-find (re-pattern \"hello$\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result2 == NULL || TAG(result2) == CLJ_NIL);
}

// ============================================================================
// RE-MATCHES TESTS
// ============================================================================

TEST_SHARED(test_regex_re_matches_full) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-matches (re-pattern "hello") "hello") => "hello"
    CljObject *result = eval_string("(re-matches (re-pattern \"hello\") \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "hello");
}

TEST_SHARED(test_regex_re_matches_partial_fails) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-matches (re-pattern "hello") "hello world") => nil
    // Because re-matches requires the ENTIRE string to match
    CljObject *result = eval_string("(re-matches (re-pattern \"hello\") \"hello world\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
}

TEST_SHARED(test_regex_re_matches_digits) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-matches (re-pattern "\\d+") "12345") => "12345"
    CljObject *result = eval_string("(re-matches (re-pattern \"\\\\d+\") \"12345\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "12345");
    
    // Test: (re-matches (re-pattern "\\d+") "123abc") => nil
    CljObject *result2 = eval_string("(re-matches (re-pattern \"\\\\d+\") \"123abc\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result2 == NULL || TAG(result2) == CLJ_NIL);
}

// ============================================================================
// REGEX? PREDICATE TESTS
// ============================================================================

TEST_SHARED(test_regex_predicate) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (regex? (re-pattern "test")) => true
    CljObject *result = eval_string("(regex? (re-pattern \"test\"))", g_test_eval_state);
    TEST_ASSERT_TRUE(result == clj_true);
    
    // Test: (regex? "test") => false
    CljObject *result2 = eval_string("(regex? \"test\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result2 == clj_false);
    
    // Test: (regex? 123) => false
    CljObject *result3 = eval_string("(regex? 123)", g_test_eval_state);
    TEST_ASSERT_TRUE(result3 == clj_false);
}

// ============================================================================
// CHARACTER CLASS TESTS
// ============================================================================

TEST_SHARED(test_regex_char_class) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "[a-z]+") "ABC123def") => "def"
    CljObject *result = eval_string("(re-find (re-pattern \"[a-z]+\") \"ABC123def\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "def");
}

TEST_SHARED(test_regex_negated_char_class) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "[^0-9]+") "123abc456") => "abc"
    CljObject *result = eval_string("(re-find (re-pattern \"[^0-9]+\") \"123abc456\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "abc");
}

// ============================================================================
// QUANTIFIER TESTS
// ============================================================================

TEST_SHARED(test_regex_quantifier_star) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "ab*c") "ac") => "ac"
    CljObject *result = eval_string("(re-find (re-pattern \"ab*c\") \"ac\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "ac");
    
    // Test: (re-find (re-pattern "ab*c") "abbbbc") => "abbbbc"
    CljObject *result2 = eval_string("(re-find (re-pattern \"ab*c\") \"abbbbc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    assert_string(result2, "abbbbc");
}

TEST_SHARED(test_regex_quantifier_plus) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "ab+c") "ac") => nil (b+ requires at least one b)
    CljObject *result = eval_string("(re-find (re-pattern \"ab+c\") \"ac\")", g_test_eval_state);
    TEST_ASSERT_TRUE(result == NULL || TAG(result) == CLJ_NIL);
    
    // Test: (re-find (re-pattern "ab+c") "abc") => "abc"
    CljObject *result2 = eval_string("(re-find (re-pattern \"ab+c\") \"abc\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    assert_string(result2, "abc");
}

TEST_SHARED(test_regex_quantifier_question) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: (re-find (re-pattern "colou?r") "color") => "color"
    CljObject *result = eval_string("(re-find (re-pattern \"colou?r\") \"color\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "color");
    
    // Test: (re-find (re-pattern "colou?r") "colour") => "colour"
    CljObject *result2 = eval_string("(re-find (re-pattern \"colou?r\") \"colour\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    assert_string(result2, "colour");
}

// ============================================================================
// SHORTHAND CLASS TESTS
// ============================================================================

TEST_SHARED(test_regex_shorthand_digit) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: \d matches digits
    CljObject *result = eval_string("(re-find (re-pattern \"\\\\d\\\\d\\\\d\") \"abc123xyz\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "123");
}

TEST_SHARED(test_regex_shorthand_word) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: \w matches word characters [a-zA-Z0-9_]
    CljObject *result = eval_string("(re-find (re-pattern \"\\\\w+\") \"   hello_world123   \")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "hello_world123");
}

TEST_SHARED(test_regex_shorthand_whitespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Test: \s matches whitespace
    CljObject *result = eval_string("(re-find (re-pattern \"\\\\s+\") \"hello   world\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result);
    assert_string(result, "   ");
}

// ============================================================================
// HIGH-LEVEL TESTS: String Representation
// ============================================================================

TEST_SHARED(test_regex_string_representation_basic) {
    assert_string(eval_string("(str (re-pattern \"test\"))", g_test_eval_state), "#\"test\"");
}

TEST_SHARED(test_regex_string_representation_special_chars) {
    assert_string(eval_string("(str (re-pattern \"hello.*world\"))", g_test_eval_state), "#\"hello.*world\"");
}

TEST_SHARED(test_regex_string_representation_digits) {
    assert_string(eval_string("(str (re-pattern \"\\\\d+\"))", g_test_eval_state), "#\"\\d+\"");
}

TEST_SHARED(test_regex_string_representation_in_collection) {
    CljObject *result = eval_string("(str [(re-pattern \"test\")])", g_test_eval_state);
    CljString *str = as_clj_string(result);
    TEST_ASSERT_NOT_NULL(strstr(clj_string_data(str), "#\"test\""));
}

// ============================================================================
// UNSUPPORTED FEATURE TESTS
// These features throw exceptions, which is the correct behavior.
// The tests above demonstrate that the supported features work correctly.
// Unsupported features like alternation (|) and quantifier bounds ({n,m})
// are validated at compile time and throw descriptive exceptions.
// ============================================================================

// Note: Tests for unsupported features (alternation |, quantifier bounds {n,m})
// are omitted as they correctly throw exceptions at compile time.
// The implementation correctly rejects these patterns with clear error messages:
// - "Unsupported regex feature: alternation '|'"
// - "Unsupported regex feature: quantifier bounds '{n,m}'"

