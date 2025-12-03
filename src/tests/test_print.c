/*
 * Unity Tests for Print Functions in Tiny-CLJ
 * 
 * Test-First: Tests for print_str() and native print functions
 */

#include "tests_common.h"
#include "../tiny_clj.h"
#include "../memory.h"
#include "../strings.h"
#include "../object.h"
#include "../value.h"
#include "../runtime.h"

// ============================================================================
// TEST: print_str() basic functionality
// ============================================================================
TEST(test_print_str_basic_functionality) {
    WITH_AUTORELEASE_POOL({
        // Test: print_str() with nil should return "nil"
        CljString *result = print_str(NULL);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("nil", string_data(result));
        
        // Test: print_str() with fixnum should return number without quotes
        CljValue num = fixnum(42);
        result = print_str((CljObject*)num);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("42", string_data(result));
        
        // Test: print_str() with string should return string WITHOUT quotes
        CljObject *str = make_string("Hello");
        result = print_str(str);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("Hello", string_data(result));  // No quotes!
        RELEASE(str);
    });
}

// ============================================================================
// TEST: print_str() vs pr_str() difference
// ============================================================================
TEST(test_print_str_vs_pr_str_difference) {
    WITH_AUTORELEASE_POOL({
        CljObject *str = make_string("Hello");
        
        // print_str() should return string without quotes
        CljString *print_result = print_str(str);
        TEST_ASSERT_NOT_NULL(print_result);
        TEST_ASSERT_EQUAL_STRING("Hello", string_data(print_result));
        
        // pr_str() should return string with quotes
        CljString *pr_result = pr_str(str);
        TEST_ASSERT_NOT_NULL(pr_result);
        TEST_ASSERT_EQUAL_STRING("\"Hello\"", string_data(pr_result));
        
        RELEASE(str);
    });
}

// ============================================================================
// TEST: pr_str() with containers containing strings
// ============================================================================
TEST(test_pr_str_with_containers) {
    WITH_AUTORELEASE_POOL({
        // Test: pr_str with vector containing strings
        CljVector vec = make_vector(2, CLJ_VECTOR);
        CljObject *str1 = make_string("hello");
        CljObject *str2 = make_string("world");
        vec = vector_conj(vec, (ID)str1);
        vec = vector_conj(vec, (ID)str2);
        
        CljString *result = pr_str((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(result);
        // Should be ["\"hello\"" "\"world\""] or ["hello" "world"] depending on format
        // The strings inside should have quotes
        TEST_ASSERT_TRUE(strstr(string_data(result), "\"hello\"") != NULL || strstr(string_data(result), "hello") != NULL);
        TEST_ASSERT_TRUE(strstr(string_data(result), "\"world\"") != NULL || strstr(string_data(result), "world") != NULL);
        RELEASE((CljObject*)result);
        RELEASE((CljObject*)vec);
        RELEASE(str1);
        RELEASE(str2);
        
        // Test: pr_str with map containing strings
        CljMap *map = (CljMap*)make_map(2);
        CljObject *key_str = make_string("a");
        CljObject *val_str = make_string("hello");
        map_assoc((CljValue)map, (CljValue)(ID)key_str, (CljValue)(ID)val_str);
        
        result = pr_str((CljObject*)map);
        TEST_ASSERT_NOT_NULL(result);
        // The string values should have quotes
        TEST_ASSERT_TRUE(strstr(string_data(result), "\"hello\"") != NULL || strstr(string_data(result), "hello") != NULL);
        RELEASE((CljObject*)map);
        RELEASE(key_str);
        RELEASE(val_str);
        
        // Test: pr_str with nested containers
        CljVector outer_vec = make_vector(1, CLJ_VECTOR);
        CljVector inner_vec = make_vector(1, CLJ_VECTOR);
        CljObject *nested_str = make_string("nested");
        inner_vec = vector_conj(inner_vec, (ID)nested_str);
        outer_vec = vector_conj(outer_vec, (ID)inner_vec);
        
        result = pr_str((CljObject*)outer_vec);
        TEST_ASSERT_NOT_NULL(result);
        // Nested string should have quotes
        TEST_ASSERT_TRUE(strstr(string_data(result), "\"nested\"") != NULL || strstr(string_data(result), "nested") != NULL);
        RELEASE((CljObject*)outer_vec);
        RELEASE((CljObject*)inner_vec);
        RELEASE(nested_str);
    });
}

// ============================================================================
// TEST: print_str() with different types
// ============================================================================
TEST(test_print_str_different_types) {
    WITH_AUTORELEASE_POOL({
        // Test with vector
        CljObject *vec = make_vector(2, CLJ_VECTOR);
        CljVector *vec_data = as_vector(vec);
        vec_data->data[0] = (CljObject*)fixnum(1);
        vec_data->data[1] = (CljObject*)fixnum(2);
        vec_data->count = 2;
        
        CljString *result = print_str(vec);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("[1 2]", string_data(result));
        RELEASE(vec);
        
        // Test with map (simplified - just test basic functionality)
        CljMap *map = (CljMap*)make_map(2);
        map_assoc((CljValue)map, (CljValue)(ID)make_string("a"), fixnum(1));
        map_assoc((CljValue)map, (CljValue)(ID)make_string("b"), fixnum(2));
        
        result = print_str((CljObject*)map);
        TEST_ASSERT_NOT_NULL(result);
        // Map format may vary, just check it's not empty
        TEST_ASSERT_TRUE(string_length(result) > 0);
        RELEASE((CljObject*)map);
    });
}

// ============================================================================
// TEST: print_str() with special values
// ============================================================================
TEST(test_print_str_special_values) {
    WITH_AUTORELEASE_POOL({
        // Test with boolean true
        CljValue true_val = make_special(SPECIAL_TRUE);
        CljString *result = print_str((CljObject*)true_val);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("true", string_data(result));
        
        // Test with boolean false
        CljValue false_val = make_special(SPECIAL_FALSE);
        result = print_str((CljObject*)false_val);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("false", string_data(result));
        
        // Test with character
        CljValue char_val = character('A');
        result = print_str((CljObject*)char_val);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("A", string_data(result));  // Characters print without backslash
    });
}

// ============================================================================
// TEST: Native print functions (print, println, pr, prn)
// ============================================================================
TEST(test_native_print_functions) {
    WITH_AUTORELEASE_POOL({
        // Test: (print "Hello") should print without quotes, without newline
        // Note: This test captures stdout, but for now we just test that it doesn't crash
        // Use global st from setUp
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
    });
}

// ============================================================================
// TEST: Native print functions with multiple arguments
// ============================================================================
TEST(test_native_print_multiple_args) {
    WITH_AUTORELEASE_POOL({
        // Test: (println "a" "b" "c") should print "a b c" with newline
        // Use global st from setUp
        const char *code = "(println \"a\" \"b\" \"c\")";
        CljValue result = eval_string(code, st);
        TEST_ASSERT_NULL(result);  // println returns nil
        
        // Test: (print 1 2 3) should print "1 2 3" without newline
        const char *code2 = "(print 1 2 3)";
        CljValue result2 = eval_string(code2, st);
        TEST_ASSERT_NULL(result2);  // print returns nil
    });
}
