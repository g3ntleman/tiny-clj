/**
 * @brief Generic deadline-based scheduler for audio engine ticks.
 *
 * The scheduler is platform-neutral: callers provide the current monotonic
 * time in nanoseconds and the backend decides how to sleep or wake. When the
 * caller wakes up late, the scheduler can request multiple ticks in one wakeup
 * and reports how many overdue ticks were skipped beyond the catch-up limit.
 */

#include "audio_tick_scheduler.h"

void audio_tick_scheduler_init(AudioTickScheduler *scheduler, uint64_t tick_period_ns, uint32_t max_catchup_ticks) {
    if (!scheduler) {
        return;
    }
    scheduler->tick_period_ns = tick_period_ns > 0u ? tick_period_ns : 1000000u;
    scheduler->next_deadline_ns = 0u;
    scheduler->max_catchup_ticks = max_catchup_ticks > 0u ? max_catchup_ticks : 1u;
    scheduler->running = false;
}

void audio_tick_scheduler_start(AudioTickScheduler *scheduler, uint64_t now_ns) {
    if (!scheduler) {
        return;
    }
    scheduler->running = true;
    scheduler->next_deadline_ns = now_ns + scheduler->tick_period_ns;
}

void audio_tick_scheduler_stop(AudioTickScheduler *scheduler) {
    if (!scheduler) {
        return;
    }
    scheduler->running = false;
    scheduler->next_deadline_ns = 0u;
}

uint32_t audio_tick_scheduler_ticks_due(AudioTickScheduler *scheduler, uint64_t now_ns, uint32_t *out_skipped_ticks) {
    if (out_skipped_ticks) {
        *out_skipped_ticks = 0u;
    }
    if (!scheduler || !scheduler->running || scheduler->tick_period_ns == 0u) {
        return 0u;
    }
    if (now_ns < scheduler->next_deadline_ns) {
        return 0u;
    }

    uint64_t overdue_ns = now_ns - scheduler->next_deadline_ns;
    uint64_t raw_due64 = 1u + (overdue_ns / scheduler->tick_period_ns);
    uint32_t raw_due = raw_due64 > UINT32_MAX ? UINT32_MAX : (uint32_t)raw_due64;
    uint32_t due = raw_due;
    if (due > scheduler->max_catchup_ticks) {
        due = scheduler->max_catchup_ticks;
        if (out_skipped_ticks) {
            *out_skipped_ticks = raw_due - due;
        }
    }

    scheduler->next_deadline_ns += ((uint64_t)raw_due * scheduler->tick_period_ns);
    if (scheduler->next_deadline_ns <= now_ns) {
        scheduler->next_deadline_ns = now_ns + scheduler->tick_period_ns;
    }
    return due;
}
