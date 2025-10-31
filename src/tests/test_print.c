/*
 * Unity Tests for Print Functions in Tiny-CLJ
 * 
 * Test-First: Tests for print_str() and native print functions
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../clj_strings.h"
#include "../object.h"
#include "../value.h"
#include "../runtime.h"

// ============================================================================
// TEST: print_str() basic functionality
// ============================================================================
TEST(test_print_str_basic_functionality) {
    // Test: print_str() with nil should return "nil"
    char *result = print_str(NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("nil", result);
    free(result);
    
    // Test: print_str() with fixnum should return number without quotes
    CljValue num = fixnum(42);
    result = print_str((CljObject*)num);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("42", result);
    free(result);
    
    // Test: print_str() with string should return string WITHOUT quotes
    CljObject *str = make_string("Hello");
    result = print_str(str);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello", result);  // No quotes!
    free(result);
    RELEASE(str);
}

// ============================================================================
// TEST: print_str() vs pr_str() difference
// ============================================================================
TEST(test_print_str_vs_pr_str_difference) {
    CljObject *str = make_string("Hello");
    
    // print_str() should return string without quotes
    char *print_result = print_str(str);
    TEST_ASSERT_NOT_NULL(print_result);
    TEST_ASSERT_EQUAL_STRING("Hello", print_result);
    free(print_result);
    
    // pr_str() should return string with quotes
    char *pr_result = pr_str(str);
    TEST_ASSERT_NOT_NULL(pr_result);
    TEST_ASSERT_EQUAL_STRING("\"Hello\"", pr_result);
    free(pr_result);
    
    RELEASE(str);
}

// ============================================================================
// TEST: print_str() with different types
// ============================================================================
TEST(test_print_str_different_types) {
    // Test with vector (via Parser, kein manueller Aufbau)
    EvalState *st = evalstate();
    ID vec = parse("[1 2]", st);
    TEST_ASSERT_NOT_NULL(vec);
    
    char *result = print_str((CljObject*)vec);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("[1 2]", result);
    free(result);
    RELEASE((CljObject*)vec);
    
    // Test with map (simplified - just test basic functionality)
    CljMap *map = (CljMap*)make_map(2);
    map_assoc_cow((CljValue)map, (CljValue)make_string("a"), fixnum(1));
    map_assoc_cow((CljValue)map, (CljValue)make_string("b"), fixnum(2));
    
    result = print_str((CljObject*)map);
    TEST_ASSERT_NOT_NULL(result);
    // Map format may vary, just check it's not empty
    TEST_ASSERT_TRUE(strlen(result) > 0);
    free(result);
    RELEASE((CljObject*)map);
}

// ============================================================================
// TEST: print_str() with special values
// ============================================================================
TEST(test_print_str_special_values) {
    // Test with boolean true
    CljValue true_val = make_special(SPECIAL_TRUE);
    char *result = print_str((CljObject*)true_val);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("true", result);
    free(result);
    
    // Test with boolean false
    CljValue false_val = make_special(SPECIAL_FALSE);
    result = print_str((CljObject*)false_val);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("false", result);
    free(result);
    
    // Test with character
    CljValue char_val = character('A');
    result = print_str((CljObject*)char_val);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("A", result);  // Characters print without backslash
    free(result);
}

// ============================================================================
// TEST: Native print functions (print, println, pr, prn)
// ============================================================================
TEST(test_native_print_functions) {
    EvalState *st = evalstate_new();
    
    // Test: (print "Hello") should print without quotes, without newline
    // Note: This test captures stdout, but for now we just test that it doesn't crash
    const char *code1 = "(print \"Hello\")";
    CljValue result1 = eval_string(code1, st);
    TEST_ASSERT_NULL(result1);  // print returns nil
    
    // Test: (println "Hello") should print without quotes, with newline
    const char *code2 = "(println \"Hello\")";
    CljValue result2 = eval_string(code2, st);
    TEST_ASSERT_NULL(result2);  // println returns nil
    
    // Test: (pr "Hello") should print with quotes, without newline
    const char *code3 = "(pr \"Hello\")";
    CljValue result3 = eval_string(code3, st);
    TEST_ASSERT_NULL(result3);  // pr returns nil
    
    // Test: (prn "Hello") should print with quotes, with newline
    const char *code4 = "(prn \"Hello\")";
    CljValue result4 = eval_string(code4, st);
    TEST_ASSERT_NULL(result4);  // prn returns nil
    
    evalstate_free(st);
}

// ============================================================================
// TEST: Native print functions with multiple arguments
// ============================================================================
TEST(test_native_print_multiple_args) {
    EvalState *st = evalstate_new();
    
    // Test: (println "a" "b" "c") should print "a b c" with newline
    const char *code = "(println \"a\" \"b\" \"c\")";
    CljValue result = eval_string(code, st);
    TEST_ASSERT_NULL(result);  // println returns nil
    
    // Test: (print 1 2 3) should print "1 2 3" without newline
    const char *code2 = "(print 1 2 3)";
    CljValue result2 = eval_string(code2, st);
    TEST_ASSERT_NULL(result2);  // print returns nil
    
    evalstate_free(st);
}

// ============================================================================
// TEST: to_string() formatting for fixed-point (Q16.13) without float printf
// ============================================================================
TEST(test_to_string_fixed_format) {
    // Exact representable values in Q16.13
    CljValue f0 = fixed(0.0f);
    CljValue f1 = fixed(1.0f);
    CljValue fneg = fixed(-2.5f);   // -2.5 = -2 - 1/2
    CljValue f075 = fixed(0.75f);   // 0.75 = 3/4
    CljValue f3125 = fixed(3.125f); // 3 + 1/8

    char *s0 = to_string((CljObject*)f0);
    char *s1 = to_string((CljObject*)f1);
    char *sneg = to_string((CljObject*)fneg);
    char *s075 = to_string((CljObject*)f075);
    char *s3125 = to_string((CljObject*)f3125);

    TEST_ASSERT_NOT_NULL(s0);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_NOT_NULL(sneg);
    TEST_ASSERT_NOT_NULL(s075);
    TEST_ASSERT_NOT_NULL(s3125);

    TEST_ASSERT_EQUAL_STRING("0", s0);
    TEST_ASSERT_EQUAL_STRING("1", s1);
    TEST_ASSERT_EQUAL_STRING("-2.5", sneg);
    TEST_ASSERT_EQUAL_STRING("0.75", s075);
    TEST_ASSERT_EQUAL_STRING("3.125", s3125);

    free(s0);
    free(s1);
    free(sneg);
    free(s075);
    free(s3125);
}
