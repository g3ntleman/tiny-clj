/*
 * Simple Assertion Functions Test
 * 
 * Tests the Clojure Core API assertion functions for Tiny-Clj
 */

#include "../unity.h"
#include "../tiny_clj.h"
#include "../CljObject.h"
#include "../clj_symbols.h"
#include "../exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test setup and teardown (disabled in unit test runner)
#ifndef UNIT_TEST_RUNNER
void setUp(void) {
    // Initialize symbol table
    init_special_symbols();
    
    // Initialize meta registry
    meta_registry_init();
}

void tearDown(void) {
    // Cleanup symbol table
    symbol_table_cleanup();
    
    // Cleanup autorelease pool
    cljvalue_pool_cleanup_all();
}
#endif

// ============================================================================
// ASSERTION TESTS
// ============================================================================

void test_clj_assert_success(void) {
    printf("\n=== Testing clj_assert Success ===\n");
    
    // This should not throw an exception
    clj_assert(true, "This should not fail");
    
    // Test passes if we reach here
    TEST_ASSERT_TRUE(true);
}

void test_clj_assert_args_success(void) {
    printf("\n=== Testing clj_assert_args Success ===\n");
    
    // This should not throw an exception
    clj_assert_args("test_function", true, "Valid parameter");
    
    // Test passes if we reach here
    TEST_ASSERT_TRUE(true);
}

void test_clj_assert_args_multiple_success(void) {
    printf("\n=== Testing clj_assert_args_multiple Success ===\n");
    
    // This should not throw an exception
    clj_assert_args_multiple("test_function", 3,
        true, "First condition",
        true, "Second condition", 
        true, "Third condition");
    
    // Test passes if we reach here
    TEST_ASSERT_TRUE(true);
}

void test_assertion_functions_exist(void) {
    printf("\n=== Testing Assertion Functions Exist ===\n");
    
    // Test that assertion functions are callable
    // (This will not throw because conditions are true)
    clj_assert(true, "Test message");
    clj_assert_with_location(true, "Test message", "test.clj", 1, 1);
    clj_assert_args("test", true, "Test message");
    clj_assert_args_multiple("test", 1, true, "Test message");
    
    // Test passes if we reach here
    TEST_ASSERT_TRUE(true);
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

// moved to test runner (test_unit_main.c)
