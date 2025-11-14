/*
 * Unit Tests using Unity Framework
 * 
 * Basic unit tests for Tiny-Clj core functionality migrated from MinUnit.
 */

#include "tests_common.h"

// Forward declaration
int load_clojure_core(EvalState *st);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// BASIC FUNCTIONALITY TESTS
// ============================================================================

TEST(test_list_count) {
    // Test null pointer
    TEST_ASSERT_EQUAL_INT(0, list_count(NULL));

    // Test non-list object (this should not crash)
    // Create a proper CljObject for testing
    CljObject *int_obj = AUTORELEASE(make_string("42")); // Use string as non-list object
    TEST_ASSERT_EQUAL_INT(0, list_count((CljList*)int_obj));

    // Test empty list (clj_nil is not a list)
    CljObject *empty_list = NULL;
    TEST_ASSERT_EQUAL_INT(0, list_count((CljList*)empty_list));
}

TEST(test_list_creation) {
    // High-level test using eval_string
    
    
    // Test empty list creation - (list) returns empty list in Clojure
    CljObject *list = eval_string("(list)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(list);  // (list) returns empty list, not nil
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, list->type);
    
    // Test list with elements
    CljObject *list_with_elements = eval_string("(list 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(list_with_elements);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, list_with_elements->type);
    
    // Test count function
    CljObject *count_result = eval_string("(count (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(count_result);
    if (count_result && is_fixnum(count_result)) {
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(count_result));
    }
    
    // Clean up
}

TEST(test_symbol_creation) {
    // High-level test using eval_string
    
    // Test symbol creation (quoted symbol)
    CljObject *sym = eval_string("'test-symbol", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, sym->type);
    
    // Test symbol with namespace
    CljObject *ns_sym = eval_string("'user/test-symbol", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(ns_sym);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, ns_sym->type);
    
    // Clean up
}

TEST(test_string_creation) {
    // Test direct string creation (bypassing eval_string)

    // Test direct string creation
    CljObject *str = AUTORELEASE(make_string("hello world"));
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, str->type);

    // Clean up
}

