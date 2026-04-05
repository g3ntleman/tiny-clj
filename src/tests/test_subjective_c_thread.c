#include <stdatomic.h>

#include "unity/src/unity.h"
#include "test_registry.h"
#include "thread.h"

static atomic_uint g_subjective_c_once_runs = 0u;

typedef struct {
    SubjectiveCMutex *mutex;
    SubjectiveCCondVar *condvar;
    SubjectiveCOnce *once;
    bool ran;
    bool trylock_result;
    bool entered_wait;
    bool woke;
} SubjectiveCThreadTestContext;

static void tread_test_mark_ran(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->mutex) {
        return;
    }
    subjective_c_mutex_lock(ctx->mutex);
    ctx->ran = true;
    subjective_c_mutex_unlock(ctx->mutex);
}

static void tread_test_trylock_once(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->mutex) {
        return;
    }
    ctx->trylock_result = subjective_c_mutex_trylock(ctx->mutex);
    if (ctx->trylock_result) {
        subjective_c_mutex_unlock(ctx->mutex);
    }
}

static void tread_test_wait_for_signal(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->mutex || !ctx->condvar) {
        return;
    }

    subjective_c_mutex_lock(ctx->mutex);
    ctx->entered_wait = true;
    ctx->woke = subjective_c_condvar_wait(ctx->condvar, ctx->mutex, 1000u);
    subjective_c_mutex_unlock(ctx->mutex);
}

static void tread_test_sleep_briefly(void *arg) {
    (void)arg;
    tread_sleep_ms(20u);
}

static void tread_test_once_increment(void) {
    (void)atomic_fetch_add_explicit(&g_subjective_c_once_runs, 1u, memory_order_relaxed);
}

static void tread_test_run_once(void *arg) {
    SubjectiveCThreadTestContext *ctx = (SubjectiveCThreadTestContext *)arg;
    if (!ctx || !ctx->once) {
        return;
    }
    subjective_c_once_run(ctx->once, tread_test_once_increment);
}

TEST(test_tread_create_join) {
    SubjectiveCMutex *mutex = subjective_c_mutex_create();
    TEST_ASSERT_NOT_NULL(mutex);

    SubjectiveCThreadTestContext ctx = {
        .mutex = mutex,
    };

    SubjectiveCThread *thread = tread_create(tread_test_mark_ran,
                                                            &ctx,
                                                            NULL);
    TEST_ASSERT_NOT_NULL(thread);
    TEST_ASSERT_TRUE(tread_join(thread));
    tread_destroy(thread);

    subjective_c_mutex_lock(mutex);
    bool ran = ctx.ran;
    subjective_c_mutex_unlock(mutex);
    TEST_ASSERT_TRUE(ran);

    subjective_c_mutex_destroy(mutex);
}

TEST(test_tread_create_with_name) {
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

    SubjectiveCThread *thread = tread_create(tread_test_mark_ran,
                                                            &ctx,
                                                            &config);
    TEST_ASSERT_NOT_NULL(thread);
    TEST_ASSERT_TRUE(tread_join(thread));
    tread_destroy(thread);

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
    SubjectiveCThread *thread = tread_create(tread_test_trylock_once,
                                                            &ctx,
                                                            NULL);
    TEST_ASSERT_NOT_NULL(thread);
    TEST_ASSERT_TRUE(tread_join(thread));
    tread_destroy(thread);
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

    SubjectiveCThread *thread = tread_create(tread_test_wait_for_signal,
                                                            &ctx,
                                                            NULL);
    TEST_ASSERT_NOT_NULL(thread);

    for (uint32_t i = 0u; i < 200u && !ctx.entered_wait; i++) {
        tread_sleep_ms(1u);
    }
    TEST_ASSERT_TRUE(ctx.entered_wait);

    subjective_c_mutex_lock(mutex);
    subjective_c_condvar_signal(condvar);
    subjective_c_mutex_unlock(mutex);

    TEST_ASSERT_TRUE(tread_join(thread));
    tread_destroy(thread);
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

TEST(test_tread_stack_high_water_mark_null_is_zero) {
    TEST_ASSERT_EQUAL_UINT64(0u, (uint64_t)tread_stack_high_water_mark_bytes(NULL));
}

TEST(test_tread_stack_high_water_mark_reports_platform_value) {
    SubjectiveCThread *thread = tread_create(tread_test_sleep_briefly, NULL, NULL);
    TEST_ASSERT_NOT_NULL(thread);

    tread_sleep_ms(1u);
    size_t stack_hwm = tread_stack_high_water_mark_bytes(thread);
#if defined(ESP_PLATFORM)
    TEST_ASSERT_TRUE(stack_hwm > 0u);
#else
    TEST_ASSERT_EQUAL_UINT64(0u, (uint64_t)stack_hwm);
#endif

    TEST_ASSERT_TRUE(tread_join(thread));
    tread_destroy(thread);
}

TEST(test_subjective_c_once_runs_initializer_once_across_threads) {
    SubjectiveCOnce once = SUBJECTIVE_C_ONCE_INIT;
    SubjectiveCThreadTestContext ctx = {
        .once = &once,
    };
    atomic_store_explicit(&g_subjective_c_once_runs, 0u, memory_order_relaxed);

    SubjectiveCThread *threads[4] = {0};
    for (size_t i = 0; i < 4u; i++) {
        threads[i] = tread_create(tread_test_run_once, &ctx, NULL);
        TEST_ASSERT_NOT_NULL(threads[i]);
    }
    for (size_t i = 0; i < 4u; i++) {
        TEST_ASSERT_TRUE(tread_join(threads[i]));
        tread_destroy(threads[i]);
    }

    subjective_c_once_run(&once, tread_test_once_increment);
    TEST_ASSERT_EQUAL_UINT32(1u,
                             atomic_load_explicit(&g_subjective_c_once_runs, memory_order_relaxed));
}
