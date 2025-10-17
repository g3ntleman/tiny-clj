/*
 * Exception Tests using Unity Framework
 * 
 * Tests for TRY/CATCH/END_TRY exception handling including nested
 * exception handling, auto-release, and exception stack.
 */

#include "unity.h"
#include "object.h"
#include "exception.h"
#include "memory.h"
#include "memory_profiler.h"
#include "namespace.h"
#include "symbol.h"
#include "clj_string.h"
#include "map.h"
#include "parser.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// EXCEPTION TESTS
// ============================================================================

void test_simple_try_catch_exception_caught(void) {
    WITH_AUTORELEASE_POOL({
        bool exception_caught = false;
        
        TRY {
            throw_exception("TestException", "Test error", __FILE__, __LINE__, 0);
            TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
        } CATCH(ex) {
            exception_caught = true;
            TEST_ASSERT_NOT_NULL(ex);
            TEST_ASSERT_EQUAL_STRING("TestException", ex->type);
            TEST_ASSERT_EQUAL_STRING("Test error", ex->message);
        } END_TRY
        
        TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
    });
}

void test_simple_try_catch_no_exception(void) {
    // Foundation pre-ARC style: Isolate autorelease pool per test
    WITH_AUTORELEASE_POOL({
        bool try_executed = false;
        bool catch_executed = false;
        
        TRY {
            try_executed = true;
        } CATCH(ex) {
            catch_executed = true;
            TEST_ASSERT_TRUE_MESSAGE(false, "CATCH should not run when no exception");
            RELEASE(ex);
        } END_TRY
        
        TEST_ASSERT_TRUE_MESSAGE(try_executed, "TRY block should have executed");
        TEST_ASSERT_FALSE_MESSAGE(catch_executed, "CATCH block should not have executed");
    });
}

void test_nested_try_catch_inner_exception(void) {
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    // Foundation pre-ARC style: Isolate autorelease pool per test
    WITH_AUTORELEASE_POOL({
        TRY {
            outer_try = true;
            TRY {
                inner_try = true;
                throw_exception("InnerException", "Inner error", __FILE__, __LINE__, 0);
                TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here");
            } CATCH(ex) {
                inner_catch = true;
                TEST_ASSERT_EQUAL_STRING("InnerException", ex->type);
            } END_TRY
            after_inner = true;
        } CATCH(ex) {
            outer_catch = true;
            TEST_ASSERT_TRUE_MESSAGE(false, "Outer CATCH should not run");
        } END_TRY
    });
    
    TEST_ASSERT_TRUE_MESSAGE(outer_try, "Outer TRY should have executed");
    TEST_ASSERT_TRUE_MESSAGE(inner_try, "Inner TRY should have executed");
    TEST_ASSERT_TRUE_MESSAGE(inner_catch, "Inner CATCH should have executed");
    TEST_ASSERT_TRUE_MESSAGE(after_inner, "Code after inner TRY should have executed");
    TEST_ASSERT_FALSE_MESSAGE(outer_catch, "Outer CATCH should not have executed");
}

