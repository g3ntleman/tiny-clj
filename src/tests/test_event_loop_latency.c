/*
 * Event-loop latency helper tests.
 */

#include "tests_common.h"
#include "../event_loop.h"
#include "../fx_host_runloop.h"
#include "../function.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static ID event_loop_test_slow_native_task(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    usleep(25000);
    return NULL;
}

static uint64_t event_loop_test_monotonic_ms(void) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000ull) + ((uint64_t)ts.tv_nsec / 1000000ull);
}

typedef struct {
    ID fn;
    useconds_t delay_us;
    bool enqueue_ok;
} EventLoopRunIngressWakeArgs;

static void *event_loop_run_ingress_wake_thread(void *arg) {
    EventLoopRunIngressWakeArgs *args = (EventLoopRunIngressWakeArgs *)arg;
    if (!args || !args->fn) {
        return NULL;
    }
    usleep(args->delay_us);
    args->enqueue_ok = event_loop_enqueue_ingress((CljObject *)args->fn);
    return NULL;
}

typedef struct {
    useconds_t delay_us;
} EventLoopRunWakeArgs;

static void *event_loop_run_wake_thread(void *arg) {
    EventLoopRunWakeArgs *args = (EventLoopRunWakeArgs *)arg;
    if (!args) {
        return NULL;
    }
    usleep(args->delay_us);
    event_loop_wake();
    return NULL;
}

typedef bool (*EventLoopStdoutCaptureAction)(void *ctx);

static char *event_loop_capture_stdout(EventLoopStdoutCaptureAction action, void *ctx) {
    if (!action) {
        return NULL;
    }
    FILE *tmp = tmpfile();
    if (!tmp) {
        return NULL;
    }
    int stdout_fd = fileno(stdout);
    int saved_stdout = dup(stdout_fd);
    if (saved_stdout < 0) {
        fclose(tmp);
        return NULL;
    }
    fflush(stdout);
    if (dup2(fileno(tmp), stdout_fd) < 0) {
        close(saved_stdout);
        fclose(tmp);
        return NULL;
    }

    (void)action(ctx);

    fflush(stdout);
    (void)dup2(saved_stdout, stdout_fd);
    close(saved_stdout);

    if (fseek(tmp, 0, SEEK_END) != 0) {
        fclose(tmp);
        return NULL;
    }
    long size = ftell(tmp);
    if (size < 0) {
        fclose(tmp);
        return NULL;
    }
    if (fseek(tmp, 0, SEEK_SET) != 0) {
        fclose(tmp);
        return NULL;
    }
    char *buf = (char *)CLJ_MALLOC((size_t)size + 1u);
    if (!buf) {
        fclose(tmp);
        return NULL;
    }
    size_t read_n = fread(buf, 1u, (size_t)size, tmp);
    buf[read_n] = '\0';
    fclose(tmp);
    return buf;
}

typedef struct {
    EvalState *st;
    bool ran;
} EventLoopDrainCaptureCtx;

static bool event_loop_capture_drain_one(void *ctx) {
    EventLoopDrainCaptureCtx *capture = (EventLoopDrainCaptureCtx *)ctx;
    if (!capture || !capture->st) {
        return false;
    }
    capture->ran = event_loop_run_next(NULL, capture->st);
    return capture->ran;
}

TEST(test_event_loop_time_until_next_timer_no_timer_returns_minus_one) {
    int t = event_loop_time_until_next_timer_ms();
    TEST_ASSERT_EQUAL_INT(-1, t);
}

TEST(test_event_loop_time_until_next_timer_for_scheduled_timer) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID timer_id = eval_string("(schedule-periodic 30 1000 (fn [] 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(timer_id);
    TEST_ASSERT_TRUE(is_fixnum(timer_id));

    int t = event_loop_time_until_next_timer_ms();
    TEST_ASSERT_TRUE_MESSAGE(t >= 0, "scheduled timer should report non-negative remaining ms");
    TEST_ASSERT_TRUE_MESSAGE(t <= 60, "remaining ms should be close to configured delay");
}

TEST(test_event_loop_has_pending_tasks_tracks_zero_delay_schedule) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after clear");

    ID schedule_result = eval_string("(schedule 0 (fn [] 42))", g_test_eval_state);
    TEST_ASSERT_NULL_MESSAGE(schedule_result, "zero-delay schedule is expected to return nil");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(), "queue should contain immediate scheduled task");

    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued task");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after running one task");
}

TEST(test_event_loop_run_next_zero_delay_stress) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    for (int i = 0; i < 256; i++) {
        ID schedule_result = eval_string("(schedule 0 (fn [] 42))", g_test_eval_state);
        TEST_ASSERT_NULL_MESSAGE(schedule_result, "zero-delay schedule is expected to return nil");
        TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(), "queue should contain scheduled task");

        bool ran = event_loop_run_next(NULL, g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued task");
    }

    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after stress loop");
}

