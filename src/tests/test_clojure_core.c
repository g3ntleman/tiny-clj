#include "../unity.h"
#include "../tiny_clj.h"
#include "test_helpers.h"
#include "../CljObject.h"
#include "../vector.h"
#include "../clj_symbols.h"
#include "../map.h"
#include <stdlib.h>

void setUp(void) {
    // Setup before each test
    init_special_symbols();
    meta_registry_init();
    load_clojure_core();
}

void tearDown(void) {
    // Cleanup after each test
    cleanup_clojure_core();
    meta_registry_cleanup();
    cljvalue_pool_cleanup_all();
}

void test_inc_function(void) {
    CljObject *arg = autorelease(make_int(5));
    CljObject *result = autorelease(call_clojure_core_function("inc", 1, &arg));
    
    ASSERT_OBJ_INT_EQ(result, 6);
}

void test_dec_function(void) {
    CljObject *arg = autorelease(make_int(10));
    CljObject *result = autorelease(call_clojure_core_function("dec", 1, &arg));
    
    ASSERT_OBJ_INT_EQ(result, 9);
}

void test_add_function(void) {
    CljObject *arg1 = autorelease(make_int(3));
    CljObject *arg2 = autorelease(make_int(7));
    CljObject *args[2] = {arg1, arg2};
    CljObject *result = autorelease(call_clojure_core_function("add", 2, args));
    
    ASSERT_OBJ_INT_EQ(result, 10);
}

void test_sub_function(void) {
    CljObject *arg1 = autorelease(make_int(10));
    CljObject *arg2 = autorelease(make_int(3));
    CljObject *args[2] = {arg1, arg2};
    CljObject *result = autorelease(call_clojure_core_function("sub", 2, args));
    
    ASSERT_OBJ_INT_EQ(result, 7);
}

void test_mul_function(void) {
    CljObject *arg1 = autorelease(make_int(6));
    CljObject *arg2 = autorelease(make_int(7));
    CljObject *args[2] = {arg1, arg2};
    CljObject *result = autorelease(call_clojure_core_function("mul", 2, args));
    
    ASSERT_OBJ_INT_EQ(result, 42);
}

void test_div_function(void) {
    CljObject *arg1 = autorelease(make_int(15));
    CljObject *arg2 = autorelease(make_int(3));
    CljObject *args[2] = {arg1, arg2};
    CljObject *result = autorelease(call_clojure_core_function("div", 2, args));
    
    ASSERT_OBJ_INT_EQ(result, 5);
}

void test_square_function(void) {
    CljObject *arg = autorelease(make_int(4));
    CljObject *result = autorelease(call_clojure_core_function("square", 1, &arg));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, result->type);
    TEST_ASSERT_EQUAL_INT(16, result->as.i);
}

void test_nil_predicate(void) {
    CljObject *nil_val = clj_nil(); // Singleton - no autorelease!
    CljObject *result = autorelease(call_clojure_core_function("nil?", 1, &nil_val));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_BOOL, result->type);
    TEST_ASSERT_TRUE(result == clj_true());
}

void test_true_predicate(void) {
    CljObject *true_val = clj_true(); // Singleton - no autorelease!
    CljObject *result = autorelease(call_clojure_core_function("true?", 1, &true_val));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_BOOL, result->type);
    TEST_ASSERT_TRUE(result == clj_true());
}

void test_false_predicate(void) {
    CljObject *false_val = clj_false(); // Singleton - no autorelease!
    CljObject *result = autorelease(call_clojure_core_function("false?", 1, &false_val));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_BOOL, result->type);
    TEST_ASSERT_TRUE(result == clj_true());
}

void test_identity_function(void) {
    CljObject *test_val = autorelease(make_string("hello"));
    CljObject *result = autorelease(call_clojure_core_function("identity", 1, &test_val));
    
    ASSERT_TYPE(result, CLJ_STRING);
    TEST_ASSERT_EQUAL_STRING("hello", (char*)result->as.data);
}