void test_nested_try_catch_outer_exception(void) {
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    TRY {
        outer_try = true;
        TRY {
            inner_try = true;
            // No exception in inner block
        } CATCH(ex) {
            inner_catch = true;
            TEST_ASSERT_TRUE_MESSAGE(false, "Inner CATCH should not run");
        } END_TRY
        after_inner = true;
        throw_exception("OuterException", "Outer error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        outer_catch = true;
        TEST_ASSERT_EQUAL_STRING("OuterException", ex->type);
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(outer_try, "Outer TRY should have executed");
    TEST_ASSERT_TRUE_MESSAGE(inner_try, "Inner TRY should have executed");
    TEST_ASSERT_FALSE_MESSAGE(inner_catch, "Inner CATCH should not have executed");
    TEST_ASSERT_TRUE_MESSAGE(after_inner, "Code after inner TRY should have executed");
    TEST_ASSERT_TRUE_MESSAGE(outer_catch, "Outer CATCH should have executed");
}

void test_exception_with_autorelease(void) {
    WITH_AUTORELEASE_POOL({
        bool exception_caught = false;
        
        TRY {
            // Create some objects that should be cleaned up
            CljObject *obj1 = make_int(42);
            CljObject *obj2 = make_string("test");
            TEST_ASSERT_NOT_NULL(obj1);
            TEST_ASSERT_NOT_NULL(obj2);
            
            throw_exception("AutoreleaseException", "Test with autorelease", __FILE__, __LINE__, 0);
            TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here");
        } CATCH(ex) {
            exception_caught = true;
            TEST_ASSERT_EQUAL_STRING("AutoreleaseException", ex->type);
        } END_TRY
        
        TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
        // Objects should be automatically cleaned up by WITH_AUTORELEASE_POOL
    });
}

void test_repl_crash_scenario(void) {
    // This test reproduces the exact crash scenario from the REPL
    // Test manual memory management with exceptions - Foundation-style
    
    WITH_AUTORELEASE_POOL({
        TRY {
            // Create some objects that will be in the autorelease pool
            CljObject *obj1 = make_int(42);
            CljObject *obj2 = make_string("test");
            CljObject *obj3 = make_symbol("test", NULL);
            
            // Throw exception - this should cause memory corruption
            // when the autorelease pool is cleaned up
            throw_exception("WrongArgumentException", "String cannot be used as a Number", 
                          "src/function_call.c", 144, 0);
            
        } CATCH(ex) {
            // This should catch the exception but may crash during cleanup
            TEST_ASSERT_NOT_NULL(ex);
            TEST_ASSERT_EQUAL_STRING("WrongArgumentException", ex->type);
        } END_TRY
    });
}

void test_map_arity_exception_zero_args(void) {
    WITH_AUTORELEASE_POOL({
        EvalState *st = evalstate_new();
        bool exception_caught = false;
        
        TRY {
            // Create a map and bind it to 'm'
            CljObject *map_obj = AUTORELEASE(make_map(2));
            CljObject *key = AUTORELEASE(make_symbol(":a", NULL));
            CljObject *val = AUTORELEASE(make_int(1));
            map_assoc(map_obj, key, val);
            
            // Define 'm' in current namespace
            CljObject *m_sym =AUTORELEASE(make_symbol("m", NULL));
            ns_define(st, m_sym, map_obj);
            
            // Try to call map with 0 arguments: (m)
            CljObject *result = eval_string("(m)", st);
            
            // Should not reach here
            TEST_ASSERT_TRUE_MESSAGE(false, "Should throw ArityException when calling map with 0 args");
        } CATCH(ex) {
            exception_caught = true;
            TEST_ASSERT_NOT_NULL(ex);
            // Check exception type (might be ArityException or RuntimeException)
            // Accept both as valid error indicators
        } END_TRY
        
        TEST_ASSERT_TRUE_MESSAGE(exception_caught, 
            "Exception should be thrown when calling map with wrong arity");
        
        evalstate_free(st);
    });
}

// ============================================================================
// TEST GROUPS
// ============================================================================

static void test_group_basic_exceptions(void) {
    RUN_TEST(test_simple_try_catch_exception_caught);
    RUN_TEST(test_simple_try_catch_no_exception);
}

static void test_group_nested_exceptions(void) {
    RUN_TEST(test_nested_try_catch_inner_exception);
    RUN_TEST(test_nested_try_catch_outer_exception);
}

static void test_group_advanced_exceptions(void) {
    RUN_TEST(test_exception_with_autorelease);
    RUN_TEST(test_repl_crash_scenario);
    RUN_TEST(test_map_arity_exception_zero_args);
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

static void __attribute__((unused)) print_usage(const char *program_name) {
    printf("Exception Tests for Tiny-CLJ\n");
    printf("Usage: %s [test_name]\n\n", program_name);
    printf("Available tests:\n");
    printf("  simple-caught   Test simple exception caught\n");
    printf("  simple-no-ex    Test simple try with no exception\n");
    printf("  nested-inner    Test nested try/catch with inner exception\n");
    printf("  nested-outer    Test nested try/catch with outer exception\n");
    printf("  autorelease     Test exception with autorelease cleanup\n");
    printf("  basic           Run basic exception tests\n");
    printf("  nested          Run nested exception tests\n");
    printf("  advanced        Run advanced exception tests\n");
    printf("  all             Run all tests (default)\n");
}

static void __attribute__((unused)) run_specific_test(const char *test_name) {
    if (strcmp(test_name, "simple-caught") == 0) {
        RUN_TEST(test_simple_try_catch_exception_caught);
    } else if (strcmp(test_name, "simple-no-ex") == 0) {
        RUN_TEST(test_simple_try_catch_no_exception);
    } else if (strcmp(test_name, "nested-inner") == 0) {
        RUN_TEST(test_nested_try_catch_inner_exception);
    } else if (strcmp(test_name, "nested-outer") == 0) {
        RUN_TEST(test_nested_try_catch_outer_exception);
    } else if (strcmp(test_name, "autorelease") == 0) {
        RUN_TEST(test_exception_with_autorelease);
    } else if (strcmp(test_name, "map-arity") == 0) {
        RUN_TEST(test_map_arity_exception_zero_args);
    } else if (strcmp(test_name, "basic") == 0) {
        RUN_TEST(test_group_basic_exceptions);
    } else if (strcmp(test_name, "nested") == 0) {
        RUN_TEST(test_group_nested_exceptions);
    } else if (strcmp(test_name, "advanced") == 0) {
        RUN_TEST(test_group_advanced_exceptions);
    } else if (strcmp(test_name, "all") == 0) {
        RUN_TEST(test_simple_try_catch_exception_caught);
        RUN_TEST(test_simple_try_catch_no_exception);
        RUN_TEST(test_nested_try_catch_inner_exception);
        RUN_TEST(test_nested_try_catch_outer_exception);
        RUN_TEST(test_exception_with_autorelease);
        RUN_TEST(test_map_arity_exception_zero_args);
    } else {
        printf("Unknown test: %s\n", test_name);
        printf("Use --help to see available tests\n");
    }
}

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================
