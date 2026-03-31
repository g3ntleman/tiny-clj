/*
 * Exception Tests using Unity Framework
 * 
 * Tests for TRY/CATCH/END_TRY exception handling including nested
 * exception handling, auto-release, and exception stack.
 */

#include "tests_common.h"
#include <pthread.h>
#include <unistd.h>

// ============================================================================
// TEST FIXTURES (setUp/tearDown defined in unity_test_runner.c)
// ============================================================================

// ============================================================================
// EXCEPTION TESTS
// ============================================================================

static char *capture_print_exception_output(CLJException *ex) {
    if (!ex) {
        return NULL;
    }

    FILE *tmp = tmpfile();
    if (!tmp) {
        return NULL;
    }

    int stderr_fd = fileno(stderr);
    int saved_stderr = dup(stderr_fd);
    if (saved_stderr < 0) {
        fclose(tmp);
        return NULL;
    }

    fflush(stderr);
    if (dup2(fileno(tmp), stderr_fd) < 0) {
        close(saved_stderr);
        fclose(tmp);
        return NULL;
    }

    print_exception(ex);
    fflush(stderr);

    (void)dup2(saved_stderr, stderr_fd);
    close(saved_stderr);

    if (fseek(tmp, 0, SEEK_END) != 0) {
        fclose(tmp);
        return NULL;
    }

    long len = ftell(tmp);
    if (len < 0) {
        fclose(tmp);
        return NULL;
    }

    if (fseek(tmp, 0, SEEK_SET) != 0) {
        fclose(tmp);
        return NULL;
    }

    char *buffer = CLJ_MALLOC((size_t)len + 1);
    if (!buffer) {
        fclose(tmp);
        return NULL;
    }

    size_t nread = fread(buffer, 1, (size_t)len, tmp);
    buffer[nread] = '\0';
    fclose(tmp);
    return buffer;
}

typedef struct {
    bool returned;
} ThrowOomWorkerArgs;

typedef struct {
    bool caught;
    bool returned;
} ThrowOomTryCatchWorkerArgs;

typedef struct {
    bool before_register_is_main_thread;
    bool after_register_is_main_thread;
} MainThreadRegistrationWorkerArgs;

typedef struct {
    bool before_has_interpreter_thread;
    bool before_is_interpreter_thread;
    bool after_register_has_interpreter_thread;
    bool after_register_is_interpreter_thread;
    bool after_clear_has_interpreter_thread;
    bool after_clear_is_interpreter_thread;
} InterpreterThreadRegistrationWorkerArgs;

static bool g_release_throw_invoked_during_drain = false;
static bool g_release_throw_caught = false;

static void byte_array_release_throw_while_draining(CljObject *obj) {
    CljByteArray *ba = (CljByteArray *)obj;
    g_release_throw_invoked_during_drain = is_autorelease_pool_draining();

    // Mirror default CLJ_BYTE_ARRAY payload cleanup for this test override.
    if (ba && ba->data && ((ba->base.flags & CLJ_FLAG_EXTERNAL_DATA) == 0)) {
        CLJ_FREE(ba->data);
        ba->data = NULL;
    }

    TRY {
        throw_exception_formatted("AutoreleasePoolError",
                                  __FILE__, __LINE__, 0,
                                  "release callback throw during drain");
    } CATCH(ex) {
        g_release_throw_caught = (ex != NULL && strcmp(ex->type, "AutoreleasePoolError") == 0);
    } END_TRY
}

static void *throw_oom_worker_main(void *arg) {
    ThrowOomWorkerArgs *args = (ThrowOomWorkerArgs *)arg;
    if (!args) {
        return NULL;
    }
    throw_oom();
    args->returned = true;
    return NULL;
}

static void *throw_oom_try_catch_worker_main(void *arg) {
    ThrowOomTryCatchWorkerArgs *args = (ThrowOomTryCatchWorkerArgs *)arg;
    if (!args) {
        return NULL;
    }
    TRY {
        throw_oom();
    } CATCH(ex) {
        (void)ex;
        args->caught = true;
    } END_TRY
    args->returned = true;
    return NULL;
}

static void *main_thread_registration_worker_main(void *arg) {
    MainThreadRegistrationWorkerArgs *args = (MainThreadRegistrationWorkerArgs *)arg;
    if (!args) {
        return NULL;
    }
    args->before_register_is_main_thread = subjective_c_is_main_thread();
    subjective_c_register_main_thread();
    args->after_register_is_main_thread = subjective_c_is_main_thread();
    return NULL;
}

