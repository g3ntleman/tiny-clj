/*
 * Audio backend for ESP32: LEDC PWM output + esp_timer 1ms tick.
 *
 * Each voice gets its own LEDC timer (for independent frequency) and channel.
 * The 1ms tick is driven by esp_timer and runs on-demand (start/stop).
 */

#ifdef ESP32_BUILD

#include "audio_engine.h"
#include "esp32-idf/main/vector_handheld_config.h"

#include "driver/ledc.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <string.h>

/* ========================================================================= */
/* LEDC voice mapping                                                        */
/* ========================================================================= */

typedef struct {
    int           pin;
    ledc_timer_t  timer;
    ledc_channel_t channel;
    bool          initialized;
    uint16_t      last_freq_hz;
    uint8_t       last_duty;
} LedcVoice;

/* Board-default mapping (from vector_handheld_config.h).
 * Extensible to N voices if more pins/timers are configured. */
static LedcVoice g_ledc_voices[] = {
    { .pin = VG_PIN_PIEZO_1, .timer = LEDC_TIMER_0, .channel = LEDC_CHANNEL_0 },
    { .pin = VG_PIN_PIEZO_2, .timer = LEDC_TIMER_1, .channel = LEDC_CHANNEL_1 },
};

#define LEDC_VOICE_CAP (int)(sizeof(g_ledc_voices) / sizeof(g_ledc_voices[0]))

/* ========================================================================= */
/* esp_timer handle                                                          */
/* ========================================================================= */

static esp_timer_handle_t g_audio_timer = NULL;
static _Atomic bool g_audio_tick_in_callback = false;

/* Keep callback work within one tick budget. */
#define AUDIO_TICK_BUDGET_US (VG_AUDIO_TICK_MS * 1000)

static bool ledc_mapping_valid(int voice_count) {
    for (int i = 0; i < voice_count; i++) {
        for (int j = i + 1; j < voice_count; j++) {
            if (g_ledc_voices[i].channel == g_ledc_voices[j].channel) {
                return false;
            }
            if (g_ledc_voices[i].timer == g_ledc_voices[j].timer) {
                /* Same timer would couple frequencies across voices. */
                return false;
            }
        }
    }
    return true;
}

static void audio_timer_callback(void *arg) {
    (void)arg;

    /* Real-time callback rules:
     * - no allocation
     * - no locks
     * - no I/O/logging
     * - no VM/eval calls
     */
    if (atomic_exchange_explicit(&g_audio_tick_in_callback, true, memory_order_acq_rel)) {
        g_audio_engine.telemetry.tick_overrun_count++;
        return;
    }

    int64_t start_us = esp_timer_get_time();
    audio_engine_tick();
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    if (elapsed_us > AUDIO_TICK_BUDGET_US) {
        g_audio_engine.telemetry.tick_overrun_count++;
    }
    atomic_store_explicit(&g_audio_tick_in_callback, false, memory_order_release);
}

/* ========================================================================= */
/* Backend API                                                               */
/* ========================================================================= */

void audio_backend_init(int voice_count) {
    if (voice_count > LEDC_VOICE_CAP) voice_count = LEDC_VOICE_CAP;
    if (voice_count < 0) voice_count = 0;

    if (!ledc_mapping_valid(voice_count)) {
        return;
    }

    for (int i = 0; i < voice_count; i++) {
        LedcVoice *v = &g_ledc_voices[i];

        ledc_timer_config_t timer_cfg = {
            .speed_mode      = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num       = v->timer,
            .freq_hz         = 440,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&timer_cfg);

        ledc_channel_config_t ch_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = v->channel,
            .timer_sel  = v->timer,
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = v->pin,
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&ch_cfg);
        v->initialized = true;
        v->last_freq_hz = 0;
        v->last_duty = 0;
    }
}

void audio_backend_shutdown(void) {
    for (int i = 0; i < LEDC_VOICE_CAP; i++) {
        if (g_ledc_voices[i].initialized) {
            ledc_stop(LEDC_LOW_SPEED_MODE, g_ledc_voices[i].channel, 0);
            g_ledc_voices[i].initialized = false;
        }
    }
    if (g_audio_timer) {
        esp_timer_stop(g_audio_timer);
        esp_timer_delete(g_audio_timer);
        g_audio_timer = NULL;
    }
}

void audio_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume) {
    if (voice_index < 0 || voice_index >= LEDC_VOICE_CAP) return;
    LedcVoice *v = &g_ledc_voices[voice_index];
    if (!v->initialized) return;

    /* Only update hardware on actual change */
    uint8_t duty8 = (freq_hz > 0) ? (volume >> 1) : 0; /* scale 0..255 -> 0..127 for 50% max duty */
    if (freq_hz == v->last_freq_hz && duty8 == v->last_duty) return;

    if (freq_hz == 0 || duty8 == 0) {
        ledc_stop(LEDC_LOW_SPEED_MODE, v->channel, 0);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, v->timer, freq_hz);
        uint32_t duty10 = ((uint32_t)duty8 * 1023) / 127;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, v->channel, duty10);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, v->channel);
    }
    v->last_freq_hz = freq_hz;
    v->last_duty = duty8;
}

/* ========================================================================= */
/* Tick lifecycle (on-demand)                                                */
/* ========================================================================= */

void audio_tick_start(void) {
    if (g_audio_engine.tick_running) return;

    if (!g_audio_timer) {
        esp_timer_create_args_t args = {
            .callback = audio_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "audio_tick",
        };
        if (esp_timer_create(&args, &g_audio_timer) != ESP_OK) {
            return;
        }
    }

    if (esp_timer_start_periodic(g_audio_timer, VG_AUDIO_TICK_MS * 1000) != ESP_OK) { /* us */
        return;
    }
    g_audio_engine.tick_running = true;
}

void audio_tick_stop(void) {
    if (!g_audio_engine.tick_running) return;
    if (g_audio_timer) {
        esp_timer_stop(g_audio_timer);
    }
    g_audio_engine.tick_running = false;

    /* Silence all voices on stop */
    for (int i = 0; i < LEDC_VOICE_CAP; i++) {
        if (g_ledc_voices[i].initialized) {
            ledc_stop(LEDC_LOW_SPEED_MODE, g_ledc_voices[i].channel, 0);
            g_ledc_voices[i].last_freq_hz = 0;
            g_ledc_voices[i].last_duty = 0;
        }
    }
}

bool audio_backend_host_get_status(AudioHostStatus *out) {
    if (!out) return false;

    out->backend_available = true;
    out->audio_running = g_audio_engine.tick_running;
    out->tick_enabled = g_audio_engine.tick_running;
    out->tick_thread_running = false;
    out->voice_count = g_audio_engine.voice_count;
    return true;
}

#endif /* ESP32_BUILD */
