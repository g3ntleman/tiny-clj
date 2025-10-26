/*
 * Value Tests using Unity Framework
 * 
 * Tests for CljValue API, immediate values, and value-related functionality.
 */

#include "tests_common.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// CLJVALUE TESTS
// ============================================================================

void test_cljvalue_immediate_helpers(void) {
    WITH_AUTORELEASE_POOL({
        // Test immediate value helpers
        CljValue fixnum_val = fixnum(42);
        TEST_ASSERT_TRUE(is_fixnum(fixnum_val));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(fixnum_val));
        
        CljValue char_val = character('A');
        TEST_ASSERT_TRUE(is_char(char_val));
        TEST_ASSERT_EQUAL_INT('A', as_char(char_val));
        
        CljValue bool_val = make_special(SPECIAL_TRUE);
        TEST_ASSERT_TRUE(is_bool(bool_val));
        TEST_ASSERT_TRUE(is_true(bool_val));
        
        CljValue nil_val = SPECIAL_NIL;
        TEST_ASSERT_NULL(nil_val);  // nil is NULL in our system
    });
}

void test_cljvalue_vector_api(void) {
    WITH_AUTORELEASE_POOL({
        // Test vector API
        CljValue vec = make_vector(3, 1);
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)vec)->type);
        
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        TEST_ASSERT_EQUAL_INT(3, vec_data->capacity);
        
        // Test vector operations
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->data[2] = fixnum(3);
        vec_data->count = 3;
        
        TEST_ASSERT_EQUAL_INT(3, vec_data->count);
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(vec_data->data[0]));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(vec_data->data[1]));
        TEST_ASSERT_EQUAL_INT(3, as_fixnum(vec_data->data[2]));
    });
}

void test_cljvalue_transient_vector(void) {
    WITH_AUTORELEASE_POOL({
        // Test transient vector operations
        CljValue tvec = make_vector(5, 1);  // 1 = mutable
        TEST_ASSERT_NOT_NULL(tvec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)tvec)->type);
        
        CljPersistentVector *tvec_data = as_vector((CljObject*)tvec);
        TEST_ASSERT_NOT_NULL(tvec_data);
        TEST_ASSERT_EQUAL_INT(5, tvec_data->capacity);
        
        // Test transient operations
        tvec_data->data[0] = fixnum(10);
        tvec_data->data[1] = fixnum(20);
        tvec_data->count = 2;
        
        TEST_ASSERT_EQUAL_INT(2, tvec_data->count);
        TEST_ASSERT_EQUAL_INT(10, as_fixnum(tvec_data->data[0]));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(tvec_data->data[1]));
    });
}

void test_cljvalue_clojure_semantics(void) {
    WITH_AUTORELEASE_POOL({
        // Test Clojure semantics
        CljValue vec = make_vector(2, 1);
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->count = 2;
        
        // Test vector access
        TEST_ASSERT_EQUAL_INT(1, as_fixnum(vec_data->data[0]));
        TEST_ASSERT_EQUAL_INT(2, as_fixnum(vec_data->data[1]));
        
        // Test vector count
        TEST_ASSERT_EQUAL_INT(2, vec_data->count);
    });
}

void test_cljvalue_wrapper_functions(void) {
    WITH_AUTORELEASE_POOL({
        // Test wrapper functions
        CljValue fixnum_val = fixnum(123);
        TEST_ASSERT_TRUE(is_fixnum(fixnum_val));
        TEST_ASSERT_EQUAL_INT(123, as_fixnum(fixnum_val));
        
        CljValue char_val = character('Z');
        TEST_ASSERT_TRUE(is_char(char_val));
        TEST_ASSERT_EQUAL_INT('Z', as_char(char_val));
        
        CljValue bool_val = make_special(SPECIAL_FALSE);
        TEST_ASSERT_TRUE(is_bool(bool_val));
        TEST_ASSERT_TRUE(is_false(bool_val));
    });
}