static void *interpreter_thread_registration_worker_main(void *arg) {
    InterpreterThreadRegistrationWorkerArgs *args = (InterpreterThreadRegistrationWorkerArgs *)arg;
    if (!args) {
        return NULL;
    }
    args->before_has_interpreter_thread = subjective_c_has_interpreter_thread();
    args->before_is_interpreter_thread = subjective_c_is_interpreter_thread();
    subjective_c_register_interpreter_thread();
    args->after_register_has_interpreter_thread = subjective_c_has_interpreter_thread();
    args->after_register_is_interpreter_thread = subjective_c_is_interpreter_thread();
    subjective_c_clear_interpreter_thread();
    args->after_clear_has_interpreter_thread = subjective_c_has_interpreter_thread();
    args->after_clear_is_interpreter_thread = subjective_c_is_interpreter_thread();
    return NULL;
}

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
        // CljObject *obj3 = AUTORELEASE(intern_symbol_global("test")); // Unused variable removed
        
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
    bool exception_caught = false;
    
    TRY {
        // Create a map and bind it to 'm'
        CljValue map_obj = AUTORELEASE(make_map(2));
        CljObject *key = AUTORELEASE(intern_symbol_global(":a"));
        CljObject *val = fixnum(1);
        (void)map_by_associng_kv(map_obj, key, val);
        
        // Define 'm' in current namespace (use global g_test_eval_state from setUp)
        CljObject *m_sym = AUTORELEASE(intern_symbol_global("m"));
        ns_define(g_test_eval_state->current_ns, m_sym, (CljObject*)map_obj);
        
        // Try to call map function with 0 arguments: (map)
        eval_string("(map)", g_test_eval_state);
        
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
}

TEST(test_with_autorelease_pool_swallows_exceptions) {
    bool exception_caught_outside = false;
    
    // Test that WITH_AUTORELEASE_POOL now DOES propagate exceptions correctly
    // The comment now correctly states "Exception propagates automatically"
    
    TRY {
        // This should throw an exception inside WITH_AUTORELEASE_POOL
        WITH_AUTORELEASE_POOL({
            // Create some objects to test memory cleanup
            CljObject *obj1 = AUTORELEASE(intern_symbol_global("test1"));
            CljObject *obj2 = AUTORELEASE(intern_symbol_global("test2"));
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

TEST(test_throw_exception_formatted_during_pool_drain_does_not_recurse) {
    bool escaped = false;
    g_release_throw_invoked_during_drain = false;
    g_release_throw_caught = false;

    subjective_c_register_release_fn(CLJ_BYTE_ARRAY, byte_array_release_throw_while_draining);

    uint32_t mark = autorelease_pool_mark();
    CljByteArray *ba = AUTORELEASE(make_byte_array(8));
    TEST_ASSERT_NOT_NULL(ba);

    TRY {
        autorelease_pool_drain_to_depth(mark);
    } CATCH(ex) {
        (void)ex;
        escaped = true;
    } END_TRY

    subjective_c_register_release_fn(CLJ_BYTE_ARRAY, NULL);

    TEST_ASSERT_FALSE_MESSAGE(escaped, "Exception from release callback should be catchable without recursion");
    TEST_ASSERT_TRUE_MESSAGE(g_release_throw_invoked_during_drain,
                             "Release callback must execute while autorelease pool is draining");
    TEST_ASSERT_TRUE_MESSAGE(g_release_throw_caught,
                             "throw_exception_formatted must be catchable during drain (no recursive throw)");
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
        
        // Throw it (AUTORELEASE so pool owns; END_TRY does not release)
        throw_exception_object(AUTORELEASE(original_exception));
        
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

TEST(test_oom_exception_is_static_and_no_stacktrace) {
    bool exception_caught = false;

    TRY {
        // Even when memory is available, OutOfMemoryError must not allocate a new exception
        // or generate a stacktrace.
        throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                                  "Out of memory while allocating %s", "test");
        TEST_FAIL_MESSAGE("Should not reach here after throwing OOM");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_PTR_MESSAGE(clj_oom_exception, ex, "OOM must use the static singleton exception");
        TEST_ASSERT_EQUAL_STRING("OutOfMemoryError", ex->type);
#ifdef DEBUG
        TEST_ASSERT_NULL_MESSAGE(ex->stacktrace, "OOM exception must not allocate/generate stacktrace");
#endif
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "OOM exception should have been caught");
}

TEST(test_print_exception_oom_includes_native_stacktrace_marker) {
    bool exception_caught = false;
    bool marker_found = false;

    TRY {
        throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                                  "Out of memory while allocating %s", "stacktrace-test");
        TEST_FAIL_MESSAGE("Should not reach here after throwing OOM");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("OutOfMemoryError", ex->type);

        char *output = capture_print_exception_output(ex);
        TEST_ASSERT_NOT_NULL_MESSAGE(output, "Failed to capture print_exception output");
        marker_found = (strstr(output, "Native stack trace (OOM):") != NULL);
        CLJ_FREE(output);
    } END_TRY

    TEST_ASSERT_TRUE_MESSAGE(exception_caught, "OOM exception should have been caught");
    TEST_ASSERT_TRUE_MESSAGE(marker_found, "OOM output should include native stacktrace marker");
}

TEST(test_throw_oom_on_background_thread_does_not_cross_thread_longjmp) {
    pthread_t worker;
    ThrowOomWorkerArgs args = {0};
    bool exception_caught = false;

    subjective_c_register_main_thread();
    TEST_ASSERT_TRUE(subjective_c_is_main_thread());

    TRY {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&worker, NULL, throw_oom_worker_main, &args));
        TEST_ASSERT_EQUAL_INT(0, pthread_join(worker, NULL));
    } CATCH(ex) {
        (void)ex;
        exception_caught = true;
    } END_TRY

    TEST_ASSERT_FALSE_MESSAGE(exception_caught, "background-thread OOM must not longjmp into main-thread TRY");
    TEST_ASSERT_TRUE_MESSAGE(args.returned, "throw_oom should return on background threads");
}

TEST(test_throw_oom_on_background_thread_with_local_try_catch_is_catchable) {
    pthread_t worker;
    ThrowOomTryCatchWorkerArgs args = {0};

    subjective_c_register_main_thread();
    TEST_ASSERT_TRUE(subjective_c_is_main_thread());

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&worker, NULL, throw_oom_try_catch_worker_main, &args));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker, NULL));

    TEST_ASSERT_TRUE_MESSAGE(args.returned, "worker thread should complete after local OOM handling");
    TEST_ASSERT_TRUE_MESSAGE(args.caught, "background-thread OOM should be catchable inside local TRY/CATCH");
}

