#include "../unity.h"
#include "../tiny_clj.h"
#include "test_helpers.h"
#include "../CljObject.h"
#include "../clj_symbols.h"
#include <stdlib.h>

void setUp(void) {
    // Setup before each test
    init_special_symbols();
    meta_registry_init();
}

void tearDown(void) {
    // Cleanup after each test
    meta_registry_cleanup();
    cljvalue_pool_cleanup_all();
}

void test_singleton_creation(void) {
    // Test singleton creation and pointer equality
    CljObject *nil1 = clj_nil();
    CljObject *nil2 = clj_nil();
    CljObject *true1 = clj_true();
    CljObject *true2 = clj_true();
    CljObject *false1 = clj_false();
    CljObject *false2 = clj_false();
    
    ASSERT_TYPE(nil1, CLJ_NIL);
    ASSERT_TYPE(true1, CLJ_BOOL);
    ASSERT_TYPE(false1, CLJ_BOOL);
    
    // Singletons should be identical (same pointer)
    TEST_ASSERT_EQUAL_PTR(nil1, nil2);
    TEST_ASSERT_EQUAL_PTR(true1, true2);
    TEST_ASSERT_EQUAL_PTR(false1, false2);
    
    // Test boolean values
    ASSERT_OBJ_BOOL_EQ(true1, 1);
    ASSERT_OBJ_BOOL_EQ(false1, 0);
}

void test_int_creation(void) {
    CljObject *int_obj = autorelease(make_int(42));
    
    ASSERT_OBJ_INT_EQ(int_obj, 42);
}

void test_string_creation(void) {
    CljObject *str_obj = autorelease(make_string("hello world"));
    
    ASSERT_TYPE(str_obj, CLJ_STRING);
    ASSERT_OBJ_CSTR_EQ(str_obj, "hello world");
}

void test_clj_equal_primitives(void) {
    // Test integer equality
    CljObject *int1 = autorelease(make_int(42));
    CljObject *int2 = autorelease(make_int(42));
    CljObject *int3 = autorelease(make_int(43));
    
    TEST_ASSERT_TRUE(clj_equal(int1, int2));
    TEST_ASSERT_FALSE(clj_equal(int1, int3));
    
    // Test string equality
    CljObject *str1 = autorelease(make_string("hello"));
    CljObject *str2 = autorelease(make_string("hello"));
    CljObject *str3 = autorelease(make_string("world"));
    
    TEST_ASSERT_TRUE(clj_equal(str1, str2));
    TEST_ASSERT_FALSE(clj_equal(str1, str3));
    
    // Test singleton equality
    TEST_ASSERT_TRUE(clj_equal(clj_nil(), clj_nil()));
    TEST_ASSERT_TRUE(clj_equal(clj_true(), clj_true()));
    TEST_ASSERT_TRUE(clj_equal(clj_false(), clj_false()));
    
    // Test different types
    TEST_ASSERT_FALSE(clj_equal(int1, str1));
    TEST_ASSERT_FALSE(clj_equal(clj_nil(), clj_true()));
}

void test_pr_str_functionality(void) {
    // Test string representation of different types
    char *nil_str = pr_str(clj_nil());
    TEST_ASSERT_EQUAL_STRING("nil", nil_str);
    free(nil_str);
    
    char *true_str = pr_str(clj_true());
    TEST_ASSERT_EQUAL_STRING("true", true_str);
    free(true_str);
    
    char *false_str = pr_str(clj_false());
    TEST_ASSERT_EQUAL_STRING("false", false_str);
    free(false_str);
    
    CljObject *int_obj = autorelease(make_int(42));
    char *int_str = pr_str(int_obj);
    TEST_ASSERT_EQUAL_STRING("42", int_str);
    free(int_str);
    
    CljObject *str_obj = autorelease(make_string("hello"));
    char *str_str = pr_str(str_obj);
    TEST_ASSERT_EQUAL_STRING("\"hello\"", str_str);
    free(str_str);
}

void test_memory_management(void) {
    // Test that objects are properly cleaned up
    CljObject *obj1 = autorelease(make_int(1));
    CljObject *obj2 = autorelease(make_int(2));
    CljObject *obj3 = autorelease(make_string("test"));
    
    TEST_ASSERT_NOT_NULL(obj1);
    TEST_ASSERT_NOT_NULL(obj2);
    TEST_ASSERT_NOT_NULL(obj3);
    
    // Cleanup should work without errors
    cljvalue_pool_cleanup_all();
}

// moved to test runner (test_unit_main.c)
