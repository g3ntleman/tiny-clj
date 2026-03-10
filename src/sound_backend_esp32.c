/*
 * Sound backend for ESP32: LEDC PWM output + esp_timer 1ms tick.
 *
 * Each voice gets its own LEDC timer (for independent frequency) and channel.
 * The 1ms tick is driven by esp_timer and runs on-demand (start/stop).
 */

#ifdef ESP32_BUILD

#include "gpio.h"
#include "sound_engine.h"
#include "sound_tick_scheduler.h"
#include "esp32-idf/main/vector_handheld_config.h"

#include "esp_timer.h"
#include <stdatomic.h>
#include <string.h>

/* ========================================================================= */
/* PWM voice mapping                                                         */
/* ========================================================================= */

typedef struct {
    int           pin;
    uint16_t      last_freq_hz;
    uint8_t       last_duty;
} PwmVoice;

/* Board-default mapping (from vector_handheld_config.h).
 * Extensible to N voices if more pins/timers are configured. */
static PwmVoice g_pwm_voices[] = {
    { .pin = VG_PIN_PIEZO_1 },
    { .pin = VG_PIN_PIEZO_2 },
};

#define PWM_VOICE_CAP (int)(sizeof(g_pwm_voices) / sizeof(g_pwm_voices[0]))

/* ========================================================================= */
/* esp_timer handle                                                          */
/* ========================================================================= */

static esp_timer_handle_t g_sound_timer = NULL;
static _Atomic bool g_sound_tick_in_callback = false;
static SoundTickScheduler g_sound_tick_scheduler;

/* Keep callback work within one tick budget. */
#define SOUND_TICK_BUDGET_US (VG_SOUND_TICK_MS * 1000)
#define SOUND_TICK_PERIOD_NS ((uint64_t)VG_SOUND_TICK_MS * 1000000ull)
#define SOUND_TICK_MAX_CATCHUP_TICKS 4u

static int sound_backend_active_voice_count(void) {
    int voice_count = g_sound_engine.voice_count;
    if (voice_count < 0) voice_count = 0;
    if (voice_count > PWM_VOICE_CAP) voice_count = PWM_VOICE_CAP;
    return voice_count;
}

static void sound_backend_reset_voice_cache(PwmVoice *voice) {
    if (!voice) {
        return;
    }
    voice->last_freq_hz = 0;
    voice->last_duty = 0;
}

static void sound_backend_stop_pwm_voice(PwmVoice *voice) {
    if (!voice) {
        return;
    }
    (void)gpio_pwm_stop(voice->pin);
    sound_backend_reset_voice_cache(voice);
}

static void sound_backend_silence_pwm_voices(void) {
    int voice_count = sound_backend_active_voice_count();
    for (int i = 0; i < voice_count; i++) {
        sound_backend_stop_pwm_voice(&g_pwm_voices[i]);
    }
}

static void sound_timer_callback(void *arg) {
    (void)arg;

    /* Real-time callback rules:
     * - no allocation
     * - no locks
     * - no I/O/logging
     * - no VM/eval calls
     */
    if (atomic_exchange_explicit(&g_sound_tick_in_callback, true, memory_order_acq_rel)) {
        g_sound_engine.telemetry.tick_overrun_count++;
        return;
    }

    int64_t start_us = esp_timer_get_time();
    uint32_t skipped_ticks = 0u;
    uint32_t due = sound_tick_scheduler_ticks_due(&g_sound_tick_scheduler,
                                                  (uint64_t)start_us * 1000ull,
                                                  &skipped_ticks);
    if (skipped_ticks > 0u) {
        g_sound_engine.telemetry.tick_overrun_count += skipped_ticks;
    }
    for (uint32_t i = 0; i < due; i++) {
        sound_engine_tick();
    }
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    int64_t budget_us = (int64_t)SOUND_TICK_BUDGET_US * (due > 0u ? (int64_t)due : 1ll);
    if (elapsed_us > budget_us) {
        g_sound_engine.telemetry.tick_overrun_count++;
    }
    atomic_store_explicit(&g_sound_tick_in_callback, false, memory_order_release);
}

/* ========================================================================= */
/* Backend API                                                               */
/* ========================================================================= */

void sound_backend_init(int voice_count) {
    if (voice_count > PWM_VOICE_CAP) voice_count = PWM_VOICE_CAP;
    if (voice_count < 0) voice_count = 0;
    sound_tick_scheduler_init(&g_sound_tick_scheduler, SOUND_TICK_PERIOD_NS, SOUND_TICK_MAX_CATCHUP_TICKS);

    for (int i = 0; i < PWM_VOICE_CAP; i++) {
        sound_backend_reset_voice_cache(&g_pwm_voices[i]);
    }
}

void sound_backend_shutdown(void) {
    sound_tick_stop();
    sound_backend_silence_pwm_voices();
    if (g_sound_timer) {
        esp_timer_delete(g_sound_timer);
        g_sound_timer = NULL;
    }
}

void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume) {
    if (voice_index < 0 || voice_index >= sound_backend_active_voice_count()) return;
    PwmVoice *v = &g_pwm_voices[voice_index];

    /* Only update hardware on actual change */
    uint8_t duty8 = (freq_hz > 0) ? (volume >> 1) : 0; /* scale 0..255 -> 0..127 for 50% max duty */
    if (freq_hz == v->last_freq_hz && duty8 == v->last_duty) return;

    if (freq_hz == 0 || duty8 == 0) {
        (void)gpio_pwm_stop(v->pin);
    } else {
        int32_t gpio_duty = ((int32_t)duty8 * 255 + 63) / 127;
        (void)gpio_pwm_start_or_update(v->pin, freq_hz, gpio_duty);
    }
    v->last_freq_hz = freq_hz;
    v->last_duty = duty8;
}

/* ========================================================================= */
/* Tick lifecycle (on-demand)                                                */
/* ========================================================================= */

void sound_tick_start(void) {
    if (g_sound_engine.tick_running) return;

    if (!g_sound_timer) {
        esp_timer_create_args_t args = {
            .callback = sound_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "sound_tick",
        };
        if (esp_timer_create(&args, &g_sound_timer) != ESP_OK) {
            return;
        }
    }

    sound_tick_scheduler_start(&g_sound_tick_scheduler, (uint64_t)esp_timer_get_time() * 1000ull);
    if (esp_timer_start_periodic(g_sound_timer, VG_SOUND_TICK_MS * 1000) != ESP_OK) { /* us */
        sound_tick_scheduler_stop(&g_sound_tick_scheduler);
        return;
    }
    g_sound_engine.tick_running = true;
}

void sound_tick_stop(void) {
    if (!g_sound_engine.tick_running) return;
    if (g_sound_timer) {
        esp_timer_stop(g_sound_timer);
    }
    sound_tick_scheduler_stop(&g_sound_tick_scheduler);
    g_sound_engine.tick_running = false;
    sound_backend_silence_pwm_voices();
}

bool sound_backend_host_get_status(SoundHostStatus *out) {
    if (!out) return false;

    out->backend_available = true;
    out->sound_running = g_sound_engine.tick_running;
    out->tick_enabled = g_sound_engine.tick_running;
    out->tick_thread_running = false;
    out->voice_count = g_sound_engine.voice_count;
    return true;
}

#endif /* ESP32_BUILD */