TEST(test_event_loop_run_next_slow_task_warning_keeps_task_objects_alive_and_bounded) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    MemoryStats before = memory_profiler_get_stats();

    for (int i = 0; i < 4; i++) {
        ID fn = make_named_func(event_loop_test_slow_native_task,
                                intern_symbol_global("event-loop-test/slow-native-task"));
        TEST_ASSERT_NOT_NULL_MESSAGE(fn, "slow native task should be creatable");
        event_loop_enqueue((CljObject *)fn, NULL);
        RELEASE(fn);

        bool ran = event_loop_run_next(NULL, g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute the slow queued task");
    }

    MemoryStats after = memory_profiler_get_stats();
    long long diff = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(1024,
                                          (int)diff,
                                          "slow-task warning path should not retain task objects or rendered log strings");
}

TEST(test_event_loop_run_next_slow_task_warning_includes_named_closure_symbol) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (defn event-loop-latency-slow-handler [event] "
        "    (sleep 25) "
        "    nil) "
        "  event-loop-latency-slow-handler)",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_CLOSURE || TAG(fn) == CLJ_FUNC);

    ID payload = eval_string("{:id :slow-runloop-event}", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload),
                             "enqueue for slow warning test should succeed");

    EventLoopDrainCaptureCtx capture = {.st = g_test_eval_state, .ran = false};
    char *stdout_output = event_loop_capture_stdout(event_loop_capture_drain_one, &capture);
    TEST_ASSERT_NOT_NULL_MESSAGE(stdout_output, "failed to capture stdout for runloop warning");
    TEST_ASSERT_TRUE_MESSAGE(capture.ran, "run_next should execute the queued slow closure");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(stdout_output, "[viewer-runloop] warning: clojure runloop event took"),
                                 "expected slow runloop warning on stdout");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(stdout_output, "event-loop-latency-slow-handler"),
                                 "expected warning to include the closure function name");
    TEST_ASSERT_NULL_MESSAGE(strstr(stdout_output, "<symbol>"),
                             "expected warning to avoid opaque <symbol> callable names");
    CLJ_FREE(stdout_output);
}

TEST(test_event_loop_time_until_next_timer_does_not_consume_timer_entry) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID timer_id_val = eval_string("(schedule-periodic 200 1000 (fn [] 1))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(timer_id_val);
    TEST_ASSERT_TRUE(is_fixnum(timer_id_val));
    int timer_id = (int)as_fixnum(timer_id_val);
    TEST_ASSERT_TRUE_MESSAGE(timer_id > 0, "timer id should be positive");

    for (int i = 0; i < 256; i++) {
        int t = event_loop_time_until_next_timer_ms();
        TEST_ASSERT_TRUE_MESSAGE(t >= 0, "time-until-next-timer should remain non-negative");
    }

    TEST_ASSERT_TRUE_MESSAGE(timer_cancel(timer_id), "timer should still exist and be cancellable");
    TEST_ASSERT_EQUAL_INT(-1, event_loop_time_until_next_timer_ms());
}

TEST(test_event_loop_ingress_enqueue_executes_on_run_next) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(), "ingress queue should start empty");

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-marker (atom 0)) "
        "  (fn event-loop-ingress-task [] "
        "    (reset! event-loop-ingress-marker 1) "
        "    123))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress(fn), "ingress enqueue should succeed");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_ingress_has_pending(), "ingress queue should report pending task");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(), "event loop should report pending task");

    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute one ingress task");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(), "ingress queue should be empty after drain");

    ID marker = eval_string("@event-loop-ingress-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(marker));
}

TEST(test_event_loop_ingress_drain_budget_keeps_excess_work_pending_for_next_tick) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-budget-marker (atom [])) "
        "  (fn event-loop-ingress-budget-task [x] "
        "    (swap! event-loop-ingress-budget-marker conj x) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, intern_symbol_global(":a")),
                             "first ingress enqueue should succeed");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, intern_symbol_global(":b")),
                             "second ingress enqueue should succeed");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_ingress_has_pending(), "ingress should report pending work");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "first run_next should execute one ingress callback");
    ID first_marker_ok = eval_string("(= @event-loop-ingress-budget-marker [:a])", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, first_marker_ok);
    TEST_ASSERT_TRUE_MESSAGE(event_loop_ingress_has_pending(),
                             "one ingress callback should remain pending for next tick");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "second run_next should execute remaining ingress callback");
    ID second_marker_ok = eval_string("(= @event-loop-ingress-budget-marker [:a :b])", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, second_marker_ok);
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(),
                              "ingress queue should be empty after second tick");
}