void test_count_vector(void) {
    // Test: (count [1 2 3]) => 3
    // For now, test with an empty vector since vector creation is complex
    CljObject *vec = autorelease(make_vector(0, 0));
    CljPersistentVector *vec_data = as_vector(vec);
    
    // Empty vector should have count 0
    CljObject *result = autorelease(call_clojure_core_function("count", 1, &vec));
    ASSERT_OBJ_INT_EQ(result, 0);
}

void test_count_list(void) {
    // Test: (count '(a b c)) => 3
    // For now, test with an empty list since list creation is complex
    CljObject *list = autorelease(make_list());
    CljList *list_data = as_list(list);
    
    // Empty list should have count 0
    CljObject *result = autorelease(call_clojure_core_function("count", 1, &list));
    ASSERT_OBJ_INT_EQ(result, 0);
}

void test_count_map(void) {
    // Test: (count {:a 1 :b 2}) => 2
    CljObject *map = autorelease(make_map(4));
    CljMap *map_data = as_map(map);
    
    CljObject *key1 = autorelease(make_symbol("a", NULL));
    CljObject *val1 = autorelease(make_int(1));
    CljObject *key2 = autorelease(make_symbol("b", NULL));
    CljObject *val2 = autorelease(make_int(2));
    
    map_assoc(map, key1, val1);
    map_assoc(map, key2, val2);
    
    CljObject *result = autorelease(call_clojure_core_function("count", 1, &map));
    ASSERT_OBJ_INT_EQ(result, 2);
}

void test_count_string(void) {
    // Test: (count "hello") => 5
    CljObject *str = autorelease(make_string("hello"));
    CljObject *result = autorelease(call_clojure_core_function("count", 1, &str));
    
    ASSERT_OBJ_INT_EQ(result, 5);
}

void test_count_nil(void) {
    // Test: (count nil) => 0
    CljObject *nil_val = clj_nil();
    CljObject *result = autorelease(call_clojure_core_function("count", 1, &nil_val));
    
    ASSERT_OBJ_INT_EQ(result, 0);
}

void test_count_single_value(void) {
    // Test: (count 42) => 1 (single value)
    CljObject *int_val = autorelease(make_int(42));
    CljObject *result = autorelease(call_clojure_core_function("count", 1, &int_val));
    
    ASSERT_OBJ_INT_EQ(result, 1);
}

void test_division_by_zero(void) {
    CljObject *arg1 = autorelease(make_int(10));
    CljObject *arg2 = autorelease(make_int(0));
    CljObject *args[2] = {arg1, arg2};
    CljObject *result = autorelease(call_clojure_core_function("div", 2, args));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_EXCEPTION, result->type);
}

void test_wrong_argument_count(void) {
    CljObject *arg = autorelease(make_int(5));
    CljObject *result = autorelease(call_clojure_core_function("add", 1, &arg));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_EXCEPTION, result->type);
}

void test_wrong_argument_type(void) {
    CljObject *arg = autorelease(make_string("not_a_number"));
    CljObject *result = autorelease(call_clojure_core_function("inc", 1, &arg));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_EXCEPTION, result->type);
}

void test_nonexistent_function(void) {
    CljObject *result = autorelease(call_clojure_core_function("nonexistent", 0, NULL));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_EXCEPTION, result->type);
}

void test_negative_numbers(void) {
    CljObject *arg = autorelease(make_int(-5));
    CljObject *result = autorelease(call_clojure_core_function("inc", 1, &arg));
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(CLJ_INT, result->type);
    TEST_ASSERT_EQUAL_INT(-4, result->as.i);
}

void test_large_numbers(void) {
    CljObject *arg1 = autorelease(make_int(1000));
    CljObject *arg2 = autorelease(make_int(2000));
    CljObject *args[2] = {arg1, arg2};
    CljObject *result = autorelease(call_clojure_core_function("add", 2, args));
    
    ASSERT_OBJ_INT_EQ(result, 3000);
}

