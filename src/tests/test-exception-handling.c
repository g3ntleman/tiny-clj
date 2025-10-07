/**
 * Test suite for TRY/CATCH/END_TRY exception handling
 * Tests nested exception handling, auto-release, and exception stack
 */

#include "minunit.h"
#include "namespace.h"
#include "exception.h"
#include "object.h"
#include "runtime.h"
#include "function_call.h"
#include "clj_symbols.h"
#include <string.h>

static EvalState *test_state = NULL;

static void test_setup(void) {
    test_state = evalstate();
    if (!test_state) {
        fprintf(stderr, "FATAL: evalstate() returned NULL\n");
        exit(1);
    }
    test_state->exception_stack = NULL;
    set_global_eval_state(test_state);
}

static void test_teardown(void) {
    set_global_eval_state(NULL);
    test_state = NULL;
}

// Test 1: Simple TRY/CATCH - Exception caught
static char *test_simple_try_catch_exception_caught(void) {
    test_setup();
    EvalState *st = test_state;
    bool exception_caught = false;
    
    TRY {
        throw_exception("TestException", "Test error", __FILE__, __LINE__, 0);
        mu_assert("Should not reach here after throw", 0);
    } CATCH(ex) {
        exception_caught = true;
        mu_assert("Exception should not be NULL", ex != NULL);
        mu_assert("Exception type should be TestException", strcmp(ex->type, "TestException") == 0);
        mu_assert("Exception message should be Test error", strcmp(ex->message, "Test error") == 0);
    } END_TRY
    
    mu_assert("Exception should have been caught", exception_caught);
    test_teardown();
    return 0;
}

// Test 2: Simple TRY/CATCH - No exception
static char *test_simple_try_catch_no_exception(void) {
    test_setup();
    EvalState *st = test_state;
    bool try_executed = false;
    bool catch_executed = false;
    
    TRY {
        try_executed = true;
    } CATCH(ex) {
        catch_executed = true;
        mu_assert("CATCH should not run when no exception", 0);
    } END_TRY
    
    mu_assert("TRY block should have executed", try_executed);
    mu_assert("CATCH block should not have executed", !catch_executed);
    test_teardown();
    return 0;
}

// Test 3: Nested TRY/CATCH - Inner exception caught
static char *test_nested_try_catch_inner_exception(void) {
    test_setup();
    EvalState *st = test_state;
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    TRY {
        outer_try = true;
        TRY {
            inner_try = true;
            throw_exception("InnerException", "Inner error", __FILE__, __LINE__, 0);
            mu_assert("Should not reach here", 0);
        } CATCH(ex) {
            inner_catch = true;
            mu_assert("Exception type should be InnerException", strcmp(ex->type, "InnerException") == 0);
        } END_TRY
        after_inner = true;
    } CATCH(ex) {
        outer_catch = true;
        mu_assert("Outer CATCH should not run", 0);
    } END_TRY
    
    mu_assert("Outer and inner TRY blocks should execute", outer_try && inner_try && inner_catch && after_inner);
    mu_assert("Outer CATCH should not execute", !outer_catch);
    test_teardown();
    return 0;
}