TEST(test_event_loop_ingress_close_rejects_new_enqueue_but_drains_pending) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_is_closed(), "ingress should start open");

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-close-marker (atom 0)) "
        "  (fn event-loop-ingress-close-task [] "
        "    (swap! event-loop-ingress-close-marker (fn [x] (+ x 1))) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress(fn), "first enqueue should succeed");
    event_loop_ingress_close();
    TEST_ASSERT_TRUE_MESSAGE(event_loop_ingress_is_closed(), "ingress should report closed");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_enqueue_ingress(fn), "enqueue after close should fail");

    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "pending ingress task should still drain after close");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(), "ingress queue should be empty after drain");

    ID marker = eval_string("@event-loop-ingress-close-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(marker));
}

typedef struct {
    ID fn;
    int count;
    int success_count;
} IngressProducerArgs;

static void *event_loop_ingress_producer_thread(void *arg) {
    IngressProducerArgs *a = (IngressProducerArgs *)arg;
    if (!a) {
        return NULL;
    }
    int ok = 0;
    for (int i = 0; i < a->count; i++) {
        if (event_loop_enqueue_ingress(a->fn)) {
            ok++;
        }
    }
    a->success_count = ok;
    return NULL;
}

typedef struct {
    const char *expr;
    pthread_t producer_thread;
    pthread_t callback_thread;
    bool ran;
    bool eval_ok;
    bool enqueue_ok;
} NativeIngressEvalArgs;

static void event_loop_native_eval_callback(void *ctx, EvalState *st) {
    NativeIngressEvalArgs *args = (NativeIngressEvalArgs *)ctx;
    if (!args) {
        return;
    }
    args->callback_thread = pthread_self();
    args->ran = true;
    ID result = eval_string(args->expr, st);
    args->eval_ok = (result == clj_true);
}

static void *event_loop_native_eval_producer_thread(void *arg) {
    NativeIngressEvalArgs *args = (NativeIngressEvalArgs *)arg;
    if (!args) {
        return NULL;
    }
    args->producer_thread = pthread_self();
    args->enqueue_ok =
        event_loop_enqueue_ingress_native(event_loop_native_eval_callback, args, NULL);
    return NULL;
}

static void *event_loop_native_dispatch_producer_thread(void *arg) {
    NativeIngressEvalArgs *args = (NativeIngressEvalArgs *)arg;
    if (!args) {
        return NULL;
    }
    args->producer_thread = pthread_self();
    args->enqueue_ok =
        event_loop_dispatch_native(event_loop_native_eval_callback, args, NULL);
    return NULL;
}

typedef struct {
    uint32_t value;
    uint32_t *values;
    uint32_t *count;
    pthread_t callback_thread;
} NativeIngressOrderArgs;

static void event_loop_native_order_callback(void *ctx, EvalState *st) {
    (void)st;
    NativeIngressOrderArgs *args = (NativeIngressOrderArgs *)ctx;
    if (!args || !args->values || !args->count) {
        return;
    }
    args->callback_thread = pthread_self();
    args->values[*args->count] = args->value;
    (*args->count)++;
}

