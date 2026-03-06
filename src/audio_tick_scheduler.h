#ifndef TINY_CLJ_AUDIO_TICK_SCHEDULER_H
#define TINY_CLJ_AUDIO_TICK_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t tick_period_ns;
    uint64_t next_deadline_ns;
    uint32_t max_catchup_ticks;
    bool running;
} AudioTickScheduler;

void audio_tick_scheduler_init(AudioTickScheduler *scheduler, uint64_t tick_period_ns, uint32_t max_catchup_ticks);
void audio_tick_scheduler_start(AudioTickScheduler *scheduler, uint64_t now_ns);
void audio_tick_scheduler_stop(AudioTickScheduler *scheduler);
uint32_t audio_tick_scheduler_ticks_due(AudioTickScheduler *scheduler, uint64_t now_ns, uint32_t *out_skipped_ticks);

#endif
