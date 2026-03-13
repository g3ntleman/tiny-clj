#ifndef TINY_CLJ_SOUND_TICK_SCHEDULER_H
#define TINY_CLJ_SOUND_TICK_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t tick_period_ns;
    uint64_t next_deadline_ns;
    uint32_t max_catchup_ticks;
    bool running;
} SoundTickScheduler;

void sound_tick_scheduler_init(SoundTickScheduler *scheduler, uint64_t tick_period_ns, uint32_t max_catchup_ticks);
void sound_tick_scheduler_start(SoundTickScheduler *scheduler, uint64_t now_ns);
void sound_tick_scheduler_stop(SoundTickScheduler *scheduler);
uint32_t sound_tick_scheduler_ticks_due(SoundTickScheduler *scheduler, uint64_t now_ns, uint32_t *out_skipped_ticks);

#endif /* TINY_CLJ_SOUND_TICK_SCHEDULER_H */