TEST(test_event_loop_ingress_concurrent_producers_fifo_drain) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-concurrent-marker (atom 0)) "
        "  (fn event-loop-ingress-concurrent-task [] "
        "    (swap! event-loop-ingress-concurrent-marker (fn [x] (+ x 1))) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    enum { THREADS = 4, TASKS_PER_THREAD = 8 };
    pthread_t threads[THREADS];
    IngressProducerArgs args[THREADS];

    for (int i = 0; i < THREADS; i++) {
        args[i].fn = fn;
        args[i].count = TASKS_PER_THREAD;
        args[i].success_count = 0;
        int rc = pthread_create(&threads[i], NULL, event_loop_ingress_producer_thread, &args[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_create failed");
    }
    for (int i = 0; i < THREADS; i++) {
        int rc = pthread_join(threads[i], NULL);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_join failed");
    }

    int expected = 0;
    for (int i = 0; i < THREADS; i++) {
        expected += args[i].success_count;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(THREADS * TASKS_PER_THREAD, expected,
                                  "all producer enqueues should succeed within ingress capacity");

    int ran_count = 0;
    while (event_loop_has_pending_tasks()) {
        bool ran = event_loop_run_next(NULL, g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued ingress task");
        ran_count++;
    }
    TEST_ASSERT_EQUAL_INT(expected, ran_count);
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(), "ingress queue should be empty after draining");

    ID marker = eval_string("@event-loop-ingress-concurrent-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(expected, as_fixnum(marker));
}

TEST(test_event_loop_native_ingress_callback_can_call_clojure_on_drain_thread) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID setup = eval_string(
        "(do "
        "  (def event-loop-native-marker (atom nil)) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    NativeIngressEvalArgs args = {
        .expr = "(do (reset! event-loop-native-marker :from-native) true)",
    };
    pthread_t producer;
    int rc = pthread_create(&producer, NULL, event_loop_native_eval_producer_thread, &args);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_create failed");
    rc = pthread_join(producer, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_join failed");

    TEST_ASSERT_TRUE_MESSAGE(args.enqueue_ok, "native ingress enqueue should succeed");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(), "native ingress should make work visible");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "run_next should execute queued native ingress callback");
    TEST_ASSERT_TRUE_MESSAGE(args.ran, "native ingress callback should run");
    TEST_ASSERT_TRUE_MESSAGE(args.eval_ok, "native ingress callback should be able to evaluate Clojure");
    TEST_ASSERT_FALSE_MESSAGE(pthread_equal(args.producer_thread, args.callback_thread),
                              "native ingress callback must not execute on producer thread");

    ID marker = eval_string("@event-loop-native-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":from-native"), marker);
}

TEST(test_event_loop_dispatch_native_runs_inline_without_registered_interpreter_thread) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    subjective_c_clear_interpreter_thread();

    ID setup = eval_string(
        "(do "
        "  (def event-loop-native-dispatch-marker (atom nil)) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    NativeIngressEvalArgs args = {
        .expr = "(do (reset! event-loop-native-dispatch-marker :inline-no-interpreter) true)",
        .producer_thread = pthread_self(),
    };

    TEST_ASSERT_TRUE_MESSAGE(event_loop_dispatch_native(event_loop_native_eval_callback, &args, NULL),
                             "dispatch_native should succeed inline without interpreter registration");
    TEST_ASSERT_TRUE_MESSAGE(args.ran, "dispatch_native should run callback immediately");
    TEST_ASSERT_TRUE_MESSAGE(args.eval_ok, "inline callback should be able to evaluate Clojure");
    TEST_ASSERT_TRUE_MESSAGE(pthread_equal(args.producer_thread, args.callback_thread),
                             "inline dispatch should run on the caller thread");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(),
                              "inline dispatch should not leave pending event-loop work behind");

    ID marker = eval_string("@event-loop-native-dispatch-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":inline-no-interpreter"), marker);
}

TEST(test_event_loop_dispatch_native_runs_inline_on_registered_interpreter_thread) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    subjective_c_clear_interpreter_thread();
    subjective_c_register_interpreter_thread();

    ID setup = eval_string(
        "(do "
        "  (def event-loop-native-dispatch-registered-marker (atom nil)) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    NativeIngressEvalArgs args = {
        .expr = "(do (reset! event-loop-native-dispatch-registered-marker :inline-registered) true)",
        .producer_thread = pthread_self(),
    };

    TEST_ASSERT_TRUE_MESSAGE(event_loop_dispatch_native(event_loop_native_eval_callback, &args, NULL),
                             "dispatch_native should succeed inline on the interpreter thread");
    TEST_ASSERT_TRUE_MESSAGE(args.ran, "dispatch_native should run immediately on interpreter thread");
    TEST_ASSERT_TRUE_MESSAGE(args.eval_ok, "inline interpreter-thread callback should be able to evaluate Clojure");
    TEST_ASSERT_TRUE_MESSAGE(pthread_equal(args.producer_thread, args.callback_thread),
                             "interpreter-thread dispatch should stay on the same thread");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(),
                              "interpreter-thread dispatch should not enqueue work");

    ID marker = eval_string("@event-loop-native-dispatch-registered-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":inline-registered"), marker);

    subjective_c_clear_interpreter_thread();
}

TEST(test_event_loop_dispatch_native_enqueues_from_foreign_thread_when_interpreter_registered) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();
    subjective_c_clear_interpreter_thread();
    subjective_c_register_interpreter_thread();

    ID setup = eval_string(
        "(do "
        "  (def event-loop-native-dispatch-worker-marker (atom nil)) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    NativeIngressEvalArgs args = {
        .expr = "(do (reset! event-loop-native-dispatch-worker-marker :queued-from-worker) true)",
    };
    pthread_t producer;
    int rc = pthread_create(&producer, NULL, event_loop_native_dispatch_producer_thread, &args);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_create failed");
    rc = pthread_join(producer, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_join failed");

    TEST_ASSERT_TRUE_MESSAGE(args.enqueue_ok, "dispatch_native should enqueue from foreign threads");
    TEST_ASSERT_FALSE_MESSAGE(args.ran, "foreign-thread dispatch should not run callback inline");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(),
                             "queued dispatch should make pending work visible");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "run_next should drain queued dispatch callback");
    TEST_ASSERT_TRUE_MESSAGE(args.ran, "queued dispatch callback should eventually run");
    TEST_ASSERT_TRUE_MESSAGE(args.eval_ok, "queued dispatch callback should be able to evaluate Clojure");
    TEST_ASSERT_FALSE_MESSAGE(pthread_equal(args.producer_thread, args.callback_thread),
                              "queued dispatch callback must not execute on producer thread");

    ID marker = eval_string("@event-loop-native-dispatch-worker-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":queued-from-worker"), marker);

    subjective_c_clear_interpreter_thread();
}

