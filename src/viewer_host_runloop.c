#include "viewer_host_runloop.h"

#include <stdio.h>
#include <string.h>

#include "event_loop.h"
#include "platform.h"
#include "exception.h"

ViewerRunloopThread g_runloop_thread = {0};

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
    while (atomic_load_explicit(&g_runloop_thread.running, memory_order_acquire)) {
        if (viewer_drain_one_runloop_task(st)) {
            continue;
        }
        int timeout_ms = event_loop_time_until_next_timer_ms();
        if (timeout_ms < 0 || timeout_ms > 1) {
            timeout_ms = 1;
        }
        platform_runloop_run_once((unsigned int)timeout_ms);
    }
    return NULL;
}

bool start_runloop_thread(EvalState *st) {
    if (!st) {
        return false;
    }
    if (g_runloop_thread.started) {
        return true;
    }
    g_runloop_thread.eval_state = st;
    atomic_store_explicit(&g_runloop_thread.running, true, memory_order_release);
    if (pthread_create(&g_runloop_thread.thread, NULL, viewer_runloop_thread_main, st) != 0) {
        atomic_store_explicit(&g_runloop_thread.running, false, memory_order_release);
        g_runloop_thread.eval_state = NULL;
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
    g_runloop_thread.started = false;
    memset(&g_runloop_thread.thread, 0, sizeof(pthread_t));
    g_runloop_thread.eval_state = NULL;
}