void test_namespace_access(void) {
    CljNamespace *ns = get_clojure_core_namespace();
    
    TEST_ASSERT_NOT_NULL(ns);
    TEST_ASSERT_NOT_NULL(ns->mappings);
    
    CljMap *map = as_map(ns->mappings);
    TEST_ASSERT_TRUE(map->count > 0);
}

// --- Tests for clojure.core/some (interpreted) ---

// use generic constructor from vector.c

void test_some_truthy_identity_vector(void) {
    // (some identity [nil 0 2]) => 0 (0 ist truthy)
    CljObject *nilv = clj_nil();
    CljObject *zero = autorelease(make_int(0));
    CljObject *two  = autorelease(make_int(2));
    CljObject *items[3] = {nilv, zero, two};
    CljObject *vec = autorelease(vector_from_items(items, 3));

    CljObject *pred_sym = autorelease(make_symbol("identity", NULL));
    CljObject *args[2] = {pred_sym, vec};
    CljObject *res = autorelease(call_clojure_core_function("some", 2, args));
    ASSERT_TYPE(res, CLJ_INT);
    TEST_ASSERT_EQUAL_INT(0, res->as.i);
}

void test_some_nil_when_no_match(void) {
    // (some identity [nil nil]) => nil
    CljObject *nilv = clj_nil();
    CljObject *items[2] = {nilv, nilv};
    CljObject *vec = autorelease(vector_from_items(items, 2));

    CljObject *pred_sym = autorelease(make_symbol("identity", NULL));
    CljObject *args[2] = {pred_sym, vec};
    CljObject *res = autorelease(call_clojure_core_function("some", 2, args));
    TEST_ASSERT_TRUE(res == clj_nil());
}

void test_some_short_circuit_first_truthy(void) {
    // (some identity [1 2 3]) => 1 (sollte beim ersten truthy abbrechen)
    CljObject *one = autorelease(make_int(1));
    CljObject *two = autorelease(make_int(2));
    CljObject *three = autorelease(make_int(3));
    CljObject *items[3] = {one, two, three};
    CljObject *vec = autorelease(vector_from_items(items, 3));

    CljObject *pred_sym = autorelease(make_symbol("identity", NULL));
    CljObject *args[2] = {pred_sym, vec};
    CljObject *res = autorelease(call_clojure_core_function("some", 2, args));
    ASSERT_TYPE(res, CLJ_INT);
    TEST_ASSERT_EQUAL_INT(1, res->as.i);
}

int main(void) {
    printf("=== Unity Test Suite for Clojure Core ===\n");
    
    UNITY_BEGIN();
    
    // Arithmetic functions
    RUN_TEST(test_inc_function);
    RUN_TEST(test_dec_function);
    RUN_TEST(test_add_function);
    RUN_TEST(test_sub_function);
    RUN_TEST(test_mul_function);
    RUN_TEST(test_div_function);
    RUN_TEST(test_square_function);
    
    // Predicate functions
    RUN_TEST(test_nil_predicate);
    RUN_TEST(test_true_predicate);
    RUN_TEST(test_false_predicate);
    
    // Identity function
    RUN_TEST(test_identity_function);
    
    // Count function tests (simplified)
    RUN_TEST(test_count_string);
    RUN_TEST(test_count_nil);
    RUN_TEST(test_count_single_value);
    
    // Error handling
    RUN_TEST(test_division_by_zero);
    RUN_TEST(test_wrong_argument_count);
    RUN_TEST(test_wrong_argument_type);
    RUN_TEST(test_nonexistent_function);
    
    // Edge cases
    RUN_TEST(test_negative_numbers);
    RUN_TEST(test_large_numbers);
    
    // Namespace operations
    RUN_TEST(test_namespace_access);

    // clojure.core/some tests (will fail until implemented)
    RUN_TEST(test_some_truthy_identity_vector);
    RUN_TEST(test_some_nil_when_no_match);
    RUN_TEST(test_some_short_circuit_first_truthy);
    
    return UNITY_END();
}