TEST(test_event_loop_native_ingress_tick_budget_limits_callbacks_per_run) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    uint32_t values[5] = {0};
    uint32_t count = 0u;
    NativeIngressOrderArgs args[5] = {
        {.value = 1u, .values = values, .count = &count},
        {.value = 2u, .values = values, .count = &count},
        {.value = 3u, .values = values, .count = &count},
        {.value = 4u, .values = values, .count = &count},
        {.value = 5u, .values = values, .count = &count},
    };

    for (size_t i = 0; i < 5u; i++) {
        TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_native(event_loop_native_order_callback,
                                                                   &args[i],
                                                                   NULL),
                                 "native ingress enqueue should succeed");
    }

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "run_next should drain a native-ingress tick");
    TEST_ASSERT_EQUAL_UINT32(4u, count);
    TEST_ASSERT_EQUAL_UINT32(1u, values[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, values[1]);
    TEST_ASSERT_EQUAL_UINT32(3u, values[2]);
    TEST_ASSERT_EQUAL_UINT32(4u, values[3]);
    TEST_ASSERT_TRUE_MESSAGE(event_loop_has_pending_tasks(),
                             "native ingress beyond the tick budget should remain pending");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "second run_next should drain the remaining native callback");
    TEST_ASSERT_EQUAL_UINT32(5u, count);
    TEST_ASSERT_EQUAL_UINT32(5u, values[4]);
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(),
                              "native ingress queue should be empty after the second tick");
}

TEST(test_event_loop_ingress_backpressure_rejects_when_full_and_recovers_after_drain) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-backpressure-marker (atom 0)) "
        "  (fn event-loop-ingress-backpressure-task [] "
        "    (swap! event-loop-ingress-backpressure-marker (fn [x] (+ x 1))) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    int accepted = 0;
    for (int i = 0; i < 256; i++) {
        if (!event_loop_enqueue_ingress(fn)) {
            break;
        }
        accepted++;
    }
    TEST_ASSERT_TRUE_MESSAGE(accepted > 0, "ingress should accept at least one task");
    TEST_ASSERT_TRUE_MESSAGE(accepted < 256, "ingress should be bounded and reject before 256 tasks");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_enqueue_ingress(fn), "enqueue should fail when ingress is full");

    int drained = 0;
    while (event_loop_has_pending_tasks()) {
        bool ran = event_loop_run_next(NULL, g_test_eval_state);
        TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued ingress task");
        drained++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(accepted, drained, "drained task count should match accepted enqueue count");

    ID marker = eval_string("@event-loop-ingress-backpressure-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(accepted, as_fixnum(marker));

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress(fn), "enqueue should recover after queue drain");
    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute recovered enqueue");
}

TEST(test_event_loop_ingress_call_preserves_nil_payload_as_argument) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-nil-arg-marker (atom false)) "
        "  (fn event-loop-ingress-nil-arg-task [x] "
        "    (reset! event-loop-ingress-nil-arg-marker (nil? x)) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, NULL),
                             "ingress call enqueue with nil payload should succeed");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_ingress_has_pending(),
                             "ingress queue should report pending call task");

    bool ran = event_loop_run_next(NULL, g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ran, "run_next should execute queued ingress call");

    ID marker = eval_string("@event-loop-ingress-nil-arg-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(clj_true, marker,
                                  "ingress callback should receive nil payload as one argument");
}

#if MEMORY_PROFILING_ENABLED
TEST(test_event_loop_ingress_call_does_not_allocate_task_map_after_warmup) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(fn event-loop-ingress-pod-task [event] "
        "  (:kind event))",
        g_test_eval_state);
    ID payload = eval_string("{:kind :collision :key :brick-2001}", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload), "warmup enqueue should succeed");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state), "warmup run_next should succeed");

    size_t baseline = memory_current_usage_bytes();

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload), "steady-state enqueue should succeed");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(baseline, memory_current_usage_bytes(),
                                     "ingress enqueue call should stay heap-stable after warmup");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state), "steady-state run_next should succeed");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(baseline, memory_current_usage_bytes(),
                                     "ingress drain should return to steady-state heap after warmup");
}

