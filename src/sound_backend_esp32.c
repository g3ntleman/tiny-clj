/*
 * Sound backend for ESP32: LEDC PWM output + esp_timer 1ms tick.
 *
 * Each voice gets its own LEDC timer (for independent frequency) and channel.
 * The 1ms tick is driven by esp_timer and runs on-demand (start/stop).
 */

#ifdef ESP32_BUILD

#include "gpio.h"
#include "sound_engine.h"
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
    uint16_t      retained_freq_hz;
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
static _Atomic uint32_t g_sound_scheduled_ticks = 0u;

/* Keep callback work within one tick budget. */
#define SOUND_TICK_BUDGET_US (VG_SOUND_TICK_MS * 1000)

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
    voice->retained_freq_hz = 0;
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

static uint64_t sound_backend_ticks_to_delay_us(uint32_t ticks) {
    if (ticks == 0u) {
        return 1u;
    }
    return (uint64_t)ticks * (uint64_t)VG_SOUND_TICK_MS * 1000ull;
}

static void sound_backend_schedule_next_timer(uint32_t ticks) {
    if (!g_sound_timer || !sound_engine_tick_is_running()) {
        return;
    }
    atomic_store_explicit(&g_sound_scheduled_ticks, ticks, memory_order_release);
    (void)esp_timer_stop(g_sound_timer);
    (void)esp_timer_start_once(g_sound_timer, sound_backend_ticks_to_delay_us(ticks));
}

static void sound_timer_callback(void *arg) {
    (void)arg;

    /* Real-time callback rules:
     * - no allocation
     * - no locks
     * - no I/O/logging
     * - no VM/eval calls
     * - no retain/release or event payload construction
     *
     * Finished notifications stay POD-only here; the interpreter thread drains
     * them later via sound_engine_drain_finished_notifications().
     */
    if (atomic_exchange_explicit(&g_sound_tick_in_callback, true, memory_order_acq_rel)) {
        g_sound_engine.telemetry.tick_overrun_count++;
        return;
    }

    int64_t start_us = esp_timer_get_time();
    uint32_t due = atomic_exchange_explicit(&g_sound_scheduled_ticks, 0u, memory_order_acq_rel);
    for (uint32_t i = 0; i < due; i++) {
        sound_engine_tick();
    }
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    int64_t budget_us = (int64_t)SOUND_TICK_BUDGET_US * (due > 0u ? (int64_t)due : 1ll);
    if (elapsed_us > budget_us) {
        g_sound_engine.telemetry.tick_overrun_count++;
    }
    if (sound_engine_tick_is_running()) {
        sound_backend_schedule_next_timer(sound_engine_ticks_until_deadline());
    }
    atomic_store_explicit(&g_sound_tick_in_callback, false, memory_order_release);
}

/* ========================================================================= */
/* Backend API                                                               */
/* ========================================================================= */

void sound_backend_init(int voice_count) {
    if (voice_count > PWM_VOICE_CAP) voice_count = PWM_VOICE_CAP;
    if (voice_count < 0) voice_count = 0;
    for (int i = 0; i < PWM_VOICE_CAP; i++) {
        sound_backend_reset_voice_cache(&g_pwm_voices[i]);
    }
    atomic_store_explicit(&g_sound_scheduled_ticks, 0u, memory_order_release);
}

void sound_backend_shutdown(void) {
    sound_tick_stop();
    sound_backend_silence_pwm_voices();
    if (g_sound_timer) {
        esp_timer_delete(g_sound_timer);
        g_sound_timer = NULL;
    }
}

void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume, bool retrigger) {
    if (voice_index < 0 || voice_index >= sound_backend_active_voice_count()) return;
    PwmVoice *v = &g_pwm_voices[voice_index];

    /* Only update hardware on actual change */
    uint8_t duty8 = (freq_hz > 0) ? (volume >> 1) : 0; /* scale 0..255 -> 0..127 for 50% max duty */
    if (!retrigger && freq_hz == v->last_freq_hz && duty8 == v->last_duty) return;

    if (retrigger) {
        (void)gpio_pwm_stop(v->pin);
        v->last_freq_hz = 0;
        v->last_duty = 0;
    }

    if (freq_hz > 0 && duty8 > 0) {
        int32_t gpio_duty = ((int32_t)duty8 * 255 + 63) / 127;
        (void)gpio_pwm_start_or_update(v->pin, freq_hz, gpio_duty);
        v->retained_freq_hz = freq_hz;
    } else if (v->retained_freq_hz > 0) {
        (void)gpio_pwm_start_or_update(v->pin, v->retained_freq_hz, 0);
    } else {
        (void)gpio_pwm_stop(v->pin);
    }
    v->last_freq_hz = freq_hz;
    v->last_duty = duty8;
}

bool sound_backend_keepalive_active(void) {
    return false;
}

/* ========================================================================= */
/* Tick lifecycle (on-demand)                                                */
/* ========================================================================= */

void sound_tick_start(void) {
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

    (void)sound_engine_tick_mark_running();
    sound_backend_schedule_next_timer(0u);
}

void sound_tick_stop(void) {
    if (!sound_engine_tick_is_running()) return;
    if (g_sound_timer) {
        esp_timer_stop(g_sound_timer);
    }
    atomic_store_explicit(&g_sound_scheduled_ticks, 0u, memory_order_release);
    sound_engine_tick_mark_stopped();
    sound_backend_silence_pwm_voices();
}

void sound_tick_sleep(void) {
    if (!sound_engine_tick_is_running()) {
        return;
    }
    if (g_sound_timer) {
        esp_timer_stop(g_sound_timer);
    }
    atomic_store_explicit(&g_sound_scheduled_ticks, 0u, memory_order_release);
    sound_engine_tick_mark_stopped();
}

void sound_tick_kick(void) {
    if (!sound_engine_tick_is_running()) {
        sound_tick_start();
        return;
    }
    sound_backend_schedule_next_timer(0u);
}

bool sound_backend_host_get_status(SoundHostStatus *out) {
    if (!out) return false;

    out->backend_available = true;
    out->sound_running = sound_engine_tick_is_running();
    out->tick_enabled = sound_engine_tick_is_running();
    out->tick_thread_running = false;
    out->voice_count = g_sound_engine.voice_count;
    return true;
}

#endif /* ESP32_BUILD */
