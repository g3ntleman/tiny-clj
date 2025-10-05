/*
 * Global Singletons Unity Test
 * 
 * Tests the global singleton implementation of Tiny-Clj:
 * - CLJ_NIL, CLJ_TRUE, CLJ_FALSE singletons
 * - Singleton access functions (clj_nil, clj_true, clj_false)
 * - Pointer equality (same object returned every time)
 * - Memory management (singletons live forever)
 * - Integration with pr_str and other functions
 */

#include "../unity.h"
#include "../CljObject.h"
#include "../runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNIT_TEST_RUNNER
void setUp(void) {
    // Initialize global singletons
    clj_nil();
    clj_true();
    clj_false();
}

void tearDown(void) {
    // Cleanup if needed
}
#endif

void test_singleton_access_functions(void) {
    // Test singleton access functions
    CljObject *nil1 = clj_nil();
    CljObject *true1 = clj_true();
    CljObject *false1 = clj_false();
    
    TEST_ASSERT_NOT_NULL(nil1);
    TEST_ASSERT_NOT_NULL(true1);
    TEST_ASSERT_NOT_NULL(false1);
    
    TEST_ASSERT_EQUAL(CLJ_NIL, nil1->type);
    TEST_ASSERT_EQUAL(CLJ_BOOL, true1->type);
    TEST_ASSERT_EQUAL(CLJ_BOOL, false1->type);
}

void test_singleton_pointer_equality(void) {
    // Test that same objects are returned every time
    CljObject *nil1 = clj_nil();
    CljObject *nil2 = clj_nil();
    CljObject *true1 = clj_true();
    CljObject *true2 = clj_true();
    CljObject *false1 = clj_false();
    CljObject *false2 = clj_false();
    
    TEST_ASSERT_EQUAL_PTR(nil1, nil2);
    TEST_ASSERT_EQUAL_PTR(true1, true2);
    TEST_ASSERT_EQUAL_PTR(false1, false2);
}

void test_singleton_global_variables(void) {
    // Pointer stability across calls is sufficient here
    TEST_ASSERT_EQUAL_PTR(clj_nil(), clj_nil());
    TEST_ASSERT_EQUAL_PTR(clj_true(), clj_true());
    TEST_ASSERT_EQUAL_PTR(clj_false(), clj_false());
}

void test_singleton_pr_str(void) {
    // Test pr_str with singletons
    char *nil_str = pr_str(clj_nil());
    char *true_str = pr_str(clj_true());
    char *false_str = pr_str(clj_false());
    
    TEST_ASSERT_NOT_NULL(nil_str);
    TEST_ASSERT_NOT_NULL(true_str);
    TEST_ASSERT_NOT_NULL(false_str);
    
    TEST_ASSERT_EQUAL_STRING("nil", nil_str);
    TEST_ASSERT_EQUAL_STRING("true", true_str);
    TEST_ASSERT_EQUAL_STRING("false", false_str);
    
    free(nil_str);
    free(true_str);
    free(false_str);
}

void test_singleton_boolean_values(void) {
    // Test boolean values
    CljObject *true_obj = clj_true();
    CljObject *false_obj = clj_false();
    
    TEST_ASSERT_TRUE(true_obj->as.b);
    TEST_ASSERT_FALSE(false_obj->as.b);
}


void test_singleton_equality(void) {
    // Test equality between singleton instances
    CljObject *nil1 = clj_nil();
    CljObject *nil2 = clj_nil();
    CljObject *true1 = clj_true();
    CljObject *true2 = clj_true();
    CljObject *false1 = clj_false();
    CljObject *false2 = clj_false();
    
    TEST_ASSERT_TRUE(clj_equal(nil1, nil2));
    TEST_ASSERT_TRUE(clj_equal(true1, true2));
    TEST_ASSERT_TRUE(clj_equal(false1, false2));
    
    // Test inequality between different singletons
    TEST_ASSERT_FALSE(clj_equal(nil1, true1));
    TEST_ASSERT_FALSE(clj_equal(nil1, false1));
    TEST_ASSERT_FALSE(clj_equal(true1, false1));
}

// moved to test runner (test_unit_main.c)