TEST(test_timer_upsert_named_does_not_allocate_named_timer_nodes_after_warmup) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string("(fn named-timer-test-task [] nil)", g_test_eval_state);
    ID key = intern_symbol_global(":named-timer/steady");
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_NOT_NULL(key);

    int warm_timer_id = timer_upsert_named(key, (CljObject *)fn, 50, false, 0);
    TEST_ASSERT_TRUE_MESSAGE(warm_timer_id > 0, "warmup named timer should schedule");
    TEST_ASSERT_TRUE_MESSAGE(timer_cancel_named(key), "warmup named timer should cancel");

    size_t baseline = memory_current_usage_bytes();

    int timer_id = timer_upsert_named(key, (CljObject *)fn, 50, false, 0);
    TEST_ASSERT_TRUE_MESSAGE(timer_id > 0, "steady-state named timer should schedule");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(baseline, memory_current_usage_bytes(),
                                     "named timer schedule should stay heap-stable after warmup");

    TEST_ASSERT_TRUE_MESSAGE(timer_cancel_named(key), "steady-state named timer should cancel");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(baseline, memory_current_usage_bytes(),
                                     "named timer cancel should stay heap-stable after warmup");
}
#endif

TEST(test_event_loop_ingress_call_coalesces_duplicate_kind_and_key_payloads) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-coalesce-marker (atom [])) "
        "  (fn event-loop-ingress-coalesce-task [event] "
        "    (swap! event-loop-ingress-coalesce-marker conj [(:kind event) (:key event)]) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    ID payload_a = eval_string("{:id :ball-vs-brick :phase :enter :kind :collision :key :brick-2001 :self 1003 :other 2001}",
                               g_test_eval_state);
    ID payload_b = eval_string("{:id :ball-vs-brick :phase :enter :kind :collision :key :brick-2001 :self 1003 :other 2001}",
                               g_test_eval_state);
    TEST_ASSERT_NOT_NULL(payload_a);
    TEST_ASSERT_NOT_NULL(payload_b);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_a), "first ingress call should enqueue");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_b), "duplicate ingress call should be coalesced");

    EventLoopIngressStats stats = {0};
    TEST_ASSERT_TRUE(event_loop_ingress_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1u, stats.accepted_count);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.rejected_count);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.pending_count);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "run_next should execute one coalesced ingress callback");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(),
                              "ingress queue should be empty after one coalesced callback");

    ID marker_ok = eval_string("(= @event-loop-ingress-coalesce-marker [[:collision :brick-2001]])",
                               g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, marker_ok);
}

TEST(test_event_loop_ingress_call_does_not_coalesce_when_key_differs) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-coalesce-key-marker (atom [])) "
        "  (fn event-loop-ingress-coalesce-key-task [event] "
        "    (swap! event-loop-ingress-coalesce-key-marker conj [(:kind event) (:key event)]) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    ID payload_a = eval_string("{:id :ball-vs-brick :phase :enter :kind :collision :key :brick-2001 :self 1003 :other 2001}",
                               g_test_eval_state);
    ID payload_b = eval_string("{:id :ball-vs-brick :phase :enter :kind :collision :key :brick-2002 :self 1003 :other 2002}",
                               g_test_eval_state);
    TEST_ASSERT_NOT_NULL(payload_a);
    TEST_ASSERT_NOT_NULL(payload_b);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_a), "first ingress call should enqueue");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_b), "different key should enqueue independently");

    EventLoopIngressStats stats = {0};
    TEST_ASSERT_TRUE(event_loop_ingress_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(2u, stats.accepted_count);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.rejected_count);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.pending_count);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "first run_next should execute first ingress callback");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "second run_next should execute second ingress callback");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(),
                              "ingress queue should be empty after both callbacks");

    ID marker_ok = eval_string("(= @event-loop-ingress-coalesce-key-marker [[:collision :brick-2001] [:collision :brick-2002]])",
                               g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, marker_ok);
}

TEST(test_event_loop_ingress_call_coalesces_duplicate_spatial_identity_without_key) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-spatial-coalesce-marker (atom [])) "
        "  (fn event-loop-ingress-spatial-coalesce-task [event] "
        "    (swap! event-loop-ingress-spatial-coalesce-marker "
        "           conj [(:id event) (:phase event) (:self event) (:other event)]) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    ID payload_a = eval_string("{:source :spatial :id :ball-vs-brick :phase :enter :self 1003 :other 2001 :snapshot-gen 1}",
                               g_test_eval_state);
    ID payload_b = eval_string("{:source :spatial :id :ball-vs-brick :phase :enter :self 1003 :other 2001 :snapshot-gen 2}",
                               g_test_eval_state);
    TEST_ASSERT_NOT_NULL(payload_a);
    TEST_ASSERT_NOT_NULL(payload_b);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_a), "first spatial ingress should enqueue");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_b), "duplicate spatial identity should coalesce");

    EventLoopIngressStats stats = {0};
    TEST_ASSERT_TRUE(event_loop_ingress_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1u, stats.accepted_count);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.pending_count);

    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_FALSE(event_loop_ingress_has_pending());

    ID marker_ok = eval_string("(= @event-loop-ingress-spatial-coalesce-marker [[:ball-vs-brick :enter 1003 2001]])",
                               g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, marker_ok);
}

