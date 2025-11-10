#ifndef TINY_CLJ_EVENT_LOOP_H
#define TINY_CLJ_EVENT_LOOP_H

#include "object.h"
#include "namespace.h"
#include "map.h"
#include <stdbool.h>

// Initialize event loop (idempotent)
void event_loop_init(void);

// Enqueue go task for later execution. Takes ownership via RETAIN; releases after run.
void event_loop_enqueue(CljObject *fn_zero_arity, CljMap *result_channel);

// Run next enqueued task. Returns true if a task was executed, false if queue empty.
bool event_loop_run_next(CljMap *env, EvalState *st);

#endif