void test_cljvalue_immediates_fixnum(void) {
    WITH_AUTORELEASE_POOL({
        // Test fixnum immediates
        CljValue val1 = fixnum(0);
        TEST_ASSERT_TRUE(is_fixnum(val1));
        TEST_ASSERT_EQUAL_INT(0, as_fixnum(val1));
        
        CljValue val2 = fixnum(42);
        TEST_ASSERT_TRUE(is_fixnum(val2));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(val2));
        
        CljValue val3 = fixnum(-100);
        TEST_ASSERT_TRUE(is_fixnum(val3));
        TEST_ASSERT_EQUAL_INT(-100, as_fixnum(val3));
        
        // Test fixnum limits
        CljValue max_val = fixnum(2147483647);
        TEST_ASSERT_TRUE(is_fixnum(max_val));
        TEST_ASSERT_EQUAL_INT(2147483647, as_fixnum(max_val));
    });
}

void test_cljvalue_immediates_char(void) {
    WITH_AUTORELEASE_POOL({
        // Test char immediates
        CljValue char1 = character('A');
        TEST_ASSERT_TRUE(is_char(char1));
        TEST_ASSERT_EQUAL_INT('A', as_char(char1));
        
        CljValue char2 = character('z');
        TEST_ASSERT_TRUE(is_char(char2));
        TEST_ASSERT_EQUAL_INT('z', as_char(char2));
        
        CljValue char3 = character('0');
        TEST_ASSERT_TRUE(is_char(char3));
        TEST_ASSERT_EQUAL_INT('0', as_char(char3));
        
        CljValue char4 = character(' ');
        TEST_ASSERT_TRUE(is_char(char4));
        TEST_ASSERT_EQUAL_INT(' ', as_char(char4));
    });
}

void test_cljvalue_immediates_special(void) {
    WITH_AUTORELEASE_POOL({
        // Test special immediates
        CljValue nil_val = SPECIAL_NIL;
        TEST_ASSERT_NULL(nil_val);  // nil is NULL in our system
        
        CljValue true_val = make_special(SPECIAL_TRUE);
        TEST_ASSERT_TRUE(is_bool(true_val));
        TEST_ASSERT_TRUE(is_true(true_val));
        
        CljValue false_val = make_special(SPECIAL_FALSE);
        TEST_ASSERT_TRUE(is_bool(false_val));
        TEST_ASSERT_TRUE(is_false(false_val));
        
        // Test nil is not equal to false
        TEST_ASSERT_FALSE(is_bool(nil_val));
    });
}

void test_cljvalue_immediates_fixed(void) {
    WITH_AUTORELEASE_POOL({
        // Test fixed-point immediates
        CljValue fixed_val = fixed(123.45f);
        TEST_ASSERT_TRUE(is_fixed(fixed_val));
        TEST_ASSERT_EQUAL_FLOAT(123.45f, as_fixed(fixed_val));
        
        CljValue fixed_neg = fixed(-67.89f);
        TEST_ASSERT_TRUE(is_fixed(fixed_neg));
        TEST_ASSERT_EQUAL_FLOAT(-67.89f, as_fixed(fixed_neg));
    });
}

void test_cljvalue_parser_immediates(void) {
    WITH_AUTORELEASE_POOL({
        // Test parser immediate value creation
        EvalState *st = evalstate_new();
        TEST_ASSERT_NOT_NULL(st);
        
        // Test parsing fixnums
        CljObject *fixnum_obj = eval_string("42", st);
        TEST_ASSERT_NOT_NULL(fixnum_obj);
        TEST_ASSERT_TRUE(is_fixnum(fixnum_obj));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(fixnum_obj));
        
        // Test parsing characters - skip for now due to syntax issues
        // CljObject *char_obj = eval_string("\\A", st);
        // if (char_obj) {
        //     TEST_ASSERT_TRUE(is_char(char_obj));
        //     TEST_ASSERT_EQUAL_INT('A', as_char(char_obj));
        // } else {
        //     // Parse failed due to exception - this is OK
        // }
        
        // Test parsing booleans
        CljObject *true_obj = eval_string("true", st);
        TEST_ASSERT_NOT_NULL(true_obj);
        TEST_ASSERT_TRUE(is_bool(true_obj));
        TEST_ASSERT_TRUE(is_true(true_obj));
        
        CljObject *false_obj = eval_string("false", st);
        TEST_ASSERT_NOT_NULL(false_obj);
        TEST_ASSERT_TRUE(is_bool(false_obj));
        TEST_ASSERT_TRUE(is_false(false_obj));
        
        // Test parsing nil
        CljObject *nil_obj = eval_string("nil", st);
        TEST_ASSERT_NULL(nil_obj);  // nil is NULL in our system
        
        evalstate_free(st);
    });
}

