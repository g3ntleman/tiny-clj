#include "test_common.h"

TEST(test_throw_exception_formatted) {
    void *result = NULL;
    CLJException *caught_ex = NULL;
    
    TRY {
        result = throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0, 
                                          "Test formatted message: %d", 42);
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
    } CATCH(ex) {
        caught_ex = ex;
        TEST_ASSERT_NOT_NULL(caught_ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_RUNTIME, caught_ex->type);
        TEST_ASSERT_NOT_NULL(strstr(caught_ex->message, "Test formatted message: 42"));
        TEST_ASSERT_NOT_NULL(strstr(caught_ex->file, "test_exception.c"));
        TEST_ASSERT_TRUE(caught_ex->line > 0);
    } END_TRY
    
    TEST_ASSERT_NULL(result);
    TEST_ASSERT_NOT_NULL(caught_ex);
}

TEST(test_try_catch_flow) {
    CljString *test_obj = make_string("test");
    TEST_ASSERT_NOT_NULL(test_obj);
    int initial_rc = retain_count(test_obj);
    
    CLJException *caught_ex = NULL;
    
    TRY {
        // Retain object to test RELEASE in CATCH
        RETAIN(test_obj);
        TEST_ASSERT_EQUAL_INT(initial_rc + 1, retain_count(test_obj));
        
        throw_exception(EXCEPTION_RUNTIME, "Test exception", __FILE__, __LINE__, 0);
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
    } CATCH(ex) {
        caught_ex = ex;
        TEST_ASSERT_NOT_NULL(caught_ex);
        TEST_ASSERT_EQUAL_STRING(EXCEPTION_RUNTIME, caught_ex->type);
        
        // Verify object is still valid and can be released
        TEST_ASSERT_EQUAL_INT(initial_rc + 1, retain_count(test_obj));
        RELEASE(test_obj);
        TEST_ASSERT_EQUAL_INT(initial_rc, retain_count(test_obj));
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(caught_ex);
    RELEASE(test_obj);
}

TEST(test_stacktrace_toggle) {
    CLJException *caught_ex = NULL;
    
    TRY {
        throw_exception(EXCEPTION_RUNTIME, "Test stacktrace", __FILE__, __LINE__, 0);
        TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
    } CATCH(ex) {
        caught_ex = ex;
        TEST_ASSERT_NOT_NULL(caught_ex);
        
#ifdef DEBUG
        if (caught_ex->stacktrace) {
            TEST_ASSERT_NOT_NULL(caught_ex->stacktrace);
            CljString *st = caught_ex->stacktrace;
            TEST_ASSERT_TRUE(st->length >= 0);
        }
    TEST_ASSERT_TRUE(caught_ex->object == 0);
#endif
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(caught_ex);
}

TEST(test_autorelease_pool_cleanup_on_exception) {
    CLJException *caught_ex = NULL;
    
    TRY {
        WITH_AUTORELEASE_POOL({
            TEST_ASSERT_TRUE(is_autorelease_pool_active());
            
            CljString *pooled_obj = (CljString*)AUTORELEASE(make_string("pooled"));
            TEST_ASSERT_NOT_NULL(pooled_obj);
            
            throw_exception(EXCEPTION_RUNTIME, "Test exception in pool", __FILE__, __LINE__, 0);
            TEST_ASSERT_TRUE_MESSAGE(false, "Should not reach here after throw");
        });
    } CATCH(ex) {
        caught_ex = ex;
        TEST_ASSERT_NOT_NULL(caught_ex);
    } END_TRY
    
    TEST_ASSERT_NOT_NULL(caught_ex);
    
    // Test cleanup function
    autorelease_pool_push();
    TEST_ASSERT_TRUE(is_autorelease_pool_active());
    
    CljString *test_obj = (CljString*)AUTORELEASE(make_string("test"));
    (void)test_obj;
    
    autorelease_pool_cleanup_after_exception();
    TEST_ASSERT_FALSE(is_autorelease_pool_active());
}










