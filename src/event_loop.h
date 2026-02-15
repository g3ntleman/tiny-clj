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
void event_loop_enqueue(CljObject *fn_zero_arity, CljTransientMap *result_channel);

// Run next enqueued task. Returns true if a task was executed, false if queue empty.
bool event_loop_run_next(CljPersistentMap *env, EvalState *st);

// Returns true when the normal task queue currently has pending entries.
bool event_loop_has_pending_tasks(void);

// Returns milliseconds until the next timer is due, 0 when overdue/ready, or -1 when no timers exist.
int event_loop_time_until_next_timer_ms(void);

// Timer API
// Enqueue a timer task for execution after delay_ms milliseconds
// If periodic is true, the task will be re-scheduled every period_ms milliseconds
// Takes ownership via RETAIN; releases after run.
// Returns unique timer ID (int) or 0 on error.
int timer_enqueue(CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms);
// Cancel a timer by ID. Returns true if timer was found and cancelled, false otherwise.
bool timer_cancel(int timer_id);
// Create or update a timer by stable key (compared via clj_equal). Returns timer ID or 0 on error.
int timer_upsert_named(ID key, CljObject *fn_zero_arity, int64_t delay_ms, bool periodic, int64_t period_ms);
// Cancel a timer by stable key (compared via clj_equal). Returns true if timer was found and cancelled, false otherwise.
bool timer_cancel_named(ID key);

#endif