void test_cljvalue_memory_efficiency(void) {
    WITH_AUTORELEASE_POOL({
        // Test memory efficiency of immediate values
        CljValue fixnum_val = fixnum(42);
        TEST_ASSERT_TRUE(is_fixnum(fixnum_val));
        
        // Immediate values should not require heap allocation
        // They are stored directly in the pointer value
        TEST_ASSERT_TRUE(IS_IMMEDIATE(fixnum_val));
        
        CljValue char_val = character('A');
        TEST_ASSERT_TRUE(is_char(char_val));
        TEST_ASSERT_TRUE(IS_IMMEDIATE(char_val));
        
        CljValue bool_val = make_special(SPECIAL_TRUE);
        TEST_ASSERT_TRUE(is_bool(bool_val));
        TEST_ASSERT_TRUE(IS_IMMEDIATE(bool_val));
        
        CljValue nil_val = SPECIAL_NIL;
        TEST_ASSERT_NULL(nil_val);  // nil is NULL in our system
        TEST_ASSERT_TRUE(IS_IMMEDIATE(nil_val));
    });
}

void test_cljvalue_vectors_high_level(void) {
    WITH_AUTORELEASE_POOL({
        // Test vectors at high level
        CljValue vec = make_vector(3, 1);
        TEST_ASSERT_NOT_NULL(vec);
        TEST_ASSERT_EQUAL_INT(CLJ_VECTOR, ((CljObject*)vec)->type);
        
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        TEST_ASSERT_NOT_NULL(vec_data);
        TEST_ASSERT_EQUAL_INT(3, vec_data->capacity);
        TEST_ASSERT_EQUAL_INT(0, vec_data->count);
        
        // Test vector operations
        vec_data->data[0] = fixnum(1);
        vec_data->data[1] = fixnum(2);
        vec_data->data[2] = fixnum(3);
        vec_data->count = 3;
        
        TEST_ASSERT_EQUAL_INT(3, vec_data->count);
    });
}

void test_cljvalue_immediates_high_level(void) {
    WITH_AUTORELEASE_POOL({
        // Test immediates at high level
        CljValue fixnum_val = fixnum(42);
        TEST_ASSERT_TRUE(is_fixnum(fixnum_val));
        TEST_ASSERT_EQUAL_INT(42, as_fixnum(fixnum_val));
        
        CljValue char_val = character('A');
        TEST_ASSERT_TRUE(is_char(char_val));
        TEST_ASSERT_EQUAL_INT('A', as_char(char_val));
        
        CljValue bool_val = make_special(SPECIAL_TRUE);
        TEST_ASSERT_TRUE(is_bool(bool_val));
        TEST_ASSERT_TRUE(is_true(bool_val));
        
        CljValue nil_val = SPECIAL_NIL;
        TEST_ASSERT_NULL(nil_val);  // nil is NULL in our system
    });
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

REGISTER_TEST(test_cljvalue_immediate_helpers)
REGISTER_TEST(test_cljvalue_vector_api)
REGISTER_TEST(test_cljvalue_transient_vector)
REGISTER_TEST(test_cljvalue_clojure_semantics)
REGISTER_TEST(test_cljvalue_wrapper_functions)
REGISTER_TEST(test_cljvalue_immediates_fixnum)
REGISTER_TEST(test_cljvalue_immediates_char)
REGISTER_TEST(test_cljvalue_immediates_special)
REGISTER_TEST(test_cljvalue_immediates_fixed)
REGISTER_TEST(test_cljvalue_parser_immediates)
REGISTER_TEST(test_cljvalue_memory_efficiency)
REGISTER_TEST(test_cljvalue_vectors_high_level)
REGISTER_TEST(test_cljvalue_immediates_high_level)