#ifndef TINY_CLJ_VIEWER_HOST_RUNLOOP_H
#define TINY_CLJ_VIEWER_HOST_RUNLOOP_H

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#include "namespace.h"

typedef enum {
    VIEWER_RUNLOOP_LIVENESS_HEALTHY = 0,
    VIEWER_RUNLOOP_LIVENESS_STALLED = 1,
} ViewerRunloopLivenessState;

typedef struct {
    uint64_t last_tick_ns;
    uint64_t iteration_count;
    uint64_t age_ns;
    ViewerRunloopLivenessState state;
} ViewerRunloopLivenessSnapshot;

typedef struct {
    pthread_t thread;
    atomic_bool running;
    atomic_uint_fast64_t last_tick_ns;
    atomic_uint_fast64_t iteration_count;
    bool started;
    EvalState *eval_state;
} ViewerRunloopThread;

extern ViewerRunloopThread g_runloop_thread;

bool viewer_drain_one_runloop_task(EvalState *st);
void viewer_runloop_liveness_reset(void);
void viewer_runloop_liveness_note_progress_for_tests(uint64_t now_ns);
ViewerRunloopLivenessSnapshot viewer_runloop_liveness_snapshot(uint64_t now_ns);
bool start_runloop_thread(EvalState *st);
void stop_runloop_thread(void);

#endif /* TINY_CLJ_VIEWER_HOST_RUNLOOP_H */
