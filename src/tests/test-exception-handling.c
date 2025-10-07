/**
 * Test suite for TRY/CATCH/END_TRY exception handling
 * Tests nested exception handling, auto-release, and exception stack
 */

#include "unity.h"
#include "namespace.h"
#include "exception.h"
#include "object.h"
#include "runtime.h"
#include "function_call.h"
#include <string.h>

static EvalState *test_state = NULL;

void setUp(void) {
    test_state = evalstate();
    TEST_ASSERT_NOT_NULL(test_state);
    test_state->exception_stack = NULL;
    set_global_eval_state(test_state);
}

void tearDown(void) {
    set_global_eval_state(NULL);
    test_state = NULL;
}

// Test 1: Simple TRY/CATCH - Exception caught
void test_simple_try_catch_exception_caught(void) {
    EvalState *st = test_state;
    bool exception_caught = false;
    
    TRY {
        throw_exception("TestException", "Test error", __FILE__, __LINE__, 0);
        TEST_FAIL_MESSAGE("Should not reach here after throw");
    } CATCH(ex) {
        exception_caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("TestException", ex->type);
        TEST_ASSERT_EQUAL_STRING("Test error", ex->message);
    } END_TRY
    
    TEST_ASSERT_TRUE(exception_caught);
}

// Test 2: Simple TRY/CATCH - No exception
void test_simple_try_catch_no_exception(void) {
    EvalState *st = test_state;
    bool try_executed = false;
    bool catch_executed = false;
    
    TRY {
        try_executed = true;
    } CATCH(ex) {
        catch_executed = true;
        TEST_FAIL_MESSAGE("CATCH should not run when no exception");
    } END_TRY
    
    TEST_ASSERT_TRUE(try_executed);
    TEST_ASSERT_FALSE(catch_executed);
}

// Test 3: Nested TRY/CATCH - Inner exception caught
void test_nested_try_catch_inner_exception(void) {
    EvalState *st = test_state;
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    TRY {
        outer_try = true;
        TRY {
            inner_try = true;
            throw_exception("InnerException", "Inner error", __FILE__, __LINE__, 0);
            TEST_FAIL_MESSAGE("Should not reach here");
        } CATCH(ex) {
            inner_catch = true;
            TEST_ASSERT_EQUAL_STRING("InnerException", ex->type);
        } END_TRY
        after_inner = true;
    } CATCH(ex) {
        outer_catch = true;
        TEST_FAIL_MESSAGE("Outer CATCH should not run");
    } END_TRY
    
    TEST_ASSERT_TRUE(outer_try && inner_try && inner_catch && after_inner);
    TEST_ASSERT_FALSE(outer_catch);
}

// Test 4: Nested TRY/CATCH - Outer exception caught
void test_nested_try_catch_outer_exception(void) {
    EvalState *st = test_state;
    bool outer_try = false, inner_try = false, inner_catch = false;
    bool outer_catch = false, after_inner = false;
    
    TRY {
        outer_try = true;
        TRY {
            inner_try = true;
        } CATCH(ex) {
            inner_catch = true;
            TEST_FAIL_MESSAGE("Inner CATCH should not run");
        } END_TRY
        after_inner = true;
        throw_exception("OuterException", "Outer error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        outer_catch = true;
        TEST_ASSERT_EQUAL_STRING("OuterException", ex->type);
    } END_TRY
    
    TEST_ASSERT_TRUE(outer_try && inner_try && after_inner && outer_catch);
    TEST_ASSERT_FALSE(inner_catch);
}

// Test 5: Triple nested TRY/CATCH
void test_triple_nested_try_catch(void) {
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
                TEST_ASSERT_EQUAL_STRING("Level3Exception", ex->type);
            } END_TRY
        } CATCH(ex) {
            catch2 = true;
            TEST_FAIL_MESSAGE("Level 2 CATCH should not run");
        } END_TRY
    } CATCH(ex) {
        catch1 = true;
        TEST_FAIL_MESSAGE("Level 1 CATCH should not run");
    } END_TRY
    
    TEST_ASSERT_TRUE(level1 && level2 && level3 && catch3);
    TEST_ASSERT_FALSE(catch2 || catch1);
}

