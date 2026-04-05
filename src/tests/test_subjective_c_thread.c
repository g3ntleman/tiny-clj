#include "tests_common.h"

#include "thread.h"

typedef struct {
    SubjectiveCMutex *mutex;
    SubjectiveCCondVar *condvar;
    bool ran;
    bool trylock_result;
    bool entered_wait;
    bool woke;
} SubjectiveCThreadTestContext;

static void subjective_c_thread_test_mark_ran(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->mutex) {
        return;
    }
    subjective_c_mutex_lock(ctx->mutex);
    ctx->ran = true;
    subjective_c_mutex_unlock(ctx->mutex);
}

static void subjective_c_thread_test_trylock_once(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->mutex) {
        return;
    }
    ctx->trylock_result = subjective_c_mutex_trylock(ctx->mutex);
    if (ctx->trylock_result) {
        subjective_c_mutex_unlock(ctx->mutex);
    }
}

static void subjective_c_thread_test_wait_for_signal(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->mutex || !ctx->condvar) {
        return;
    }

    subjective_c_mutex_lock(ctx->mutex);
    ctx->entered_wait = true;
    ctx->woke = subjective_c_condvar_wait(ctx->condvar, ctx->mutex, 1000u);
    subjective_c_mutex_unlock(ctx->mutex);
}

static void subjective_c_thread_test_sleep_briefly(void *arg) {
    (void)arg;
    subjective_c_thread_sleep_ms(20u);
}

TEST(test_subjective_c_thread_create_join) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    SubjectiveCThreadTestContext ctx = {
        .mutex = mutex,
    };

    SubjectiveCThread *thread = subjective_c_thread_create(subjective_c_thread_test_mark_ran,
                                                            &ctx,
                                                            NULL);
    TEST_ASSERT_NOT_NULL(thread);
    TEST_ASSERT_TRUE(subjective_c_thread_join(thread));
    subjective_c_thread_destroy(thread);

    subjective_c_mutex_lock(mutex);
    bool ran = ctx.ran;
    subjective_c_mutex_unlock(mutex);
    TEST_ASSERT_TRUE(ran);

    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_thread_create_with_name) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    SubjectiveCThreadTestContext ctx = {
        .mutex = mutex,
    };
    SubjectiveCThreadConfig config = {
        .name = "subjective-c-thread-test",
        .stack_bytes = 0u,
        .priority = 0,
    };

    SubjectiveCThread *thread = subjective_c_thread_create(subjective_c_thread_test_mark_ran,
                                                            &ctx,
                                                            &config);
    TEST_ASSERT_NOT_NULL(thread);
    TEST_ASSERT_TRUE(subjective_c_thread_join(thread));
    subjective_c_thread_destroy(thread);

    subjective_c_mutex_lock(mutex);
    bool ran = ctx.ran;
    subjective_c_mutex_unlock(mutex);
    TEST_ASSERT_TRUE(ran);

    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_mutex_lock_unlock) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    subjective_c_mutex_lock(mutex);
    subjective_c_mutex_unlock(mutex);

    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_mutex_trylock_succeeds_when_free) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    TEST_ASSERT_TRUE(subjective_c_mutex_trylock(mutex));
    subjective_c_mutex_unlock(mutex);

    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_mutex_trylock_fails_when_held) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    SubjectiveCThreadTestContext ctx = {
        .mutex = mutex,
    };

    subjective_c_mutex_lock(mutex);
    SubjectiveCThread *thread = subjective_c_thread_create(subjective_c_thread_test_trylock_once,
                                                            &ctx,
                                                            NULL);
    TEST_ASSERT_NOT_NULL(thread);
    TEST_ASSERT_TRUE(subjective_c_thread_join(thread));
    subjective_c_thread_destroy(thread);
    subjective_c_mutex_unlock(mutex);

    TEST_ASSERT_FALSE(ctx.trylock_result);

    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_condvar_signal_wakes_waiter) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    SubjectiveCCondVar *condvar = subjective_c_condvar_create();
    TEST_ASSERT_NOT_NULL(mutex);
    TEST_ASSERT_NOT_NULL(condvar);

    SubjectiveCThreadTestContext ctx = {
        .mutex = mutex,
        .condvar = condvar,
    };

    SubjectiveCThread *thread = subjective_c_thread_create(subjective_c_thread_test_wait_for_signal,
                                                            &ctx,
                                                            NULL);
    TEST_ASSERT_NOT_NULL(thread);

    for (uint32_t i = 0u; i < 200u && !ctx.entered_wait; i++) {
        subjective_c_thread_sleep_ms(1u);
    }
    TEST_ASSERT_TRUE(ctx.entered_wait);

    subjective_c_mutex_lock(mutex);
    subjective_c_condvar_signal(condvar);
    subjective_c_mutex_unlock(mutex);

    TEST_ASSERT_TRUE(subjective_c_thread_join(thread));
    subjective_c_thread_destroy(thread);
    TEST_ASSERT_TRUE(ctx.woke);

    subjective_c_condvar_destroy(condvar);
    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_condvar_wait_timeout) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    SubjectiveCCondVar *condvar = subjective_c_condvar_create();
    TEST_ASSERT_NOT_NULL(mutex);
    TEST_ASSERT_NOT_NULL(condvar);

    subjective_c_mutex_lock(mutex);
    bool woke = subjective_c_condvar_wait(condvar, mutex, 10u);
    subjective_c_mutex_unlock(mutex);
    TEST_ASSERT_FALSE(woke);

    subjective_c_condvar_destroy(condvar);
    subjective_c_mutex_destroy(mutex);
}

TEST(test_subjective_c_thread_stack_high_water_mark_null_is_zero) {
    TEST_ASSERT_EQUAL_UINT64(0u, (uint64_t)subjective_c_thread_stack_high_water_mark_bytes(NULL));
}

TEST(test_subjective_c_thread_stack_high_water_mark_reports_platform_value) {
    SubjectiveCThread *thread = subjective_c_thread_create(subjective_c_thread_test_sleep_briefly, NULL, NULL);
    TEST_ASSERT_NOT_NULL(thread);

    subjective_c_thread_sleep_ms(1u);
    size_t stack_hwm = subjective_c_thread_stack_high_water_mark_bytes(thread);
#if defined(ESP_PLATFORM)
    TEST_ASSERT_TRUE(stack_hwm > 0u);
#else
    TEST_ASSERT_EQUAL_UINT64(0u, (uint64_t)stack_hwm);
#endif

    TEST_ASSERT_TRUE(subjective_c_thread_join(thread));
    subjective_c_thread_destroy(thread);
}
