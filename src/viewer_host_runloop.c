#include "viewer_host_runloop.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "eval.h"
#include "event_loop.h"
#include "platform.h"
#include "exception.h"

/* Host viewer only (this file is not linked on ESP32 — see TINYCLJ_FX_HOST_SOURCES).
 * Must exceed desktop EVAL_STACK_LIMIT with headroom: deep Clojure eval on this thread
 * Event-loop tasks clear the eval C-stack base (see event_loop_run_next); keep pthread stack
 * large enough for deep Clojure eval without OS stack faults. */
#define VIEWER_RUNLOOP_STACK_SIZE (8u * 1024u * 1024u)

#define VIEWER_RUNLOOP_STALL_THRESHOLD_NS (5ull * 1000ull * 1000ull * 1000ull)

ViewerRunloopThread g_runloop_thread = {0};

static uint64_t viewer_runloop_monotonic_now_ns(void) {
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Resets the host runloop liveness counters.
 *
 * @param void
 * @return void
 */
void viewer_runloop_liveness_reset(void) {
    atomic_store_explicit(&g_runloop_thread.last_tick_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_runloop_thread.iteration_count, 0u, memory_order_relaxed);
}

/**
 * @brief Records synthetic runloop progress for tests.
 *
 * @param now_ns Monotonic timestamp to publish as the latest runloop tick.
 * @return void
 */
void viewer_runloop_liveness_note_progress_for_tests(uint64_t now_ns) {
    atomic_store_explicit(&g_runloop_thread.last_tick_ns, now_ns, memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&g_runloop_thread.iteration_count, 1u, memory_order_relaxed);
}

/**
 * @brief Returns a cheap host-side liveness snapshot for the runloop thread.
 *
 * @param now_ns Current monotonic timestamp in nanoseconds.
 * @return Snapshot containing last tick, iteration count, age, and classified state.
 */
ViewerRunloopLivenessSnapshot viewer_runloop_liveness_snapshot(uint64_t now_ns) {
    ViewerRunloopLivenessSnapshot snapshot = {0};
    snapshot.last_tick_ns = atomic_load_explicit(&g_runloop_thread.last_tick_ns, memory_order_relaxed);
    snapshot.iteration_count = atomic_load_explicit(&g_runloop_thread.iteration_count, memory_order_relaxed);
    snapshot.age_ns = (snapshot.last_tick_ns > 0u && now_ns > snapshot.last_tick_ns)
                          ? (now_ns - snapshot.last_tick_ns)
                          : 0u;
    snapshot.state = (snapshot.last_tick_ns > 0u && snapshot.age_ns >= VIEWER_RUNLOOP_STALL_THRESHOLD_NS)
                         ? VIEWER_RUNLOOP_LIVENESS_STALLED
                         : VIEWER_RUNLOOP_LIVENESS_HEALTHY;
    return snapshot;
}

bool viewer_drain_one_runloop_task(EvalState *st) {
    if (!st) {
        return false;
    }
    bool ran = false;
    TRY {
        ran = event_loop_run_next(NULL, st);
    } CATCH(ex) {
        if (ex) {
            fprintf(stderr, "[viewer-runloop] uncaught exception while draining runloop task\n");
            print_exception(ex);
            fflush(stderr);
        }
        ran = false;
    } END_TRY
    return ran;
}

static void *viewer_runloop_thread_main(void *arg) {
    EvalState *st = (EvalState *)arg;
    if (!st) {
        return NULL;
    }
    char runloop_thread_stack_anchor;
    eval_bind_task_stack_anchor(&runloop_thread_stack_anchor);
    subjective_c_register_interpreter_thread();
    while (atomic_load_explicit(&g_runloop_thread.running, memory_order_acquire)) {
        uint64_t now_ns = viewer_runloop_monotonic_now_ns();
        atomic_store_explicit(&g_runloop_thread.last_tick_ns, now_ns, memory_order_relaxed);
        (void)atomic_fetch_add_explicit(&g_runloop_thread.iteration_count, 1u, memory_order_relaxed);
        if (viewer_drain_one_runloop_task(st)) {
            continue;
        }
        int timeout_ms = event_loop_time_until_next_timer_ms();
        if (timeout_ms < 0 || timeout_ms > 1) {
            timeout_ms = 1;
        }
        platform_runloop_run_once((unsigned int)timeout_ms);
    }
    subjective_c_clear_interpreter_thread();
    return NULL;
}

bool start_runloop_thread(EvalState *st) {
    if (!st) {
        return false;
    }
    if (g_runloop_thread.started) {
        return true;
    }
    viewer_runloop_liveness_reset();
    atomic_store_explicit(&g_runloop_thread.last_tick_ns, viewer_runloop_monotonic_now_ns(), memory_order_relaxed);
    g_runloop_thread.eval_state = st;
    atomic_store_explicit(&g_runloop_thread.running, true, memory_order_release);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, VIEWER_RUNLOOP_STACK_SIZE);
    int rc = pthread_create(&g_runloop_thread.thread, &attr, viewer_runloop_thread_main, st);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        atomic_store_explicit(&g_runloop_thread.running, false, memory_order_release);
        g_runloop_thread.eval_state = NULL;
        viewer_runloop_liveness_reset();
        subjective_c_clear_interpreter_thread();
        return false;
    }
    g_runloop_thread.started = true;
    return true;
}

void stop_runloop_thread(void) {
    if (!g_runloop_thread.started) {
        return;
    }
    atomic_store_explicit(&g_runloop_thread.running, false, memory_order_release);
    (void)pthread_join(g_runloop_thread.thread, NULL);
    subjective_c_clear_interpreter_thread();
    g_runloop_thread.started = false;
    memset(&g_runloop_thread.thread, 0, sizeof(pthread_t));
    g_runloop_thread.eval_state = NULL;
    viewer_runloop_liveness_reset();
}