TEST(test_event_loop_ingress_call_does_not_coalesce_spatial_events_for_different_other_entity) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-spatial-other-marker (atom [])) "
        "  (fn event-loop-ingress-spatial-other-task [event] "
        "    (swap! event-loop-ingress-spatial-other-marker "
        "           conj [(:id event) (:phase event) (:self event) (:other event)]) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    ID payload_a = eval_string("{:source :spatial :id :ball-vs-brick :phase :enter :self 1003 :other 2001}",
                               g_test_eval_state);
    ID payload_b = eval_string("{:source :spatial :id :ball-vs-brick :phase :enter :self 1003 :other 2002}",
                               g_test_eval_state);
    TEST_ASSERT_NOT_NULL(payload_a);
    TEST_ASSERT_NOT_NULL(payload_b);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_a), "first spatial ingress should enqueue");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_b), "different :other should enqueue independently");

    EventLoopIngressStats stats = {0};
    TEST_ASSERT_TRUE(event_loop_ingress_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(2u, stats.accepted_count);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.pending_count);

    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_FALSE(event_loop_ingress_has_pending());

    ID marker_ok = eval_string(
        "(= @event-loop-ingress-spatial-other-marker "
        "   [[:ball-vs-brick :enter 1003 2001] [:ball-vs-brick :enter 1003 2002]])",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, marker_ok);
}

TEST(test_event_loop_ingress_call_coalesces_duplicate_button_identity_without_key) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-ingress-button-coalesce-marker (atom [])) "
        "  (fn event-loop-ingress-button-coalesce-task [event] "
        "    (swap! event-loop-ingress-button-coalesce-marker "
        "           conj [(:source event) (:id event) (:kind event)]) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    ID payload_a = eval_string("{:source :button :id :left :kind :button/down :pin 14 :value 0}",
                               g_test_eval_state);
    ID payload_b = eval_string("{:source :button :id :left :kind :button/down :pin 14 :value 0}",
                               g_test_eval_state);
    TEST_ASSERT_NOT_NULL(payload_a);
    TEST_ASSERT_NOT_NULL(payload_b);

    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_a), "first button ingress should enqueue");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(fn, payload_b), "duplicate button identity should coalesce");

    EventLoopIngressStats stats = {0};
    TEST_ASSERT_TRUE(event_loop_ingress_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1u, stats.accepted_count);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.pending_count);

    TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));
    TEST_ASSERT_FALSE(event_loop_ingress_has_pending());

    ID marker_ok = eval_string(
        "(= @event-loop-ingress-button-coalesce-marker [[:button :left :button/down]])",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, marker_ok);
}

/* Target: 64 (raised to 1024); TODO: tearDown heap / run_next preface — lower toward 64 when possible. */
TEST(test_event_loop_run_next_prioritizes_older_task_queue_entries_before_new_ingress_calls, 1024) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID regular_fn = eval_string(
        "(do "
        "  (def event-loop-regular-marker (atom nil)) "
        "  (fn event-loop-regular-task [] "
        "    (reset! event-loop-regular-marker :regular) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(regular_fn);
    TEST_ASSERT_TRUE(TAG(regular_fn) == CLJ_FUNC || TAG(regular_fn) == CLJ_CLOSURE);

    ID ingress_fn = eval_string(
        "(do "
        "  (def event-loop-ingress-order-marker (atom nil)) "
        "  (fn event-loop-ingress-order-task [phase] "
        "    (reset! event-loop-ingress-order-marker phase) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(ingress_fn);
    TEST_ASSERT_TRUE(TAG(ingress_fn) == CLJ_FUNC || TAG(ingress_fn) == CLJ_CLOSURE);
    RETAIN(ingress_fn);

    event_loop_enqueue(regular_fn, NULL);
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(ingress_fn, intern_symbol_global(":enter")),
                             "ingress call enqueue should succeed");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "first run_next should execute some queued task");
    ID regular_marker = eval_string("@event-loop-regular-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":regular"), regular_marker);
    ID ingress_marker = eval_string("@event-loop-ingress-order-marker", g_test_eval_state);
    TEST_ASSERT_NULL_MESSAGE(ingress_marker,
                             "new ingress callback should still be pending while older task queue entry runs first");

    TEST_ASSERT_TRUE_MESSAGE(event_loop_run_next(NULL, g_test_eval_state),
                             "second run_next should execute the deferred ingress callback");
    ingress_marker = eval_string("@event-loop-ingress-order-marker", g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":enter"), ingress_marker);
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "queue should be empty after both tasks run");
}

