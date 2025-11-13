#ifndef TINY_CLJ_EVENT_LOOP_H
#define TINY_CLJ_EVENT_LOOP_H

#include "object.h"
#include "map.h"  // Must be included before namespace.h (map.h -> value.h -> symbol.h)
#include "namespace.h"
#include <stdbool.h>

// Initialize event loop (idempotent)
void event_loop_init(void);

// Clear event loop queue (for test isolation)
void event_loop_clear(void);

// Enqueue go task for later execution. Takes ownership via RETAIN; releases after run.
void event_loop_enqueue(CljObject *fn_zero_arity, CljMap *result_channel);

// Run next enqueued task. Returns true if a task was executed, false if queue empty.
bool event_loop_run_next(CljMap *env, EvalState *st);

// Timer API
// Enqueue a timer task for execution after delay_ms milliseconds
// If periodic is true, the task will be re-scheduled every period_ms milliseconds
// Takes ownership via RETAIN; releases after run.
// Returns unique timer ID (int32_t) or 0 on error.
int32_t timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms);
// Cancel a timer by ID. Returns true if timer was found and cancelled, false otherwise.
bool timer_cancel(int32_t timer_id);

#endif





