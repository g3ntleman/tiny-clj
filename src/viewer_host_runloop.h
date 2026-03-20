#ifndef TINY_CLJ_VIEWER_HOST_RUNLOOP_H
#define TINY_CLJ_VIEWER_HOST_RUNLOOP_H

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#include "namespace.h"

typedef struct {
    pthread_t thread;
    atomic_bool running;
    bool started;
    EvalState *eval_state;
} ViewerRunloopThread;

extern ViewerRunloopThread g_runloop_thread;

bool viewer_drain_one_runloop_task(EvalState *st);
bool start_runloop_thread(EvalState *st);
void stop_runloop_thread(void);

#endif /* TINY_CLJ_VIEWER_HOST_RUNLOOP_H */