TEST(test_event_loop_run_blocks_until_ingress_enqueue_and_executes_task) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID fn = eval_string(
        "(do "
        "  (def event-loop-run-ingress-marker (atom 0)) "
        "  (fn event-loop-run-ingress-task [] "
        "    (reset! event-loop-run-ingress-marker 1) "
        "    nil))",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_TRUE(TAG(fn) == CLJ_FUNC || TAG(fn) == CLJ_CLOSURE);

    EventLoopRunIngressWakeArgs args = {
        .fn = RETAIN(fn),
        .delay_us = 20000,
        .enqueue_ok = false,
    };
    pthread_t producer;
    int rc = pthread_create(&producer, NULL, event_loop_run_ingress_wake_thread, &args);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_create failed");

    uint64_t start_ms = event_loop_test_monotonic_ms();
    bool ran = event_loop_run(NULL, g_test_eval_state);
    uint64_t elapsed_ms = event_loop_test_monotonic_ms() - start_ms;

    rc = pthread_join(producer, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_join failed");

    RELEASE(args.fn);

    TEST_ASSERT_TRUE_MESSAGE(args.enqueue_ok, "producer should enqueue ingress task");
    TEST_ASSERT_TRUE_MESSAGE(ran, "event_loop_run should execute queued ingress task");
    TEST_ASSERT_TRUE_MESSAGE(elapsed_ms >= 8u, "event_loop_run should block until producer enqueue");

    ID marker = eval_string("@event-loop-run-ingress-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(marker));
}

TEST(test_event_loop_run_blocks_until_timer_due_and_executes_task) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID setup = eval_string(
        "(do "
        "  (def event-loop-run-timer-marker (atom 0)) "
        "  (schedule 20 (fn [] (reset! event-loop-run-timer-marker 1))))",
        g_test_eval_state);
    TEST_ASSERT_NULL(setup);

    uint64_t start_ms = event_loop_test_monotonic_ms();
    bool ran = event_loop_run(NULL, g_test_eval_state);
    uint64_t elapsed_ms = event_loop_test_monotonic_ms() - start_ms;

    TEST_ASSERT_TRUE_MESSAGE(ran, "event_loop_run should execute timer callback after deadline");
    TEST_ASSERT_TRUE_MESSAGE(elapsed_ms >= 8u, "event_loop_run should block until timer is due");

    ID marker = eval_string("@event-loop-run-timer-marker", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(marker);
    TEST_ASSERT_TRUE(is_fixnum(marker));
    TEST_ASSERT_EQUAL_INT(1, as_fixnum(marker));
}

TEST(test_event_loop_run_wake_interrupts_idle_wait_without_work) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    EventLoopRunWakeArgs args = {
        .delay_us = 20000,
    };
    pthread_t waker;
    int rc = pthread_create(&waker, NULL, event_loop_run_wake_thread, &args);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_create failed");

    uint64_t start_ms = event_loop_test_monotonic_ms();
    bool ran = event_loop_run(NULL, g_test_eval_state);
    uint64_t elapsed_ms = event_loop_test_monotonic_ms() - start_ms;

    rc = pthread_join(waker, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pthread_join failed");

    TEST_ASSERT_FALSE_MESSAGE(ran, "event_loop_run should return false when only externally woken");
    TEST_ASSERT_TRUE_MESSAGE(elapsed_ms >= 8u, "event_loop_run should block before wake");
    TEST_ASSERT_FALSE_MESSAGE(event_loop_has_pending_tasks(), "wake-only path should not enqueue work");
}

TEST(test_event_loop_run_blocking_thread_processes_breakout_input_ingress) {
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_eval_state, "eval state missing");
    event_loop_clear();

    ID setup = eval_string(
        "(do "
        "  (require 'tiny-breakout.runtime) "
        "  (tiny-breakout.runtime/start-runtime! nil) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_EQUAL_PTR(clj_true, setup);

    CljObject *input_fn = eval_string("tiny-breakout.runtime/apply-input!", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(input_fn);
    TEST_ASSERT_TRUE(TAG(input_fn) == CLJ_FUNC || TAG(input_fn) == CLJ_CLOSURE);

    ID input_arg = eval_string("{:right true}", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(input_arg);

    TEST_ASSERT_TRUE_MESSAGE(start_runloop_thread(g_test_eval_state),
                             "runloop thread should start");
    TEST_ASSERT_TRUE_MESSAGE(event_loop_enqueue_ingress_call(input_fn, input_arg),
                             "breakout input should enqueue through ingress");

    for (int i = 0; i < 40 && event_loop_has_pending_tasks(); i++) {
        usleep(5000);
    }

    stop_runloop_thread();

    ID paddle_x = eval_string("(:paddle-x @tiny-breakout.runtime/state*)", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(paddle_x);
    TEST_ASSERT_TRUE(is_fixnum(paddle_x));
    TEST_ASSERT_EQUAL_INT_MESSAGE(4,
                                  as_fixnum(paddle_x),
                                  "breakout input should be processed by the blocking runloop thread");
}