// Test 6: Re-throw from inner to outer CATCH
void test_rethrow_from_inner_to_outer(void) {
    EvalState *st = test_state;
    bool inner_catch = false, outer_catch = false;
    
    TRY {
        TRY {
            throw_exception("InnerException", "Original error", __FILE__, __LINE__, 0);
        } CATCH(ex) {
            inner_catch = true;
            throw_exception("RethrowException", "Re-thrown from inner", __FILE__, __LINE__, 0);
        } END_TRY
        TEST_FAIL_MESSAGE("Should not reach here");
    } CATCH(ex) {
        outer_catch = true;
        TEST_ASSERT_EQUAL_STRING("RethrowException", ex->type);
    } END_TRY
    
    TEST_ASSERT_TRUE(inner_catch && outer_catch);
}

// Test 7: Exception stack cleanup verification
void test_exception_stack_cleanup(void) {
    EvalState *st = test_state;
    ExceptionHandler *stack_before = test_state->exception_stack;
    
    TRY {
        TEST_ASSERT_NOT_EQUAL(stack_before, test_state->exception_stack);
        throw_exception("TestException", "Test", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        // During CATCH, stack is already popped
        TEST_ASSERT_EQUAL(stack_before, test_state->exception_stack);
    } END_TRY
    
    TEST_ASSERT_EQUAL(stack_before, test_state->exception_stack);
}

// Test 8: Multiple sequential TRY/CATCH blocks
void test_sequential_try_catch_blocks(void) {
    EvalState *st = test_state;
    int catch_count = 0;
    
    TRY {
        throw_exception("Exception1", "First error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        catch_count++;
        TEST_ASSERT_EQUAL_STRING("Exception1", ex->type);
    } END_TRY
    
    TRY {
        // Normal code
    } CATCH(ex) {
        TEST_FAIL_MESSAGE("Should not catch");
    } END_TRY
    
    TRY {
        throw_exception("Exception3", "Third error", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        catch_count++;
        TEST_ASSERT_EQUAL_STRING("Exception3", ex->type);
    } END_TRY
    
    TEST_ASSERT_EQUAL(2, catch_count);
}

// Test 9: Exception with empty message
void test_exception_with_empty_message(void) {
    EvalState *st = test_state;
    bool caught = false;
    
    TRY {
        throw_exception("TestType", "", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("TestType", ex->type);
        TEST_ASSERT_EQUAL_STRING("", ex->message);
    } END_TRY
    
    TEST_ASSERT_TRUE(caught);
}

// Test 10: Verify exception content in CATCH
void test_exception_content_in_catch(void) {
    EvalState *st = test_state;
    bool caught = false;
    
    TRY {
        throw_exception("TestType", "TestMsg", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        caught = true;
        TEST_ASSERT_NOT_NULL(ex);
        TEST_ASSERT_EQUAL_STRING("TestType", ex->type);
        TEST_ASSERT_EQUAL_STRING("TestMsg", ex->message);
    } END_TRY
    
    TEST_ASSERT_TRUE(caught);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_simple_try_catch_exception_caught);
    RUN_TEST(test_simple_try_catch_no_exception);
    RUN_TEST(test_nested_try_catch_inner_exception);
    RUN_TEST(test_nested_try_catch_outer_exception);
    RUN_TEST(test_triple_nested_try_catch);
    RUN_TEST(test_rethrow_from_inner_to_outer);
    RUN_TEST(test_exception_stack_cleanup);
    RUN_TEST(test_sequential_try_catch_blocks);
    RUN_TEST(test_exception_with_empty_message);
    RUN_TEST(test_exception_content_in_catch);
    
    return UNITY_END();
}