// Test 4: Nested TRY/CATCH - Outer exception caught
static char *test_nested_try_catch_outer_exception(void) {
    test_setup();
    EvalState *st = test_state;
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    TRY {
        outer_try = true;
        TRY {
            inner_try = true;
        } CATCH(ex) {
            inner_catch = true;
            mu_assert("Inner CATCH should not run", 0);
        } END_TRY
        after_inner = true;
        throw_exception("OuterException", "Outer error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        outer_catch = true;
        mu_assert("Exception type should be OuterException", strcmp(ex->type, "OuterException") == 0);
    } END_TRY
    
    mu_assert("All blocks should execute correctly", outer_try && inner_try && after_inner && outer_catch);
    mu_assert("Inner CATCH should not execute", !inner_catch);
    test_teardown();
    return 0;
}

// Test 5: Triple nested TRY/CATCH
static char *test_triple_nested_try_catch(void) {
    test_setup();
    EvalState *st = test_state;
    bool level1 = false, level2 = false, level3 = false;
    bool catch1 = false, catch2 = false, catch3 = false;
    
    TRY {
        level1 = true;
        TRY {
            level2 = true;
            TRY {
                level3 = true;
                throw_exception("Level3Exception", "Deepest error", __FILE__, __LINE__, 0);
            } CATCH(ex) {
                catch3 = true;
                mu_assert("Exception type should be Level3Exception", strcmp(ex->type, "Level3Exception") == 0);
            } END_TRY
        } CATCH(ex) {
            catch2 = true;
            mu_assert("Level 2 CATCH should not run", 0);
        } END_TRY
    } CATCH(ex) {
        catch1 = true;
        mu_assert("Level 1 CATCH should not run", 0);
    } END_TRY
    
    mu_assert("All levels should execute, only innermost CATCH", level1 && level2 && level3 && catch3);
    mu_assert("Outer CATCHes should not execute", !(catch2 || catch1));
    test_teardown();
    return 0;
}

// Test 6: Re-throw from inner to outer CATCH
static char *test_rethrow_from_inner_to_outer(void) {
    test_setup();
    EvalState *st = test_state;
    bool inner_catch = false, outer_catch = false;
    
    TRY {
        TRY {
            throw_exception("InnerException", "Original error", __FILE__, __LINE__, 0);
        } CATCH(ex) {
            inner_catch = true;
            throw_exception("RethrowException", "Re-thrown from inner", __FILE__, __LINE__, 0);
        } END_TRY
        mu_assert("Should not reach here", 0);
    } CATCH(ex) {
        outer_catch = true;
        mu_assert("Re-thrown exception type should be RethrowException", strcmp(ex->type, "RethrowException") == 0);
    } END_TRY
    
    mu_assert("Both inner and outer CATCH should execute", inner_catch && outer_catch);
    test_teardown();
    return 0;
}

// Test 7: Exception stack cleanup verification
static char *test_exception_stack_cleanup(void) {
    test_setup();
    EvalState *st = test_state;
    ExceptionHandler *stack_before = test_state->exception_stack;
    
    TRY {
        mu_assert("Stack should have changed inside TRY", stack_before != test_state->exception_stack);
        throw_exception("TestException", "Test", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        // During CATCH, stack is already popped
        mu_assert("Stack should be restored during CATCH", stack_before == test_state->exception_stack);
    } END_TRY
    
    mu_assert("Stack should be restored after END_TRY", stack_before == test_state->exception_stack);
    test_teardown();
    return 0;
}

// Test 8: Multiple sequential TRY/CATCH blocks
static char *test_sequential_try_catch_blocks(void) {
    test_setup();
    EvalState *st = test_state;
    int catch_count = 0;
    
    TRY {
        throw_exception("Exception1", "First error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        catch_count++;
        mu_assert("First exception type should be Exception1", strcmp(ex->type, "Exception1") == 0);
    } END_TRY
    
    TRY {
        // Normal code
    } CATCH(ex) {
        mu_assert("Should not catch", 0);
    } END_TRY
    
    TRY {
        throw_exception("Exception3", "Third error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        catch_count++;
        mu_assert("Third exception type should be Exception3", strcmp(ex->type, "Exception3") == 0);
    } END_TRY
    
    mu_assert("Should have caught exactly 2 exceptions", catch_count == 2);
    test_teardown();
    return 0;
}

// Test 9: Exception with empty message
static char *test_exception_with_empty_message(void) {
    test_setup();
    EvalState *st = test_state;
    bool caught = false;
    
    TRY {
        throw_exception("TestType", "", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        caught = true;
        mu_assert("Exception should not be NULL", ex != NULL);
        mu_assert("Exception type should be TestType", strcmp(ex->type, "TestType") == 0);
        mu_assert("Exception message should be empty", strcmp(ex->message, "") == 0);
    } END_TRY
    
    mu_assert("Exception should have been caught", caught);
    test_teardown();
    return 0;
}

// Test 10: Verify exception content in CATCH
static char *test_exception_content_in_catch(void) {
    test_setup();
    EvalState *st = test_state;
    bool caught = false;
    
    TRY {
        throw_exception("TestType", "TestMsg", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        caught = true;
        mu_assert("Exception should not be NULL", ex != NULL);
        mu_assert("Exception type should be TestType", strcmp(ex->type, "TestType") == 0);
        mu_assert("Exception message should be TestMsg", strcmp(ex->message, "TestMsg") == 0);
    } END_TRY
    
    mu_assert("Exception should have been caught", caught);
    test_teardown();
    return 0;
}

// ============================================================================
// TEST SUITE RUNNER
// ============================================================================

static char *all_exception_tests(void) {
    mu_run_test(test_simple_try_catch_exception_caught);
    mu_run_test(test_simple_try_catch_no_exception);
    mu_run_test(test_nested_try_catch_inner_exception);
    mu_run_test(test_nested_try_catch_outer_exception);
    mu_run_test(test_triple_nested_try_catch);
    mu_run_test(test_rethrow_from_inner_to_outer);
    mu_run_test(test_exception_stack_cleanup);
    mu_run_test(test_sequential_try_catch_blocks);
    mu_run_test(test_exception_with_empty_message);
    mu_run_test(test_exception_content_in_catch);
    
    return 0;
}

int main(void) {
    printf("🧪 === Exception Handling Tests ===\n\n");
    
    init_special_symbols();
    
    int result = run_minunit_tests(all_exception_tests, "Exception Handling Tests");
    
    symbol_table_cleanup();
    
    return result;
}