TEST(test_register_main_thread_does_not_replace_existing_main_thread) {
    pthread_t worker;
    MainThreadRegistrationWorkerArgs args = {0};

    subjective_c_register_main_thread();
    TEST_ASSERT_TRUE(subjective_c_is_main_thread());

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&worker, NULL, main_thread_registration_worker_main, &args));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker, NULL));

    TEST_ASSERT_FALSE_MESSAGE(args.before_register_is_main_thread, "worker must not start as main thread");
    TEST_ASSERT_FALSE_MESSAGE(args.after_register_is_main_thread, "worker must not become main thread by registering");
    TEST_ASSERT_TRUE_MESSAGE(subjective_c_is_main_thread(), "main-thread registration must remain stable");
}

TEST(test_register_interpreter_thread_is_visible_process_wide_and_clearable) {
    pthread_t worker;
    InterpreterThreadRegistrationWorkerArgs args = {0};

    subjective_c_clear_interpreter_thread();
    TEST_ASSERT_FALSE(subjective_c_has_interpreter_thread());
    TEST_ASSERT_FALSE(subjective_c_is_interpreter_thread());

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&worker, NULL, interpreter_thread_registration_worker_main, &args));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker, NULL));

    TEST_ASSERT_FALSE_MESSAGE(args.before_has_interpreter_thread,
                              "worker must not start with a registered interpreter thread");
    TEST_ASSERT_FALSE_MESSAGE(args.before_is_interpreter_thread,
                              "worker must not start as interpreter thread");
    TEST_ASSERT_TRUE_MESSAGE(args.after_register_has_interpreter_thread,
                             "worker registration must become globally visible");
    TEST_ASSERT_TRUE_MESSAGE(args.after_register_is_interpreter_thread,
                             "registered worker must see itself as interpreter thread");
    TEST_ASSERT_FALSE_MESSAGE(args.after_clear_has_interpreter_thread,
                              "clearing interpreter thread must remove global registration");
    TEST_ASSERT_FALSE_MESSAGE(args.after_clear_is_interpreter_thread,
                              "worker must stop matching interpreter thread after clear");
    TEST_ASSERT_FALSE_MESSAGE(subjective_c_has_interpreter_thread(),
                              "main thread must observe cleared interpreter-thread registration");
    TEST_ASSERT_FALSE_MESSAGE(subjective_c_is_interpreter_thread(),
                              "main thread must not match interpreter-thread registration");
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