TEST(test_vector_creation) {
    // Step 1: Test empty vector (should be singleton)
    
    // Test empty vector creation
    CljObject *vec = eval_string("[]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec->type);
    
    // Test vector with elements
    CljObject *vec2 = eval_string("[1 2 3]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec2);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, vec2->type);
    
    // Clean up
}


TEST(test_map_creation) {
    // Test map creation using CljValue API
    CljMap *map = AUTORELEASE(make_map(16));
    TEST_ASSERT_NOT_NULL(map);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP, map->base.type);
}

TEST(test_array_map_builtin) {
        EvalState *eval_state = evalstate_new(false);
        
        // Test empty map: (array-map)
        CljObject *result0 = parse("(array-map)", eval_state);
        ID eval0 = eval_parsed(result0, eval_state, NULL);
        TEST_ASSERT_EQUAL_INT(0, map_count(eval0));
        // result0 and eval0 are automatically managed by AUTORELEASE

        // Test single key-value: (array-map "a" 1)
        CljObject *eval1 = eval_string("(array-map \"a\" 1)", eval_state);
        
        TEST_ASSERT_NOT_NULL(eval1);
        TEST_ASSERT_EQUAL_INT(CLJ_MAP, eval1->type);
        TEST_ASSERT_EQUAL_INT(1, map_count((CljMap*)eval1));
        // result1 and eval1 are automatically managed by parse() and eval_parsed()

        // Test multiple pairs: (array-map "a" 1 "b" 2)
        CljObject *result2 = parse("(array-map \"a\" 1 \"b\" 2)", eval_state);
        ID eval2 = eval_parsed(result2, eval_state, NULL);
        TEST_ASSERT_EQUAL_INT(2, map_count(eval2));
        // result2 and eval2 are automatically managed by parse() and eval_parsed()

        // Test with keywords: (array-map :a 1 :b 2)
        CljObject *result3 = parse("(array-map :a 1 :b 2)", eval_state);
        ID eval3 = eval_parsed(result3, eval_state, NULL);
        TEST_ASSERT_EQUAL_INT(2, map_count(eval3));
        // result3 and eval3 are automatically managed by parse() and eval_parsed()

        evalstate_free(eval_state);
}

TEST(test_integer_creation) {
    // High-level test using eval_string
    
    // Test positive integer
    CljObject *int_val = eval_string("42", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(int_val);
    TEST_ASSERT_TRUE(is_fixnum(int_val));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(int_val));
    
    // Test negative integer
    CljObject *neg_int = eval_string("-100", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(neg_int);
    TEST_ASSERT_TRUE(is_fixnum(neg_int));
    TEST_ASSERT_EQUAL_INT(-100, as_fixnum(neg_int));
    
    // Test zero
    CljObject *zero = eval_string("0", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(zero);
    TEST_ASSERT_TRUE(is_fixnum(zero));
    TEST_ASSERT_EQUAL_INT(0, as_fixnum(zero));
    
    // Memory is automatically managed by eval_string
}

TEST(test_float_creation) {
    // High-level test using eval_string
    
    // Test positive float
    CljObject *float_val = eval_string("3.14", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(float_val);
    TEST_ASSERT_TRUE(is_fixed(float_val));
    TEST_ASSERT_TRUE(as_fixed(float_val) > 3.1f && as_fixed(float_val) < 3.2f);
    
    // Test negative float
    CljObject *neg_float = eval_string("-2.5", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(neg_float);
    TEST_ASSERT_TRUE(is_fixed(neg_float));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.5f, as_fixed(neg_float));
    
    // Test zero float
    CljObject *zero_float = eval_string("0.0", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(zero_float);
    TEST_ASSERT_TRUE(is_fixed(zero_float));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, as_fixed(zero_float));
    
    // Memory is automatically managed by eval_string
}

TEST(test_nil_creation) {
    // High-level test using eval_string
    
    
    // Test nil literal - nil is represented as NULL in our system
    CljObject *nil_obj = eval_string("nil", g_test_eval_state);
    TEST_ASSERT_NULL(nil_obj);  // nil is NULL in our system
    
    // Test nil in expressions - (count nil) throws IllegalArgumentException (Clojure-compatible)
    TRY {
        (void)eval_string("(count nil)", g_test_eval_state);
        // Should not reach here - exception should be thrown
        TEST_FAIL_MESSAGE("(count nil) should throw IllegalArgumentException");
    } CATCH(ex) {
        // Exception expected for nil (Clojure behavior)
        TEST_ASSERT_NOT_NULL_MESSAGE(ex, "Exception should be thrown");
        TEST_ASSERT_NOT_NULL_MESSAGE(ex->message, "Exception message should be set");
        const char *msg = ex->message;
        const char *found = strstr(msg, "count");
        if (!found) found = strstr(msg, "nil");
        TEST_ASSERT_NOT_NULL_MESSAGE(found, "Exception should mention 'count' or 'nil'");
    } END_TRY
    
    // Memory is automatically managed by eval_string
}

// ============================================================================
// PARSER TESTS
// ============================================================================

// Parser tests moved to parser_tests.c to avoid duplication

// ============================================================================
// MEMORY MANAGEMENT TESTS
// ============================================================================

// Memory management tests moved to memory_tests.c to avoid duplication

// ============================================================================
// EXCEPTION HANDLING TESTS
// ============================================================================

// Exception handling tests moved to exception_tests.c to avoid duplication

// ============================================================================
// CLJVALUE API TESTS (Phase 0-2)
// ============================================================================

// CljValue tests moved to test_values.c to avoid duplication

TEST(test_special_form_and) {
    
    // Initialize namespace first
    
    // (and) => true
    ID result1 = eval_string("(and)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result1));
    
    // (and true true) => true
    ID result2 = eval_string("(and true true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result2));
    
    // (and true false) => false
    ID result3 = eval_string("(and true false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_FALSE(clj_is_truthy((CljObject*)result3));
    
    // (and false true) => false (short-circuit)
    ID result4 = eval_string("(and false true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_FALSE(clj_is_truthy((CljObject*)result4));
    
    // result1, result2, result3, result4 are automatically managed by eval_string
}

TEST(test_special_form_or) {
    
    // Initialize namespace first
    
    // Test direct nil check first
    CljValue nil_val = NULL;
    TEST_ASSERT_NULL(nil_val);
    // clj_is_truthy expects CljObject*, not CljValue
    TEST_ASSERT_FALSE(clj_is_truthy((CljObject*)nil_val));
    
    // (or) => nil
    ID result1 = eval_string("(or)", g_test_eval_state);
    if (result1) {
        TEST_ASSERT_FALSE(clj_is_truthy((CljObject*)result1));
    } else {
        // nil is NULL in our system - this is correct!
    }
    
    // (or false false) => false
    ID result2 = eval_string("(or false false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_FALSE(clj_is_truthy((CljObject*)result2));
    
    // (or false true) => true
    ID result3 = eval_string("(or false true)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result3));
    
    // (or true false) => true (short-circuit)
    ID result4 = eval_string("(or true false)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(clj_is_truthy((CljObject*)result4));
    
    // result1, result2, result3, result4 are automatically managed by eval_string
}

TEST(test_special_form_when) {
    
    // (when true expr) => expr
    ID result1 = eval_string("(when true 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result1));
    
    // (when false expr) => nil
    ID result2 = eval_string("(when false 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result2); // nil is NULL in our system
    
    // (when nil expr) => nil
    ID result3 = eval_string("(when nil 42)", g_test_eval_state);
    TEST_ASSERT_NULL(result3); // nil is NULL in our system
    
    // (when true expr1 expr2) => expr2 (last expression)
    ID result4 = eval_string("(when true 1 2)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_TRUE(is_fixnum(result4));
    TEST_ASSERT_EQUAL_INT(2, as_fixnum(result4));
    
    // (when true expr1 expr2 expr3) => expr3 (last expression)
    ID result5 = eval_string("(when true 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_TRUE(is_fixnum(result5));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result5));
    
    // (when true) => nil (no body expressions)
    ID result6 = eval_string("(when true)", g_test_eval_state);
    TEST_ASSERT_NULL(result6); // nil is NULL in our system
    
    // (when true (do expr1 expr2)) => last result of do
    ID result7 = eval_string("(when true (do 10 20))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result7);
    TEST_ASSERT_TRUE(is_fixnum(result7));
    TEST_ASSERT_EQUAL_INT(20, as_fixnum(result7));
    
    // (when condition with string) => string
    ID result8 = eval_string("(when true \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result8);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, ((CljObject*)result8)->type);
    
    // (when condition with arithmetic) => result
    ID result9 = eval_string("(when true (+ 1 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result9);
    TEST_ASSERT_TRUE(is_fixnum(result9));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum(result9));
    
    // (when false with multiple expressions) => nil (condition false, body not evaluated)
    ID result10 = eval_string("(when false 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NULL(result10); // nil is NULL in our system
    
    // result1-10 are automatically managed by eval_string
}

// ============================================================================
// REGRESSION TEST: nil in if statements within functions
// ============================================================================
// This test verifies that nil is correctly treated as falsy in if statements
// within functions. Previously, nil was not correctly handled when passed as
// a parameter to a function containing an if statement.
TEST(test_if_nil_in_function_regression) {
    // Test 1: Simple fn with if and nil parameter
    // ((fn [x] (if x :then :else)) nil) => :else
    CljObject *result1 = NULL;
    TRY {
        result1 = eval_string("((fn [x] (if x :then :else)) nil)", g_test_eval_state);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "if in function should not throw exception, got: %s", ex->message);
        TEST_FAIL_MESSAGE(msg);
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result1, "((fn [x] (if x :then :else)) nil) should return :else, not NULL");
    TEST_ASSERT_TRUE_MESSAGE(result1 && TAG(result1) == CLJ_SYMBOL, "Result should be a symbol");
    CljSymbol *sym1 = as_symbol(result1);
    TEST_ASSERT_NOT_NULL(sym1);
    TEST_ASSERT_EQUAL_CHAR(':', sym1->name[0]);
    TEST_ASSERT_EQUAL_STRING("else", sym1->name + 1);
    
    // Test 2: defn with if and nil parameter
    // (defn test-nil [x] (if x 1 0))
    // (test-nil nil) => 0
    CljObject *def_result = NULL;
    TRY {
        def_result = eval_string("(defn test-nil [x] (if x 1 0))", g_test_eval_state);
        TEST_ASSERT_NOT_NULL(def_result);
        
        CljObject *result2 = eval_string("(test-nil nil)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result2, "(test-nil nil) should return 0, not NULL");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)result2), "Result should be a fixnum");
        TEST_ASSERT_EQUAL_INT(0, as_fixnum((CljValue)result2));
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "defn with if should not throw exception, got: %s", ex->message);
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    // Test 3: fn with if and nil, but no else branch
    // ((fn [x] (if x 42)) nil) => nil
    CljObject *result3 = NULL;
    TRY {
        result3 = eval_string("((fn [x] (if x 42)) nil)", g_test_eval_state);
        // When condition is falsy and no else branch, if returns nil
        TEST_ASSERT_NULL_MESSAGE(result3, "((fn [x] (if x 42)) nil) should return nil when no else branch");
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "if without else should not throw exception, got: %s", ex->message);
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    // Test 4: Multiple if statements with nil in same function
    // ((fn [x] (if x 1 (if x 2 3))) nil) => 3
    CljObject *result4 = NULL;
    TRY {
        result4 = eval_string("((fn [x] (if x 1 (if x 2 3))) nil)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result4, "Nested if with nil should return 3, not NULL");
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)result4), "Result should be a fixnum");
        TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)result4));
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Nested if should not throw exception, got: %s", ex->message);
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
    
    // Test 5: Compare nil with false - both should be falsy
    // ((fn [x] (if x :truthy :falsy)) false) => :falsy
    CljObject *result5 = NULL;
    TRY {
        result5 = eval_string("((fn [x] (if x :truthy :falsy)) false)", g_test_eval_state);
        TEST_ASSERT_NOT_NULL_MESSAGE(result5, "if with false should return :falsy, not NULL");
        TEST_ASSERT_TRUE_MESSAGE(result5 && TAG(result5) == CLJ_SYMBOL, "Result should be a symbol");
        CljSymbol *sym5 = as_symbol(result5);
        TEST_ASSERT_NOT_NULL(sym5);
        TEST_ASSERT_EQUAL_CHAR(':', sym5->name[0]);
        TEST_ASSERT_EQUAL_STRING("falsy", sym5->name + 1);
    } CATCH(ex) {
        char msg[256];
        snprintf(msg, sizeof(msg), "if with false should not throw exception, got: %s", ex->message);
        TEST_FAIL_MESSAGE(msg);
    } END_TRY
}

TEST(test_nil_in_function_environment_debug) {
    // Test: Create a function that returns its parameter
    // ((fn [x] x) nil) => nil (NULL)
    CljObject *result = eval_string("((fn [x] x) nil)", g_test_eval_state);
    // nil should be returned as NULL
    TEST_ASSERT_NULL_MESSAGE(result, "((fn [x] x) nil) should return nil (NULL)");
    
    // Test: Check if nil parameter is correctly evaluated as falsy
    // ((fn [x] (if x :truthy :falsy)) nil) => :falsy
    CljObject *result2 = eval_string("((fn [x] (if x :truthy :falsy)) nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(result2, "if with nil should return :falsy, not NULL");
    TEST_ASSERT_TRUE_MESSAGE(result2 && TAG(result2) == CLJ_SYMBOL, "Result should be a symbol");
    CljSymbol *sym = as_symbol(result2);
    TEST_ASSERT_NOT_NULL(sym);
    TEST_ASSERT_EQUAL_CHAR(':', sym->name[0]);
    TEST_ASSERT_EQUAL_STRING("falsy", sym->name + 1);
    
    // Test: Direct check of clj_is_truthy with NULL
    TEST_ASSERT_FALSE_MESSAGE(clj_is_truthy(NULL), "clj_is_truthy(NULL) should return false");
}

// ============================================================================
// SEQUENCE PERFORMANCE TESTS - MOVED TO test_sequences.c
// ============================================================================

// Sequence performance tests moved to test_sequences.c to reduce file size

TEST(test_load_multiline_file) {
    // Test multiline expressions parsing (without evaluation)
    
    // Test 1: Simple multiline function definition
    const char *multiline_def = "(def add-nums\n  (fn [a b]\n    (+ a b)))";
    CljObject *parsed1 = parse(multiline_def, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed1);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, parsed1->type);
    
    // Test 2: Multiline function with inline comments
    const char *multiline_with_comments = "(def multiply\n  (fn [x y] ; parameters\n    (* x y))) ; body";
    CljObject *parsed2 = parse(multiline_with_comments, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed2);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, parsed2->type);
    
    // Test 3: Multiline vector definition
    const char *multiline_vec = "(def my-vec\n  [1\n   2\n   3])";
    CljObject *parsed3 = parse(multiline_vec, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed3);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, parsed3->type);
    
    // Test 4: Multiline map
    const char *multiline_map = "{:a 1\n :b 2\n :c 3}";
    CljObject *parsed4 = parse(multiline_map, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed4);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP, parsed4->type);
    
    // Test 5: Multiline nested structures
    const char *multiline_nested = "[\n  {:a 1\n   :b 2}\n  (+ 1\n     2)\n  3\n]";
    CljObject *parsed5 = parse(multiline_nested, g_test_eval_state);
    TEST_ASSERT_NOT_NULL(parsed5);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, parsed5->type);
    
    // Clean up
}


// Isolated tests for easier debugging
TEST(test_first_function) {
    
    // Test first on vectors (builtin function)
    CljObject *first_result = eval_string("(first [1 2 3])", g_test_eval_state);
    if (first_result) {
        TEST_ASSERT_TRUE(is_fixnum(first_result));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(first_result));
    }
    
}

TEST(test_rest_function) {
    
    // Test rest on vectors (builtin function)
    CljObject *rest_test = eval_string("(rest [1 2 3])", g_test_eval_state);
    if (rest_test) {
        TEST_ASSERT_NOT_NULL(rest_test);
        TEST_ASSERT_TRUE(rest_test->type == CLJ_LIST || rest_test->type == CLJ_SEQ);
    }
    
}

TEST(test_cons_function) {
    
    // Test cons (builtin function)
    CljObject *cons_test = eval_string("(cons 1 '(2 3))", g_test_eval_state);
    if (cons_test) {
        TEST_ASSERT_NOT_NULL(cons_test);
        TEST_ASSERT_EQUAL_INT(CLJ_LIST, cons_test->type);
    }
    
}

TEST(test_identity_function) {
    // Test identity function from clojure.core
    // identity should return its argument unchanged
    
    // Test with number
    CljObject *result1 = eval_string("(identity 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_TRUE(is_fixnum(result1));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result1));
    
    // Test with string
    CljObject *result2 = eval_string("(identity \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_TRUE(result2 && TAG(result2) == CLJ_STRING);
    
    // Test with list
    CljObject *result3 = eval_string("(identity (list 1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_TRUE(result3 && TAG(result3) == CLJ_LIST);
    
    // Test with nil
    CljObject *result4 = eval_string("(identity nil)", g_test_eval_state);
    TEST_ASSERT_NULL(result4);  // nil is represented as NULL
    
    // Test with vector
    CljObject *result5 = eval_string("(identity [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_TRUE(result5 && TAG(result5) == CLJ_VECTOR);
    
    // Test with map
    CljObject *result6 = eval_string("(identity {:a 1 :b 2})", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result6);
    TEST_ASSERT_TRUE(result6 && TAG(result6) == CLJ_MAP);
}

TEST(test_count_vector) {
    
    // Test count on vector
    CljObject *count_result = eval_string("(count [1 2 3 4])", g_test_eval_state);
    if (count_result) {
        TEST_ASSERT_TRUE(is_fixnum(count_result));
        TEST_ASSERT_EQUAL_INT(4, as_fixnum(count_result));
    }
    
}

TEST(test_count_list) {
    
    // Test count on list
    CljObject *list_count_result = eval_string("(count (list 1 2 3))", g_test_eval_state);
    if (list_count_result) {
        TEST_ASSERT_TRUE(is_fixnum(list_count_result));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(list_count_result));
    }
    
}

TEST(test_count_string) {
    
    // Test count on string
    CljObject *string_count_result = eval_string("(count \"hello\")", g_test_eval_state);
    if (string_count_result) {
        TEST_ASSERT_TRUE(is_fixnum(string_count_result));
        TEST_ASSERT_EQUAL_INT(5, as_fixnum(string_count_result));
    }
    
}

TEST(test_map_function) {
    // Test the map higher-order function
    // NOTE: map needs to be implemented as a builtin function
    // This test is currently a placeholder that verifies the system is ready for map
    
    // Test map count
    CljObject *map_count_result = eval_string("(count {:a 1 :b 2 :c 3})", g_test_eval_state);
    if (map_count_result) {
        TEST_ASSERT_TRUE(is_fixnum(map_count_result));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(map_count_result));
    }
    
    // Test nil count - (count nil) throws IllegalArgumentException (Clojure-compatible)
    TRY {
        (void)eval_string("(count nil)", g_test_eval_state);
        TEST_FAIL_MESSAGE("(count nil) should throw IllegalArgumentException");
    } CATCH(ex) {
        // Exception expected for nil (Clojure behavior)
        TEST_ASSERT_NOT_NULL_MESSAGE(ex, "Exception should be thrown");
    } END_TRY
    
    // Test empty vector count
    CljObject *empty_vec_count = eval_string("(count [])", g_test_eval_state);
    if (empty_vec_count) {
        TEST_ASSERT_TRUE(is_fixnum(empty_vec_count));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(empty_vec_count));
    }
    
    // Test empty list count
    CljObject *empty_list_count = eval_string("(count (list))", g_test_eval_state);
    if (empty_list_count) {
        TEST_ASSERT_TRUE(is_fixnum(empty_list_count));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(empty_list_count));
    }
    
    // Test empty string count
    CljObject *empty_string_count = eval_string("(count \"\")", g_test_eval_state);
    if (empty_string_count) {
        TEST_ASSERT_TRUE(is_fixnum(empty_string_count));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(empty_string_count));
    }
    
    // Test empty map count
    CljObject *empty_map_count = eval_string("(count {})", g_test_eval_state);
    if (empty_map_count) {
        TEST_ASSERT_TRUE(is_fixnum(empty_map_count));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(empty_map_count));
    }
    
    // Test single element containers
    CljObject *single_vec_count = eval_string("(count [42])", g_test_eval_state);
    if (single_vec_count) {
        TEST_ASSERT_TRUE(is_fixnum(single_vec_count));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(single_vec_count));
    }
    
    CljObject *single_list_count = eval_string("(count (list 42))", g_test_eval_state);
    if (single_list_count) {
        TEST_ASSERT_TRUE(is_fixnum(single_list_count));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(single_list_count));
    }
    
    CljObject *single_string_count = eval_string("(count \"x\")", g_test_eval_state);
    if (single_string_count) {
        TEST_ASSERT_TRUE(is_fixnum(single_string_count));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(single_string_count));
    }
    
    CljObject *single_map_count = eval_string("(count {:a 1})", g_test_eval_state);
    if (single_map_count) {
        TEST_ASSERT_TRUE(is_fixnum(single_map_count));
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(single_map_count));
    }
    
    // TODO: When map is implemented as builtin, add tests like:
    // (map inc [1 2 3]) => (2 3 4)
    // (map square [1 2 3 4]) => (1 4 9 16)
    // (map inc []) => ()
    // (map (fn [x] (+ x 1)) [1 2 3]) => (2 3 4)
}

// ============================================================================
// RECUR TESTS - MOVED TO test_recur.c
// ============================================================================

// ============================================================================
// Namespace Lookup Tests
// ============================================================================


// ============================================================================
// FIXED-POINT ARITHMETIC TESTS - MOVED TO test_fixed_point.c
// ============================================================================

// Fixed-Point arithmetic tests moved to test_fixed_point.c to reduce file size

// Symbol output tests removed - integrated into existing test structure

// ============================================================================
// DEBUGGING TESTS FOR RECUR IMPLEMENTATION
// ============================================================================

// Test as_list function with valid list
TEST(test_as_list_valid) {
    
    // Create a simple list: (list 1 2 3)
    CljObject *list = eval_string("(list 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_TRUE(list && TAG(list) == CLJ_LIST);
    
    // Test as_list conversion
    CljList *list_data = as_list(list);
    TEST_ASSERT_NOT_NULL(list_data);
    
    // Test LIST_FIRST
    CljObject *first = LIST_FIRST(list_data);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(first));  // First element should be a fixnum (1)
}

// Test as_list function with invalid input
TEST(test_as_list_invalid) {
    // Test with non-list type - use a simple integer instead
    CljObject *int_obj = fixnum(42);
    TEST_ASSERT_NOT_NULL(int_obj);
    
    // Verify the integer is valid and not a list
    TEST_ASSERT_TRUE(IS_IMMEDIATE(int_obj));
    TEST_ASSERT_FALSE(int_obj && TAG(int_obj) == CLJ_LIST);
    
    // Note: We can't test as_list with NULL or non-list types as it throws an exception
    // This is expected behavior - as_list should only be called with valid lists
    // The function is designed to fail fast with exceptions for invalid inputs
}

// Test LIST_FIRST with valid list
TEST(test_list_first_valid) {
    
    // Create a simple list: (list 42)
    CljObject *list = eval_string("(list 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_TRUE(list && TAG(list) == CLJ_LIST);
    
    CljList *list_data = as_list(list);
    TEST_ASSERT_NOT_NULL(list_data);
    
    CljObject *first = LIST_FIRST(list_data);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_TRUE(IS_IMMEDIATE(first));
    
    // Don't RELEASE list - eval_string returns autoreleased object
}

// Test is_type function with various types
TEST(test_is_type_function) {
    
    // Test with list
    CljObject *list = eval_string("(list 1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_TRUE(list && TAG(list) == CLJ_LIST);
    TEST_ASSERT_FALSE(list && TAG(list) == CLJ_SYMBOL);
    
    // Test with symbol - use a defined symbol
    CljObject *symbol = eval_string("'test-symbol", g_test_eval_state);  // Quote the symbol to avoid evaluation
    TEST_ASSERT_NOT_NULL(symbol);
    TEST_ASSERT_TRUE(symbol && TAG(symbol) == CLJ_SYMBOL);
    TEST_ASSERT_FALSE(symbol && TAG(symbol) == CLJ_LIST);
    
    // Test with number
    CljObject *number = eval_string("42", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(number);
    TEST_ASSERT_TRUE(IS_IMMEDIATE(number));
    TEST_ASSERT_FALSE(number && TAG(number) == CLJ_SYMBOL);
    
}

// Test eval_list with function call
TEST(test_eval_list_function_call) {
    // Define a simple function
    CljObject *def_result = NULL;
    TRY {
        def_result = eval_string("(def test-fn (fn [x] (* x 2)))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Failed to define function");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(def_result);
    // No RELEASE needed - eval_string returns autoreleased object
    
    // Call the function
    CljObject *result = NULL;
    TRY {
        result = eval_string("(test-fn 5)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Failed to call function - symbol not resolved");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(IS_IMMEDIATE(result));
    
    // No RELEASE needed - eval_string returns autoreleased object
}

// ============================================================================
// ISOLATED TEST FOR DEF PROBLEM
// ============================================================================

// Test that isolates the def problem: checks if def stores symbol correctly
TEST(test_def_isolated_problem) {
    
    // Step 1: Verify namespace is initialized
    TEST_ASSERT_NOT_NULL(g_test_eval_state->current_ns);
    // Note: mappings will be created on demand by ns_define
    
    // Step 2: Define a simple value using def
    CljObject *def_result = NULL;
    TRY {
        def_result = eval_string("(def test-value 42)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("def should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(def_result);
    
    // Step 3: Check if namespace mappings exist after def
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state->current_ns->mappings, "Namespace mappings should exist after def");
    
    // Step 4: Use the symbol returned by def (should be the same as what was stored)
    CljObject *test_value_sym = def_result;  // def returns the symbol
    TEST_ASSERT_NOT_NULL(test_value_sym);
    TEST_ASSERT_TRUE(test_value_sym && TAG(test_value_sym) == CLJ_SYMBOL);
    
    CljObject *resolved = ns_resolve(g_test_eval_state, (CljSymbol*)test_value_sym);
    if (resolved) {
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)resolved), "Resolved value should be a fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(42, as_fixnum((CljValue)resolved), "Resolved value should be 42");
        RELEASE(resolved);
    } else {
        TEST_FAIL_MESSAGE("ns_resolve should find test-value after def");
    }
    
    // Step 5: Try to resolve via eval_symbol (what eval_string uses)
    CljObject *eval_resolved = NULL;
    TRY {
        eval_resolved = eval_symbol(as_symbol(test_value_sym), g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("eval_symbol should not throw exception for defined symbol");
        RELEASE(test_value_sym);
        return;
    } END_TRY
    
    if (eval_resolved) {
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)eval_resolved), "eval_symbol result should be a fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(42, as_fixnum((CljValue)eval_resolved), "eval_symbol result should be 42");
    } else {
        TEST_FAIL_MESSAGE("eval_symbol should find test-value after def");
    }
    
    // Step 6: Try to use the symbol in eval_string
    CljObject *eval_string_result = NULL;
    TRY {
        eval_string_result = eval_string("test-value", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("eval_string should resolve test-value without exception");
        RELEASE(test_value_sym);
        // Don't RELEASE eval_resolved - eval_symbol returns autoreleased object
        return;
    } END_TRY
    
    if (eval_string_result) {
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)eval_string_result), "eval_string result should be a fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(42, as_fixnum((CljValue)eval_string_result), "eval_string result should be 42");
    } else {
        TEST_FAIL_MESSAGE("eval_string should return 42 for test-value");
    }
    
    // Cleanup
    RELEASE(test_value_sym);
    // Don't RELEASE eval_resolved - eval_symbol returns autoreleased object
}

// Test that isolates the def problem with functions: checks if def stores function correctly
TEST(test_def_function_isolated_problem) {
    
    // Step 1: Define a function using def and fn
    CljObject *def_result = NULL;
    TRY {
        def_result = eval_string("(def test-fn (fn [x] (* x 2)))", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("def with fn should not throw exception");
        return;
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(def_result);
    
    // Step 2: Check if namespace mappings exist after def
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state->current_ns->mappings, "Namespace mappings should exist after def");
    
    // Step 3: Use the symbol returned by def (should be the same as what was stored)
    CljObject *test_fn_sym = def_result;  // def returns the symbol
    TEST_ASSERT_NOT_NULL(test_fn_sym);
    TEST_ASSERT_TRUE(test_fn_sym && TAG(test_fn_sym) == CLJ_SYMBOL);
    
    // Step 4: Check if mappings map exists and has entries
    if (!g_test_eval_state->current_ns->mappings) {
        TEST_FAIL_MESSAGE("Namespace mappings should exist after def");
        RELEASE(test_fn_sym);
        return;
    }
    
    // Step 5: Try to get the value directly from the map
    CljObject *direct_map_value = map_get(g_test_eval_state->current_ns->mappings, test_fn_sym, NULL);
    if (direct_map_value) {
        TEST_ASSERT_TRUE_MESSAGE(direct_map_value && TAG(direct_map_value) == CLJ_FUNC || direct_map_value && TAG(direct_map_value) == CLJ_CLOSURE, 
                                 "Direct map lookup should return a function");
    } else {
        TEST_FAIL_MESSAGE("Direct map_get should find test-fn after def");
    }
    
    // Step 6: Try to resolve the symbol directly via ns_resolve
    CljObject *resolved = ns_resolve(g_test_eval_state, as_symbol(test_fn_sym));
    if (resolved) {
        TEST_ASSERT_TRUE_MESSAGE(resolved && TAG(resolved) == CLJ_FUNC || resolved && TAG(resolved) == CLJ_CLOSURE, 
                                 "Resolved value should be a function");
    } else {
        TEST_FAIL_MESSAGE("ns_resolve should find test-fn after def (direct map lookup succeeded)");
    }
    
    // Step 4: Try to resolve via eval_symbol
    CljObject *eval_resolved = NULL;
    TRY {
        eval_resolved = eval_symbol(as_symbol(test_fn_sym), g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("eval_symbol should not throw exception for defined function");
        RELEASE(test_fn_sym);
        if (resolved) RELEASE(resolved);
        return;
    } END_TRY
    
    if (!eval_resolved) {
        TEST_FAIL_MESSAGE("eval_symbol should find test-fn after def");
    }
    
    // Step 7: Try to call the function via eval_string
    CljObject *call_result = NULL;
    TRY {
        call_result = eval_string("(test-fn 5)", g_test_eval_state);
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Calling test-fn should not throw exception");
        RELEASE(test_fn_sym);
        if (resolved) RELEASE(resolved);
        // Don't RELEASE eval_resolved - eval_symbol returns autoreleased object
        return;
    } END_TRY
    
    if (call_result) {
        TEST_ASSERT_TRUE_MESSAGE(is_fixnum((CljValue)call_result), "Function call result should be a fixnum");
        TEST_ASSERT_EQUAL_INT_MESSAGE(10, as_fixnum((CljValue)call_result), "Function call result should be 10");
    } else {
        TEST_FAIL_MESSAGE("Function call should return 10");
    }
    
    // Cleanup
    RELEASE(test_fn_sym);
    if (resolved) RELEASE(resolved);
    // Don't RELEASE eval_resolved - eval_symbol returns autoreleased object
}

// ============================================================================
// CONJ AND REST TESTS - MOVED TO test_sequences.c
// ============================================================================

// Sequence and collection tests moved to test_sequences.c to reduce file size

// ============================================================================
// CORE PREDICATE TESTS
// ============================================================================

TEST(test_identical_predicate) {
    
    // Test identical? with same object
    CljObject *vec1 = eval_string("[1 2 3]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vec1);
    
    CljObject *result1 = eval_string("(identical? [1 2 3] [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(result1)); // Different objects
    
    // Test identical? with same reference
    CljObject *result2 = eval_string("(let [x [1 2 3]] (identical? x x))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(result2)); // Same object
    
    // Test identical? with nil
    CljObject *result3 = eval_string("(identical? nil nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(result3)); // Both nil
    
    // Test identical? with different types
    CljObject *result4 = eval_string("(identical? nil [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(result4)); // Different objects
    
}

TEST(test_vector_predicate) {
    
    // Test vector? with vector
    CljObject *result1 = eval_string("(vector? [1 2 3])", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(SPECIAL_TRUE, as_special(result1));
    
    // Test vector? with list
    CljObject *result2 = eval_string("(vector? '(1 2 3))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(result2));
    
    // Test vector? with nil
    CljObject *result3 = eval_string("(vector? nil)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(result3));
    
    // Test vector? with string
    CljObject *result4 = eval_string("(vector? \"hello\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result4);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(result4));
    
    // Test vector? with number
    CljObject *result5 = eval_string("(vector? 42)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result5);
    TEST_ASSERT_EQUAL_INT(SPECIAL_FALSE, as_special(result5));
    
}

// TODO: Fix cond special form - symbol resolution issue
// TEST(test_cond_special_form) {
//     WITH_AUTORELEASE_POOL({
//         
//         // Test cond with single condition
//         CljObject *result1 = eval_string("(cond true \"yes\")", g_test_eval_state);
//         TEST_ASSERT_NOT_NULL(result1);
//         TEST_ASSERT_EQUAL_INT(CLJ_STRING, result1->type);
//         
//         // Test cond with multiple conditions
//         CljObject *result2 = eval_string("(cond false \"no\" true \"yes\")", g_test_eval_state);
//         TEST_ASSERT_NOT_NULL(result2);
//         TEST_ASSERT_EQUAL_INT(CLJ_STRING, result2->type);
//         
//         // Test cond with no matching condition
//         CljObject *result3 = eval_string("(cond false \"no\" false \"also no\")", g_test_eval_state);
//         TEST_ASSERT_NULL(result3); // Should return nil
//         
//         // Test cond with :else clause
//         CljObject *result4 = eval_string("(cond false \"no\" :else \"default\")", g_test_eval_state);
//         TEST_ASSERT_NOT_NULL(result4);
//         TEST_ASSERT_EQUAL_INT(CLJ_STRING, result4->type);
//         
//         // Test empty cond
//         CljObject *result5 = eval_string("(cond)", g_test_eval_state);
//         TEST_ASSERT_NULL(result5); // Should return nil
//         
//     });
// }

// ============================================================================
// TYPE CHECK TESTS
// ============================================================================

TEST(test_type_check_all_types) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test nil (NULL)
    TEST_ASSERT_EQUAL_INT(CLJ_NIL, TAG(NULL));
    
    // Test immediate types
    ID fixnum_val = parse("42", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fixnum_val);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(fixnum_val));
    
    // Character literals are not yet supported in the parser
    // Skip character test for now
    // ID char_val = parse("\\a", g_test_eval_state);
    // TEST_ASSERT_NOT_NULL(char_val);
    // TEST_ASSERT_EQUAL_INT(CLJ_CHAR, TAG(char_val));
    
    ID true_val = parse("true", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(true_val);
    TEST_ASSERT_EQUAL_INT(CLJ_BOOL, TAG(true_val));
    
    ID false_val = parse("false", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(false_val);
    TEST_ASSERT_EQUAL_INT(CLJ_BOOL, TAG(false_val));
    
    ID fixed_val = parse("3.14", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fixed_val);
    TEST_ASSERT_EQUAL_INT(CLJ_FLOAT, TAG(fixed_val));
    
    // Test heap object types
    // Note: parse("()") returns nil (Clojure behavior: () is nil)
    ID empty_list_val = parse("()", g_test_eval_state);
    TEST_ASSERT_NULL(empty_list_val);  // () is nil in Clojure
    TEST_ASSERT_EQUAL_INT(CLJ_NIL, TAG(empty_list_val));
    
    ID vector_val = parse("[]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vector_val);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(vector_val));
    
    ID map_val = parse("{}", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(map_val);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP, TAG(map_val));
    
    ID string_val = parse("\"hello\"", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(string_val);
    TEST_ASSERT_EQUAL_INT(CLJ_STRING, TAG(string_val));
    
    ID symbol_val = parse("foo", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(symbol_val);
    TEST_ASSERT_EQUAL_INT(CLJ_SYMBOL, TAG(symbol_val));
    
    // Test with non-empty collections
    ID list_with_elems = parse("(1 2 3)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(list_with_elems);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, TAG(list_with_elems));
    
    ID vector_with_elems = parse("[1 2 3]", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(vector_with_elems);
    TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, TAG(vector_with_elems));
    
    ID map_with_elems = parse("{:a 1 :b 2}", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(map_with_elems);
    TEST_ASSERT_EQUAL_INT(CLJ_MAP, TAG(map_with_elems));
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// ============================================================================
// EVAL TESTS
// ============================================================================

TEST(test_eval) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (eval '(+ 1 2)) => 3
    CljObject *result1 = eval_string("(eval '(+ 1 2))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result1));
    TEST_ASSERT_EQUAL_INT(3, as_fixnum((CljValue)result1));
    
    // Test: (eval '(def x 42)) => should define x
    CljObject *result2 = eval_string("(eval '(def x 42))", g_test_eval_state);
    // def returns the var, so result should not be NULL
    TEST_ASSERT_NOT_NULL(result2);
    
    // Test: (eval 'x) => 42 (after defining x)
    CljObject *result3 = eval_string("(eval 'x)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result3);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result3));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)result3));
}

// ============================================================================
// READ-STRING TESTS
// ============================================================================

TEST(test_read_string) {
    if (!g_test_eval_state) {
        TEST_FAIL_MESSAGE("Failed to create EvalState");
        return;
    }
    
    // Test: (read-string "(+ 1 2)") => list (+ 1 2)
    CljObject *result1 = eval_string("(read-string \"(+ 1 2)\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result1);
    TEST_ASSERT_EQUAL_INT(CLJ_LIST, TAG(result1));
    
    // Test: (read-string "42") => 42
    CljObject *result2 = eval_string("(read-string \"42\")", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(result2);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, TAG(result2));
    TEST_ASSERT_EQUAL_INT(42, as_fixnum((CljValue)result2));
}

// Register all tests
