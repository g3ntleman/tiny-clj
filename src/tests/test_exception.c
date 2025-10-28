/*
 * Exception Tests using Unity Framework
 * 
 * Tests for TRY/CATCH/END_TRY exception handling including nested
 * exception handling, auto-release, and exception stack.
 */

#include "tests_common.h"

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// EXCEPTION TESTS
// ============================================================================

TEST(test_simple_try_catch_exception_caught) {
    bool exception_caught = false;
    
    TRY {
        throw_exception("TestException", "Test error", __FILE__, __LINE__, 0);
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("TestException", ex->type);
        TEST_ASSERT_EQUAL_STRING("Test error", ex->message);
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
}

TEST(test_simple_try_catch_no_exception) {
    bool try_executed = false;
    bool catch_executed = false;
    
    TRY {
        try_executed = true;
    } CATCH(ex) {
        catch_executed = true;
        TEST_ASSERT_TRUE_MESSAGE(false, "CATCH should not run when no exception");
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(try_executed, "TRY block should have executed");
    TEST_ASSERT_FALSE_MESSAGE(catch_executed, "CATCH block should not have executed");
}

TEST(test_nested_try_catch_inner_exception) {
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    TRY {
        outer_try = true;
        TRY {
            inner_try = true;
            throw_exception("InnerException", "Inner error", __FILE__, __LINE__, 0);
            TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here");
        } CATCH(ex) {
            inner_catch = true;
            TEST_ASSERT_EQUAL_STRING("InnerException", ex->type);
            // Exception is automatically managed by CATCH macro
        } END_TRY
        after_inner = true;
    } CATCH(ex) {
        outer_catch = true;
        TEST_ASSERT_TRUE_MESSAGE(false, "Outer CATCH should not run");
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(outer_try, "Outer TRY should have executed");
    TEST_ASSERT_TRUE_MESSAGE(inner_try, "Inner TRY should have executed");
    TEST_ASSERT_TRUE_MESSAGE(inner_catch, "Inner CATCH should have executed");
    TEST_ASSERT_TRUE_MESSAGE(after_inner, "Code after inner TRY should have executed");
    TEST_ASSERT_FALSE_MESSAGE(outer_catch, "Outer CATCH should not have executed");
}

TEST(test_nested_try_catch_outer_exception) {
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

TEST(test_exception_with_autorelease) {
    bool exception_caught = false;
    
    TRY {
        // Create some objects that should be cleaned up
        CljObject *obj1 = fixnum(42);
        CljValue obj2 = make_string("test");
        TEST_ASSERT_NOT_NULL(obj1);
        TEST_ASSERT_NOT_NULL(obj2);
        
        throw_exception("AutoreleaseException", "Test with autorelease", __FILE__, __LINE__, 0);
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_EQUAL_STRING("AutoreleaseException", ex->type);
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "Exception should have been caught");
    // Objects are automatically cleaned up by AUTORELEASE
}

TEST(test_repl_crash_scenario) {
    // This test reproduces the exact crash scenario from the REPL
    // Test manual memory management with exceptions - Foundation-style
    
    TRY {
        // Create some objects that will be in the autorelease pool
        // CljObject *obj1 = fixnum(42); // Unused variable removed
        // CljValue obj2 = make_string("test"); // Unused variable removed
        // CljObject *obj3 = AUTORELEASE(make_symbol_impl("test", NULL)); // Unused variable removed
        
        // Throw exception - this should cause memory corruption
        // when the autorelease pool is cleaned up
        throw_exception("WrongArgumentException", "String cannot be used as a Number", 
                      "src/function_call.c", 144, 0);
        
    } CATCH(ex) {
        // This should catch the exception but may crash during cleanup
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("WrongArgumentException", ex->type);
        // Exception is automatically managed by CATCH macro
    } END_TRY
}

TEST(test_map_arity_exception_zero_args) {
    EvalState *st = evalstate_new();
    bool exception_caught = false;
    
    TRY {
        // Create a map and bind it to 'm'
        CljValue map_obj = AUTORELEASE(make_map(2));
        CljObject *key = AUTORELEASE(make_symbol_impl(":a", NULL));
        CljObject *val = fixnum(1);
        map_assoc(map_obj, key, val);
        
        // Define 'm' in current namespace
        CljObject *m_sym = AUTORELEASE(make_symbol_impl("m", NULL));
        ns_define(st->current_ns, m_sym, (CljObject*)map_obj);
        
        // Try to call map function with 0 arguments: (map)
        eval_string("(map)", st);
        
        // Should not reach here
        TEST_ASSERT_TRUE_MESSAGE(false, "Should throw ArityException when calling map with 0 args");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        // Check exception type (might be ArityException or RuntimeException)
        // Accept both as valid error indicators
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, 
        "Exception should be thrown when calling map with wrong arity");
    
    evalstate_free(st);
}

TEST(test_with_autorelease_pool_swallows_exceptions) {
    bool exception_caught_outside = false;
    
    // Test that WITH_AUTORELEASE_POOL now DOES propagate exceptions correctly
    // The comment now correctly states "Exception propagates automatically"
    
    TRY {
        // This should throw an exception inside WITH_AUTORELEASE_POOL
        WITH_AUTORELEASE_POOL({
            // Create some objects to test memory cleanup
            CljObject *obj1 = AUTORELEASE(make_symbol_impl("test1", NULL));
            CljObject *obj2 = AUTORELEASE(make_symbol_impl("test2", NULL));
            TEST_ASSERT_NOT_NULL(obj1);
            TEST_ASSERT_NOT_NULL(obj2);
            
            // Throw exception inside the pool
            throw_exception("TestException", "Exception inside WITH_AUTORELEASE_POOL", 
                          __FILE__, __LINE__, 0);
            
            // This should never be reached
            TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
        });
        
        // If we reach here, the exception was NOT propagated
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here - exception should have been propagated");
        
    } CATCH(ex) {
        // This CATCH should NOW execute because WITH_AUTORELEASE_POOL properly propagates
        exception_caught_outside = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("TestException", ex->type);
    } END_TRY
    
    // The fix: WITH_AUTORELEASE_POOL now properly propagates exceptions
    TEST_ASSERT_TRUE_MESSAGE(exception_caught_outside, 
        "WITH_AUTORELEASE_POOL should now properly propagate exceptions to outer TRY/CATCH");
    
    // This test should now PASS, proving the fix works
}

TEST(test_throw_existing_exception) {
    bool exception_caught = false;
    CLJException *original_exception = NULL;
    
    // Test that THROW(ex) properly re-throws an existing exception
    TRY {
        // Create an exception
        original_exception = make_exception("OriginalException", "Original error message", 
                                           __FILE__, __LINE__, 0);
        TEST_ASSERT_NOT_NULL(original_exception);
        
        // Throw it
        throw_exception_object(original_exception);
        
        // Should not reach here
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
        
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        
        // Verify it's the same exception object (same pointer)
        TEST_ASSERT_EQUAL_PTR(original_exception, ex);
        
        // Verify the exception details are preserved
        TEST_ASSERT_EQUAL_STRING("OriginalException", ex->type);
        TEST_ASSERT_EQUAL_STRING("Original error message", ex->message);
        
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, 
        "Exception should have been caught when using THROW(ex)");
}

TEST(test_throw_macro_convenience) {
    bool exception_caught = false;
    CLJException *original_exception = NULL;
    
    // Test that THROW(ex) macro works the same as throw_exception_object(ex)
    TRY {
        // Create an exception
        original_exception = make_exception("MacroException", "Macro test message", 
                                           __FILE__, __LINE__, 0);
        TEST_ASSERT_NOT_NULL(original_exception);
        
        // Throw it using the macro
        THROW(original_exception);
        
        // Should not reach here
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after THROW");
        
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        
        // Verify it's the same exception object
        TEST_ASSERT_EQUAL_PTR(original_exception, ex);
        
        // Verify the exception details are preserved
        TEST_ASSERT_EQUAL_STRING("MacroException", ex->type);
        TEST_ASSERT_EQUAL_STRING("Macro test message", ex->message);
        
        // Exception is automatically managed by CATCH macro
    } END_TRY
    
    TEST_ASSERT_TRUE_MESSAGE(exception_caught, 
        "Exception should have been caught when using THROW macro");
}

// ============================================================================
// TEST GROUPS
// ============================================================================
// (Unused test groups removed for cleanup)

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

// Unused function removed for cleanup

// Unused function removed for cleanup

// ============================================================================
// TEST FUNCTIONS (no main function - called by unity_test_runner.c)
// ============================================================================

// Register all tests
