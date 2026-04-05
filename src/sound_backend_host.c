/*
 * Sound backend stub for host builds (macOS/Linux).
 * macOS builds use CoreAudio default output with a realtime render callback.
 * Other host platforms keep a silent stub backend.
 *
 * Real-time callback checklist:
 * - no allocation
 * - no locks
 * - no I/O/logging
 * - no VM/eval calls
 */

#ifndef ESP32_BUILD

#include "exception.h"
#include "sound_engine.h"
#include "sound_tick_scheduler.h"
#include "thread.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#if defined(__APPLE__)
#include <AudioUnit/AudioUnit.h>
#include <mach/mach_time.h>
#include <errno.h>
#include <unistd.h>

#define HOST_SOUND_SAMPLE_RATE 48000.0
#define HOST_SOUND_CHANNELS 2
#define HOST_SOUND_MAX_VOICES SOUND_MAX_VOICES
#define HOST_SOUND_TICK_NS 1000000u
#define HOST_SOUND_IDLE_SLEEP_NS 2000000u
#define HOST_SOUND_MAX_CATCHUP_TICKS 4u

static AudioComponentInstance g_output_unit = NULL;
static SoundTickScheduler g_tick_scheduler;
#ifndef TINY_CLJ_TEST_RUNNER
static SubjectiveCThread *g_tick_thread = NULL;
static atomic_bool g_tick_thread_running = false;
static SubjectiveCMutex *g_tick_wait_mutex = NULL;
static SubjectiveCCondVar *g_tick_wait_cond = NULL;
#endif
static atomic_bool g_tick_enabled = false;
static atomic_bool g_sound_running = false;
static atomic_bool g_sound_available = false;
static _Atomic uint64_t g_last_sound_kick_time_ns = 0u;
static int g_host_voice_count = 0;
static float g_voice_phase[HOST_SOUND_MAX_VOICES];
static _Atomic uint32_t g_voice_freq[HOST_SOUND_MAX_VOICES];
static _Atomic uint32_t g_voice_volume[HOST_SOUND_MAX_VOICES];
static _Atomic uint32_t g_voice_attack_generation[HOST_SOUND_MAX_VOICES];
static uint32_t g_voice_rendered_attack_generation[HOST_SOUND_MAX_VOICES];
static atomic_bool g_debug_noise_enabled = false;
static _Atomic uint32_t g_debug_noise_min_freq = 0;
static _Atomic uint32_t g_debug_noise_max_freq = 0;
static _Atomic uint32_t g_debug_noise_volume = 0;
static _Atomic uint32_t g_debug_noise_hold_samples = 0;
static _Atomic uint32_t g_debug_noise_remaining_samples = 0;
static _Atomic uint32_t g_debug_noise_samples_until_hop = 0;
static _Atomic uint32_t g_debug_noise_current_freq = 0;
static _Atomic uint32_t g_debug_noise_lfsr = 0x13579BDFu;
static float g_debug_noise_phase = 0.0f;
static atomic_bool g_debug_ramp_enabled = false;
static _Atomic uint32_t g_debug_ramp_start_freq = 0;
static _Atomic uint32_t g_debug_ramp_end_freq = 0;
static _Atomic uint32_t g_debug_ramp_volume = 0;
static _Atomic uint32_t g_debug_ramp_total_samples = 0;
static _Atomic uint32_t g_debug_ramp_remaining_samples = 0;
static float g_debug_ramp_phase = 0.0f;
static atomic_bool g_debug_ramp_noise_enabled = false;
static _Atomic uint32_t g_debug_ramp_noise_min_freq = 0;
static _Atomic uint32_t g_debug_ramp_noise_max_freq = 0;
static _Atomic uint32_t g_debug_ramp_noise_volume = 0;
static _Atomic uint32_t g_debug_ramp_noise_total_samples = 0;
static _Atomic uint32_t g_debug_ramp_noise_segment_total = 0;
static _Atomic uint32_t g_debug_ramp_noise_segment_remaining = 0;
static _Atomic uint32_t g_debug_ramp_noise_segment_start_freq = 0;
static _Atomic uint32_t g_debug_ramp_noise_segment_target_freq = 0;
static _Atomic uint32_t g_debug_ramp_noise_lfsr = 0x2468ACE1u;
static float g_debug_ramp_noise_phase = 0.0f;

static void host_sound_dispose_unit(void);

static uint32_t host_debug_noise_next_lfsr(uint32_t state) {
    uint32_t lfsr = state ? state : 0x13579BDFu;
    uint32_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
    return (lfsr >> 1) | (bit << 31);
}

static bool host_sound_init_failpoint_enabled(const char *step) {
    const char *env = getenv("TINYCLJ_SOUND_HOST_INIT_FAIL");
    if (!env || env[0] == '\0') {
        return false;
    }
    return (strcmp(env, "1") == 0) || (step && strcmp(env, step) == 0);
}

static void host_sound_throw_init_failure(const char *step, int status_code) {
    host_sound_dispose_unit();
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "host sound init failed at %s (status=%d)",
                              step ? step : "unknown", status_code);
}

static void host_sound_stop_debug_helpers(void) {
    atomic_store_explicit(&g_debug_noise_enabled, false, memory_order_release);
    atomic_store_explicit(&g_debug_noise_remaining_samples, 0u, memory_order_release);
    atomic_store_explicit(&g_debug_noise_samples_until_hop, 0u, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_enabled, false, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_remaining_samples, 0u, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_enabled, false, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_total_samples, 0u, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_segment_remaining, 0u, memory_order_release);
}

static void host_sound_ensure_output_running(void) {
    atomic_store_explicit(&g_sound_running, true, memory_order_release);
}

static float host_sound_amp_from_volume(uint32_t volume) {
    return ((float)volume / 255.0f) * 0.20f;
}

static float host_sound_phase_inc_from_freq(float freq_hz) {
    return freq_hz / (float)HOST_SOUND_SAMPLE_RATE;
}

static float host_sound_linear_ramp_freq_at(uint32_t start_freq_hz,
                                            uint32_t end_freq_hz,
                                            uint32_t total_samples,
                                            uint32_t remaining_samples) {
    if (total_samples <= 1u) {
        return (float)end_freq_hz;
    }
    uint32_t elapsed = (total_samples > remaining_samples) ? (total_samples - remaining_samples) : 0u;
    float t = (float)elapsed / (float)(total_samples - 1u);
    return (float)start_freq_hz + (((float)end_freq_hz - (float)start_freq_hz) * t);
}

static float host_sound_linear_ramp_freq_step(uint32_t start_freq_hz,
                                              uint32_t end_freq_hz,
                                              uint32_t total_samples) {
    if (total_samples <= 1u) {
        return 0.0f;
    }
    return ((float)end_freq_hz - (float)start_freq_hz) / (float)(total_samples - 1u);
}

static void host_sound_dispose_unit(void) {
    if (!g_output_unit) {
        return;
    }
    AudioOutputUnitStop(g_output_unit);
    AudioUnitUninitialize(g_output_unit);
    AudioComponentInstanceDispose(g_output_unit);
    g_output_unit = NULL;
}

static uint64_t host_sound_monotonic_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

#ifndef TINY_CLJ_TEST_RUNNER
static void host_sound_sleep_until_ns(uint64_t deadline_ns) {
    uint64_t now_ns = host_sound_monotonic_now_ns();
    if (deadline_ns <= now_ns) {
        return;
    }

    uint64_t wait_ns = deadline_ns - now_ns;
    struct timespec realtime_now;
    if (clock_gettime(CLOCK_REALTIME, &realtime_now) != 0) {
        struct timespec ts;
        ts.tv_sec = (time_t)(wait_ns / 1000000000ull);
        ts.tv_nsec = (long)(wait_ns % 1000000000ull);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        }
        return;
    }
    if (!g_tick_wait_mutex || !g_tick_wait_cond) {
        struct timespec ts;
        ts.tv_sec = (time_t)(wait_ns / 1000000000ull);
        ts.tv_nsec = (long)(wait_ns % 1000000000ull);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        }
        return;
    }

    uint64_t wait_ms_64 = (wait_ns + 999999ull) / 1000000ull;
    uint32_t wait_ms = (wait_ms_64 >= (uint64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)wait_ms_64;
    if (wait_ms == 0u) {
        wait_ms = 1u;
    }

    subjective_c_mutex_lock(g_tick_wait_mutex);
    if (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire) &&
        atomic_load_explicit(&g_tick_enabled, memory_order_relaxed)) {
        (void)subjective_c_condvar_wait(g_tick_wait_cond, g_tick_wait_mutex, wait_ms);
    }
    subjective_c_mutex_unlock(g_tick_wait_mutex);
}
#endif

static OSStatus host_sound_render(void *in_ref_con,
                                  AudioUnitRenderActionFlags *io_action_flags,
                                  const AudioTimeStamp *in_time_stamp,
                                  UInt32 in_bus_number,
                                  UInt32 in_number_frames,
                                  AudioBufferList *io_data) {
    (void)in_ref_con;
    (void)io_action_flags;
    (void)in_time_stamp;
    (void)in_bus_number;

    if (!io_data || io_data->mNumberBuffers == 0) return noErr;

    float *left = (float *)io_data->mBuffers[0].mData;
    float *right = (io_data->mNumberBuffers > 1) ? (float *)io_data->mBuffers[1].mData : left;
    if (!left || !right) return noErr;

    bool running = atomic_load_explicit(&g_sound_running, memory_order_relaxed);
    uint32_t voice_freq[HOST_SOUND_MAX_VOICES] = {0};
    uint32_t voice_vol[HOST_SOUND_MAX_VOICES] = {0};
    float voice_phase_inc[HOST_SOUND_MAX_VOICES] = {0.0f};
    float voice_amp[HOST_SOUND_MAX_VOICES] = {0.0f};
    if (running) {
        for (int v = 0; v < g_host_voice_count; v++) {
            voice_freq[v] = atomic_load_explicit(&g_voice_freq[v], memory_order_relaxed);
            voice_vol[v] = atomic_load_explicit(&g_voice_volume[v], memory_order_relaxed);
            uint32_t attack_generation =
                atomic_load_explicit(&g_voice_attack_generation[v], memory_order_relaxed);
            if (attack_generation != g_voice_rendered_attack_generation[v]) {
                g_voice_phase[v] = 0.0f;
                g_voice_rendered_attack_generation[v] = attack_generation;
            }
            voice_phase_inc[v] = host_sound_phase_inc_from_freq((float)voice_freq[v]);
            voice_amp[v] = host_sound_amp_from_volume(voice_vol[v]);
        }
    }

    bool debug_noise_enabled = atomic_load_explicit(&g_debug_noise_enabled, memory_order_relaxed);
    uint32_t debug_noise_min_freq = 0u;
    uint32_t debug_noise_max_freq = 0u;
    uint32_t debug_noise_volume = 0u;
    uint32_t debug_noise_hold_samples = 0u;
    uint32_t debug_noise_remaining = 0u;
    uint32_t debug_noise_samples_until_hop = 0u;
    uint32_t debug_noise_current_freq = 0u;
    uint32_t debug_noise_lfsr = 0u;
    float debug_noise_phase = 0.0f;
    float debug_noise_amp = 0.0f;
    float debug_noise_phase_inc = 0.0f;
    if (debug_noise_enabled) {
        debug_noise_min_freq = atomic_load_explicit(&g_debug_noise_min_freq, memory_order_relaxed);
        debug_noise_max_freq = atomic_load_explicit(&g_debug_noise_max_freq, memory_order_relaxed);
        debug_noise_volume = atomic_load_explicit(&g_debug_noise_volume, memory_order_relaxed);
        debug_noise_hold_samples = atomic_load_explicit(&g_debug_noise_hold_samples, memory_order_relaxed);
        debug_noise_remaining = atomic_load_explicit(&g_debug_noise_remaining_samples, memory_order_relaxed);
        debug_noise_samples_until_hop =
            atomic_load_explicit(&g_debug_noise_samples_until_hop, memory_order_relaxed);
        debug_noise_current_freq = atomic_load_explicit(&g_debug_noise_current_freq, memory_order_relaxed);
        debug_noise_lfsr = atomic_load_explicit(&g_debug_noise_lfsr, memory_order_relaxed);
        debug_noise_phase = g_debug_noise_phase;
        debug_noise_amp = host_sound_amp_from_volume(debug_noise_volume);
        debug_noise_phase_inc = host_sound_phase_inc_from_freq((float)debug_noise_current_freq);
    }

    bool debug_ramp_enabled = atomic_load_explicit(&g_debug_ramp_enabled, memory_order_relaxed);
    uint32_t debug_ramp_start_freq = 0u;
    uint32_t debug_ramp_end_freq = 0u;
    uint32_t debug_ramp_volume = 0u;
    uint32_t debug_ramp_total_samples = 0u;
    uint32_t debug_ramp_remaining = 0u;
    float debug_ramp_phase = 0.0f;
    float debug_ramp_amp = 0.0f;
    float debug_ramp_freq = 0.0f;
    float debug_ramp_freq_step = 0.0f;
    float debug_ramp_phase_inc = 0.0f;
    float debug_ramp_phase_inc_step = 0.0f;
    if (debug_ramp_enabled) {
        debug_ramp_start_freq = atomic_load_explicit(&g_debug_ramp_start_freq, memory_order_relaxed);
        debug_ramp_end_freq = atomic_load_explicit(&g_debug_ramp_end_freq, memory_order_relaxed);
        debug_ramp_volume = atomic_load_explicit(&g_debug_ramp_volume, memory_order_relaxed);
        debug_ramp_total_samples = atomic_load_explicit(&g_debug_ramp_total_samples, memory_order_relaxed);
        debug_ramp_remaining = atomic_load_explicit(&g_debug_ramp_remaining_samples, memory_order_relaxed);
        debug_ramp_phase = g_debug_ramp_phase;
        debug_ramp_amp = host_sound_amp_from_volume(debug_ramp_volume);
        debug_ramp_freq = host_sound_linear_ramp_freq_at(debug_ramp_start_freq,
                                                         debug_ramp_end_freq,
                                                         debug_ramp_total_samples,
                                                         debug_ramp_remaining);
        debug_ramp_freq_step =
            host_sound_linear_ramp_freq_step(debug_ramp_start_freq, debug_ramp_end_freq, debug_ramp_total_samples);
        debug_ramp_phase_inc = host_sound_phase_inc_from_freq(debug_ramp_freq);
        debug_ramp_phase_inc_step = host_sound_phase_inc_from_freq(debug_ramp_freq_step);
    }

    bool debug_ramp_noise_enabled =
        atomic_load_explicit(&g_debug_ramp_noise_enabled, memory_order_relaxed);
    uint32_t debug_ramp_noise_min_freq = 0u;
    uint32_t debug_ramp_noise_max_freq = 0u;
    uint32_t debug_ramp_noise_volume = 0u;
    uint32_t debug_ramp_noise_total_remaining = 0u;
    uint32_t debug_ramp_noise_segment_total = 0u;
    uint32_t debug_ramp_noise_segment_remaining = 0u;
    uint32_t debug_ramp_noise_segment_start_freq = 0u;
    uint32_t debug_ramp_noise_segment_target_freq = 0u;
    uint32_t debug_ramp_noise_lfsr = 0u;
    float debug_ramp_noise_phase = 0.0f;
    float debug_ramp_noise_amp = 0.0f;
    float debug_ramp_noise_freq = 0.0f;
    float debug_ramp_noise_freq_step = 0.0f;
    float debug_ramp_noise_phase_inc = 0.0f;
    float debug_ramp_noise_phase_inc_step = 0.0f;
    if (debug_ramp_noise_enabled) {
        debug_ramp_noise_min_freq =
            atomic_load_explicit(&g_debug_ramp_noise_min_freq, memory_order_relaxed);
        debug_ramp_noise_max_freq =
            atomic_load_explicit(&g_debug_ramp_noise_max_freq, memory_order_relaxed);
        debug_ramp_noise_volume =
            atomic_load_explicit(&g_debug_ramp_noise_volume, memory_order_relaxed);
        debug_ramp_noise_total_remaining =
            atomic_load_explicit(&g_debug_ramp_noise_total_samples, memory_order_relaxed);
        debug_ramp_noise_segment_total =
            atomic_load_explicit(&g_debug_ramp_noise_segment_total, memory_order_relaxed);
        debug_ramp_noise_segment_remaining =
            atomic_load_explicit(&g_debug_ramp_noise_segment_remaining, memory_order_relaxed);
        debug_ramp_noise_segment_start_freq =
            atomic_load_explicit(&g_debug_ramp_noise_segment_start_freq, memory_order_relaxed);
        debug_ramp_noise_segment_target_freq =
            atomic_load_explicit(&g_debug_ramp_noise_segment_target_freq, memory_order_relaxed);
        debug_ramp_noise_lfsr = atomic_load_explicit(&g_debug_ramp_noise_lfsr, memory_order_relaxed);
        debug_ramp_noise_phase = g_debug_ramp_noise_phase;
        debug_ramp_noise_amp = host_sound_amp_from_volume(debug_ramp_noise_volume);
        debug_ramp_noise_freq = host_sound_linear_ramp_freq_at(debug_ramp_noise_segment_start_freq,
                                                               debug_ramp_noise_segment_target_freq,
                                                               debug_ramp_noise_segment_total,
                                                               debug_ramp_noise_segment_remaining);
        debug_ramp_noise_freq_step =
            host_sound_linear_ramp_freq_step(debug_ramp_noise_segment_start_freq,
                                             debug_ramp_noise_segment_target_freq,
                                             debug_ramp_noise_segment_total);
        debug_ramp_noise_phase_inc = host_sound_phase_inc_from_freq(debug_ramp_noise_freq);
        debug_ramp_noise_phase_inc_step = host_sound_phase_inc_from_freq(debug_ramp_noise_freq_step);
    }

    for (UInt32 i = 0; i < in_number_frames; i++) {
        float sample = 0.0f;
        if (running) {
            for (int v = 0; v < g_host_voice_count; v++) {
                uint32_t freq = voice_freq[v];
                uint32_t vol = voice_vol[v];
                if (freq == 0 || vol == 0) continue;

                float phase = g_voice_phase[v];
                float voice_sample = (phase < 0.5f ? 1.0f : -1.0f) * voice_amp[v];
                sample += voice_sample;

                phase += voice_phase_inc[v];
                if (phase >= 1.0f) phase -= 1.0f;
                g_voice_phase[v] = phase;
            }
        }

        if (debug_noise_enabled) {
            if (debug_noise_remaining == 0u) {
                debug_noise_enabled = false;
            } else {
                if (debug_noise_samples_until_hop == 0u) {
                    debug_noise_lfsr = host_debug_noise_next_lfsr(debug_noise_lfsr);
                    uint32_t span = (debug_noise_max_freq >= debug_noise_min_freq)
                                        ? (debug_noise_max_freq - debug_noise_min_freq + 1u)
                                        : 1u;
                    debug_noise_current_freq = debug_noise_min_freq + (debug_noise_lfsr % span);
                    debug_noise_samples_until_hop =
                        debug_noise_hold_samples > 0u ? debug_noise_hold_samples : 1u;
                    debug_noise_phase_inc =
                        host_sound_phase_inc_from_freq((float)debug_noise_current_freq);
                }

                sample += (debug_noise_phase < 0.5f ? 1.0f : -1.0f) * debug_noise_amp;
                debug_noise_phase += debug_noise_phase_inc;
                if (debug_noise_phase >= 1.0f) debug_noise_phase -= 1.0f;

                debug_noise_samples_until_hop -= 1u;
                debug_noise_remaining -= 1u;
            }
        }

        if (debug_ramp_enabled) {
            if (debug_ramp_remaining == 0u) {
                debug_ramp_enabled = false;
            } else {
                sample += (debug_ramp_phase < 0.5f ? 1.0f : -1.0f) * debug_ramp_amp;
                debug_ramp_phase += debug_ramp_phase_inc;
                if (debug_ramp_phase >= 1.0f) debug_ramp_phase -= 1.0f;
                debug_ramp_freq += debug_ramp_freq_step;
                debug_ramp_phase_inc += debug_ramp_phase_inc_step;
                debug_ramp_remaining -= 1u;
            }
        }

        if (debug_ramp_noise_enabled) {
            if (debug_ramp_noise_total_remaining == 0u) {
                debug_ramp_noise_enabled = false;
            } else {
                if (debug_ramp_noise_segment_remaining == 0u) {
                    debug_ramp_noise_lfsr = host_debug_noise_next_lfsr(debug_ramp_noise_lfsr);
                    uint32_t span = (debug_ramp_noise_max_freq >= debug_ramp_noise_min_freq)
                                        ? (debug_ramp_noise_max_freq - debug_ramp_noise_min_freq + 1u)
                                        : 1u;
                    uint32_t next_target =
                        debug_ramp_noise_min_freq + (debug_ramp_noise_lfsr % span);
                    uint32_t prev_target = debug_ramp_noise_segment_target_freq;
                    debug_ramp_noise_segment_start_freq =
                        prev_target > 0u ? prev_target : debug_ramp_noise_min_freq;
                    debug_ramp_noise_segment_target_freq = next_target;
                    debug_ramp_noise_segment_remaining =
                        debug_ramp_noise_segment_total > 0u ? debug_ramp_noise_segment_total : 1u;
                    debug_ramp_noise_freq = host_sound_linear_ramp_freq_at(
                        debug_ramp_noise_segment_start_freq,
                        debug_ramp_noise_segment_target_freq,
                        debug_ramp_noise_segment_total,
                        debug_ramp_noise_segment_remaining);
                    debug_ramp_noise_freq_step = host_sound_linear_ramp_freq_step(
                        debug_ramp_noise_segment_start_freq,
                        debug_ramp_noise_segment_target_freq,
                        debug_ramp_noise_segment_total);
                    debug_ramp_noise_phase_inc = host_sound_phase_inc_from_freq(debug_ramp_noise_freq);
                    debug_ramp_noise_phase_inc_step =
                        host_sound_phase_inc_from_freq(debug_ramp_noise_freq_step);
                }

                sample += (debug_ramp_noise_phase < 0.5f ? 1.0f : -1.0f) * debug_ramp_noise_amp;
                debug_ramp_noise_phase += debug_ramp_noise_phase_inc;
                if (debug_ramp_noise_phase >= 1.0f) debug_ramp_noise_phase -= 1.0f;

                debug_ramp_noise_freq += debug_ramp_noise_freq_step;
                debug_ramp_noise_phase_inc += debug_ramp_noise_phase_inc_step;
                debug_ramp_noise_segment_remaining -= 1u;
                debug_ramp_noise_total_remaining -= 1u;
            }
        }

        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        left[i] = sample;
        right[i] = sample;
    }

    if (debug_noise_enabled) {
        atomic_store_explicit(&g_debug_noise_remaining_samples, debug_noise_remaining, memory_order_relaxed);
        atomic_store_explicit(&g_debug_noise_samples_until_hop,
                              debug_noise_samples_until_hop,
                              memory_order_relaxed);
        atomic_store_explicit(&g_debug_noise_current_freq, debug_noise_current_freq, memory_order_relaxed);
        atomic_store_explicit(&g_debug_noise_lfsr, debug_noise_lfsr, memory_order_relaxed);
        g_debug_noise_phase = debug_noise_phase;
    } else {
        atomic_store_explicit(&g_debug_noise_remaining_samples, 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&g_debug_noise_enabled, debug_noise_enabled, memory_order_relaxed);

    if (debug_ramp_enabled) {
        atomic_store_explicit(&g_debug_ramp_remaining_samples, debug_ramp_remaining, memory_order_relaxed);
        g_debug_ramp_phase = debug_ramp_phase;
    } else {
        atomic_store_explicit(&g_debug_ramp_remaining_samples, 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&g_debug_ramp_enabled, debug_ramp_enabled, memory_order_relaxed);

    if (debug_ramp_noise_enabled) {
        atomic_store_explicit(&g_debug_ramp_noise_total_samples,
                              debug_ramp_noise_total_remaining,
                              memory_order_relaxed);
        atomic_store_explicit(&g_debug_ramp_noise_segment_remaining,
                              debug_ramp_noise_segment_remaining,
                              memory_order_relaxed);
        atomic_store_explicit(&g_debug_ramp_noise_segment_start_freq,
                              debug_ramp_noise_segment_start_freq,
                              memory_order_relaxed);
        atomic_store_explicit(&g_debug_ramp_noise_segment_target_freq,
                              debug_ramp_noise_segment_target_freq,
                              memory_order_relaxed);
        atomic_store_explicit(&g_debug_ramp_noise_lfsr, debug_ramp_noise_lfsr, memory_order_relaxed);
        g_debug_ramp_noise_phase = debug_ramp_noise_phase;
    } else {
        atomic_store_explicit(&g_debug_ramp_noise_total_samples, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_debug_ramp_noise_segment_remaining, 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&g_debug_ramp_noise_enabled, debug_ramp_noise_enabled, memory_order_relaxed);

    return noErr;
}

static bool host_sound_init_unit(void) {
#ifdef TINY_CLJ_TEST_RUNNER
    const char *forced_fail_step = getenv("TINYCLJ_SOUND_HOST_INIT_FAIL");
#endif
    if (host_sound_init_failpoint_enabled("component-find")) {
        host_sound_throw_init_failure("component-find", -1);
        return false;
    }

    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) {
#ifdef TINY_CLJ_TEST_RUNNER
        if (forced_fail_step && forced_fail_step[0] != '\0' &&
            strcmp(forced_fail_step, "component-find") != 0 &&
            strcmp(forced_fail_step, "1") != 0) {
            host_sound_throw_init_failure(forced_fail_step, -1);
            return false;
        }
        return false;
#else
        host_sound_throw_init_failure("component-find", -1);
        return false;
#endif
    }

    if (host_sound_init_failpoint_enabled("instance-new")) {
        host_sound_throw_init_failure("instance-new", -1);
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(comp, &g_output_unit);
    if (status != noErr || !g_output_unit) {
#ifdef TINY_CLJ_TEST_RUNNER
        host_sound_dispose_unit();
        return false;
#else
        host_sound_throw_init_failure("instance-new", (int)status);
        return false;
#endif
    }

    AudioStreamBasicDescription asbd;
    asbd.mSampleRate = HOST_SOUND_SAMPLE_RATE;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    asbd.mBytesPerPacket = sizeof(float);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = sizeof(float);
    asbd.mChannelsPerFrame = HOST_SOUND_CHANNELS;
    asbd.mBitsPerChannel = 8 * sizeof(float);
    asbd.mReserved = 0;

    if (host_sound_init_failpoint_enabled("stream-format")) {
        host_sound_throw_init_failure("stream-format", -1);
        return false;
    }

    status = AudioUnitSetProperty(g_output_unit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &asbd,
                                  sizeof(asbd));
    if (status != noErr) {
#ifdef TINY_CLJ_TEST_RUNNER
        host_sound_dispose_unit();
        return false;
#else
        host_sound_throw_init_failure("stream-format", (int)status);
        return false;
#endif
    }

    AURenderCallbackStruct cb;
    cb.inputProc = host_sound_render;
    cb.inputProcRefCon = NULL;
    if (host_sound_init_failpoint_enabled("render-callback")) {
        host_sound_throw_init_failure("render-callback", -1);
        return false;
    }

    status = AudioUnitSetProperty(g_output_unit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input,
                                  0,
                                  &cb,
                                  sizeof(cb));
    if (status != noErr) {
#ifdef TINY_CLJ_TEST_RUNNER
        host_sound_dispose_unit();
        return false;
#else
        host_sound_throw_init_failure("render-callback", (int)status);
        return false;
#endif
    }

    if (host_sound_init_failpoint_enabled("unit-initialize")) {
        host_sound_throw_init_failure("unit-initialize", -1);
        return false;
    }

    status = AudioUnitInitialize(g_output_unit);
    if (status != noErr) {
#ifdef TINY_CLJ_TEST_RUNNER
        host_sound_dispose_unit();
        return false;
#else
        host_sound_throw_init_failure("unit-initialize", (int)status);
        return false;
#endif
    }
    return true;
}

#ifndef TINY_CLJ_TEST_RUNNER
static void host_tick_thread_main(void *arg) {
    (void)arg;
    /*
     * Ownership contract:
     * this helper thread may advance plain-C sound engine state only. It must
     * not allocate Clj objects or enqueue finished callbacks directly; those
     * are deferred to the interpreter thread drain path.
     */
    while (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire)) {
        if (!atomic_load_explicit(&g_tick_enabled, memory_order_relaxed)) {
            subjective_c_mutex_lock(g_tick_wait_mutex);
            while (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire) &&
                   !atomic_load_explicit(&g_tick_enabled, memory_order_relaxed)) {
                (void)subjective_c_condvar_wait(g_tick_wait_cond, g_tick_wait_mutex, UINT32_MAX);
            }
            subjective_c_mutex_unlock(g_tick_wait_mutex);
            sound_tick_scheduler_start(&g_tick_scheduler, host_sound_monotonic_now_ns());
            continue;
        }

        uint64_t now_ns = host_sound_monotonic_now_ns();
        uint32_t skipped_ticks = 0u;
        uint32_t due = sound_tick_scheduler_ticks_due(&g_tick_scheduler, now_ns, &skipped_ticks);
        if (skipped_ticks > 0u) {
            sound_telemetry_add_tick_overruns(skipped_ticks);
        }
        if (due == 0u) {
            host_sound_sleep_until_ns(g_tick_scheduler.next_deadline_ns);
            continue;
        }

        for (uint32_t i = 0; i < due; i++) {
            if (!atomic_load_explicit(&g_tick_thread_running, memory_order_acquire) ||
                !atomic_load_explicit(&g_tick_enabled, memory_order_relaxed)) {
                break;
            }
            sound_engine_tick();
        }
    }
}
#endif

void sound_backend_init(int voice_count) {
    if (voice_count < 1) voice_count = 1;
    if (voice_count > HOST_SOUND_MAX_VOICES) voice_count = HOST_SOUND_MAX_VOICES;
    atomic_store_explicit(&g_sound_available, false, memory_order_release);
    atomic_store_explicit(&g_sound_running, false, memory_order_release);
    atomic_store_explicit(&g_tick_enabled, false, memory_order_release);
    g_host_voice_count = voice_count;
    for (int i = 0; i < HOST_SOUND_MAX_VOICES; i++) {
        g_voice_phase[i] = 0.0f;
        atomic_store_explicit(&g_voice_freq[i], 0, memory_order_relaxed);
        atomic_store_explicit(&g_voice_volume[i], 0, memory_order_relaxed);
        atomic_store_explicit(&g_voice_attack_generation[i], 0, memory_order_relaxed);
        g_voice_rendered_attack_generation[i] = 0u;
    }
    host_sound_stop_debug_helpers();
    sound_tick_scheduler_init(&g_tick_scheduler, HOST_SOUND_TICK_NS, HOST_SOUND_MAX_CATCHUP_TICKS);

    if (!host_sound_init_unit()) {
        return;
    }

    atomic_store_explicit(&g_sound_available, true, memory_order_release);
    (void)AudioOutputUnitStart(g_output_unit);

#ifndef TINY_CLJ_TEST_RUNNER
    g_tick_wait_mutex = subjective_c_mutex_create();
    if (!g_tick_wait_mutex) {
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        host_sound_throw_init_failure("tick-thread-mutex", -1);
        return;
    }
    g_tick_wait_cond = subjective_c_condvar_create();
    if (!g_tick_wait_cond) {
        subjective_c_mutex_destroy(g_tick_wait_mutex);
        g_tick_wait_mutex = NULL;
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        host_sound_throw_init_failure("tick-thread-condvar", -1);
        return;
    }
    if (host_sound_init_failpoint_enabled("tick-thread")) {
        subjective_c_condvar_destroy(g_tick_wait_cond);
        subjective_c_mutex_destroy(g_tick_wait_mutex);
        g_tick_wait_cond = NULL;
        g_tick_wait_mutex = NULL;
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        host_sound_throw_init_failure("tick-thread", -1);
        return;
    }

    atomic_store_explicit(&g_tick_thread_running, true, memory_order_release);
    SubjectiveCThreadConfig cfg = {
        .name = "sound-tick",
        .stack_bytes = 0u,
        .priority = 0,
    };
    g_tick_thread = tread_create(host_tick_thread_main, NULL, &cfg);
    if (!g_tick_thread) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        subjective_c_condvar_destroy(g_tick_wait_cond);
        subjective_c_mutex_destroy(g_tick_wait_mutex);
        g_tick_wait_cond = NULL;
        g_tick_wait_mutex = NULL;
        host_sound_throw_init_failure("tick-thread", -1);
    }
    sound_tick_start();
#endif
}

void sound_backend_shutdown(void) {
    sound_tick_stop();
    host_sound_stop_debug_helpers();

#ifndef TINY_CLJ_TEST_RUNNER
    if (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire)) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
        subjective_c_mutex_lock(g_tick_wait_mutex);
        subjective_c_condvar_broadcast(g_tick_wait_cond);
        subjective_c_mutex_unlock(g_tick_wait_mutex);
        (void)tread_join(g_tick_thread);
    }
    tread_destroy(g_tick_thread);
    subjective_c_condvar_destroy(g_tick_wait_cond);
    subjective_c_mutex_destroy(g_tick_wait_mutex);
    g_tick_thread = NULL;
    g_tick_wait_cond = NULL;
    g_tick_wait_mutex = NULL;
#endif

    host_sound_dispose_unit();
    atomic_store_explicit(&g_sound_available, false, memory_order_release);
}

void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume, bool retrigger) {
    if (voice_index < 0 || voice_index >= g_host_voice_count) return;
    atomic_store_explicit(&g_voice_freq[voice_index], (uint32_t)freq_hz, memory_order_relaxed);
    atomic_store_explicit(&g_voice_volume[voice_index], (uint32_t)volume, memory_order_relaxed);
    if (retrigger) {
        (void)atomic_fetch_add_explicit(&g_voice_attack_generation[voice_index], 1u, memory_order_relaxed);
    }
}

bool sound_backend_keepalive_active(void) {
    return atomic_load_explicit(&g_debug_noise_enabled, memory_order_acquire) ||
           atomic_load_explicit(&g_debug_ramp_enabled, memory_order_acquire) ||
           atomic_load_explicit(&g_debug_ramp_noise_enabled, memory_order_acquire);
}

bool sound_backend_set_min_stable_duty(uint8_t duty) {
    (void)duty;
    return false;
}

uint8_t sound_backend_get_min_stable_duty(void) {
    return 0u;
}

bool sound_backend_supports_min_stable_duty(void) {
    return false;
}

void sound_tick_start(void) {
    bool started_now = sound_engine_tick_mark_running();
    if (!started_now) {
        atomic_store_explicit(&g_tick_enabled, true, memory_order_release);
        atomic_store_explicit(&g_sound_running, true, memory_order_release);
#ifndef TINY_CLJ_TEST_RUNNER
        subjective_c_mutex_lock(g_tick_wait_mutex);
        subjective_c_condvar_broadcast(g_tick_wait_cond);
        subjective_c_mutex_unlock(g_tick_wait_mutex);
#endif
        return;
    }
    sound_tick_scheduler_start(&g_tick_scheduler, host_sound_monotonic_now_ns());
    atomic_store_explicit(&g_tick_enabled, true, memory_order_release);
    atomic_store_explicit(&g_sound_running, true, memory_order_release);
#ifndef TINY_CLJ_TEST_RUNNER
    subjective_c_mutex_lock(g_tick_wait_mutex);
    subjective_c_condvar_broadcast(g_tick_wait_cond);
    subjective_c_mutex_unlock(g_tick_wait_mutex);
#endif
}

void sound_tick_stop(void) {
    sound_engine_tick_mark_stopped();
    sound_tick_scheduler_stop(&g_tick_scheduler);
    atomic_store_explicit(&g_tick_enabled, false, memory_order_release);
    atomic_store_explicit(&g_sound_running, false, memory_order_release);
}

void sound_tick_sleep(void) {
    atomic_store_explicit(&g_tick_enabled, false, memory_order_release);
#ifdef TINY_CLJ_TEST_RUNNER
    sound_engine_tick_mark_stopped();
#endif
}

void sound_tick_kick(void) {
    if (!sound_engine_tick_is_running()) {
        sound_tick_start();
        return;
    }
    uint64_t now_ns = host_sound_monotonic_now_ns();
    atomic_store_explicit(&g_last_sound_kick_time_ns, now_ns, memory_order_release);
    atomic_store_explicit(&g_tick_enabled, true, memory_order_release);
#ifndef TINY_CLJ_TEST_RUNNER
    subjective_c_mutex_lock(g_tick_wait_mutex);
    subjective_c_condvar_broadcast(g_tick_wait_cond);
    subjective_c_mutex_unlock(g_tick_wait_mutex);
#endif
}

bool sound_backend_host_get_status(SoundHostStatus *out) {
    if (!out) return false;
    out->backend_available = atomic_load_explicit(&g_sound_available, memory_order_acquire);
    out->sound_running = atomic_load_explicit(&g_sound_running, memory_order_acquire);
    out->tick_enabled = atomic_load_explicit(&g_tick_enabled, memory_order_acquire);
#ifndef TINY_CLJ_TEST_RUNNER
    out->tick_thread_running = atomic_load_explicit(&g_tick_thread_running, memory_order_acquire);
#else
    out->tick_thread_running = false;
#endif
    out->voice_count = g_host_voice_count;
    out->debug_noise_active = atomic_load_explicit(&g_debug_noise_enabled, memory_order_acquire);
    out->debug_ramp_active = atomic_load_explicit(&g_debug_ramp_enabled, memory_order_acquire);
    out->debug_ramp_noise_active =
        atomic_load_explicit(&g_debug_ramp_noise_enabled, memory_order_acquire);
    return true;
}

bool sound_backend_host_play_debug_noise(uint16_t min_freq_hz,
                                         uint16_t max_freq_hz,
                                         uint32_t duration_ms,
                                         uint32_t hop_ms,
                                         uint8_t volume) {
    if (min_freq_hz < 20 || max_freq_hz < min_freq_hz || duration_ms == 0u || hop_ms == 0u) {
        return false;
    }
    uint32_t hold_samples = (uint32_t)((HOST_SOUND_SAMPLE_RATE * (double)hop_ms) / 1000.0);
    uint32_t remaining_samples = (uint32_t)((HOST_SOUND_SAMPLE_RATE * (double)duration_ms) / 1000.0);
    if (hold_samples == 0u) hold_samples = 1u;
    if (remaining_samples == 0u) remaining_samples = 1u;

    host_sound_stop_debug_helpers();
    atomic_store_explicit(&g_debug_noise_min_freq, min_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_noise_max_freq, max_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_noise_volume, volume, memory_order_release);
    atomic_store_explicit(&g_debug_noise_hold_samples, hold_samples, memory_order_release);
    atomic_store_explicit(&g_debug_noise_remaining_samples, remaining_samples, memory_order_release);
    atomic_store_explicit(&g_debug_noise_samples_until_hop, 0u, memory_order_release);
    atomic_store_explicit(&g_debug_noise_current_freq, min_freq_hz, memory_order_release);
    g_debug_noise_phase = 0.0f;
    atomic_store_explicit(&g_debug_noise_enabled, true, memory_order_release);

    host_sound_ensure_output_running();
    return true;
}

bool sound_backend_host_debug_noise_active(void) {
    return atomic_load_explicit(&g_debug_noise_enabled, memory_order_acquire);
}

bool sound_backend_host_play_debug_ramp(uint16_t start_freq_hz,
                                        uint16_t end_freq_hz,
                                        uint32_t duration_ms,
                                        uint8_t volume) {
    if (start_freq_hz < 20 || start_freq_hz > 20000 || end_freq_hz < 20 || end_freq_hz > 20000 ||
        duration_ms == 0u) {
        return false;
    }
    uint32_t total_samples = (uint32_t)((HOST_SOUND_SAMPLE_RATE * (double)duration_ms) / 1000.0);
    if (total_samples == 0u) total_samples = 1u;

    host_sound_stop_debug_helpers();
    atomic_store_explicit(&g_debug_ramp_start_freq, start_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_end_freq, end_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_volume, volume, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_total_samples, total_samples, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_remaining_samples, total_samples, memory_order_release);
    g_debug_ramp_phase = 0.0f;
    atomic_store_explicit(&g_debug_ramp_enabled, true, memory_order_release);

    host_sound_ensure_output_running();
    return true;
}

bool sound_backend_host_debug_ramp_active(void) {
    return atomic_load_explicit(&g_debug_ramp_enabled, memory_order_acquire);
}

bool sound_backend_host_play_debug_ramp_noise(uint16_t min_freq_hz,
                                              uint16_t max_freq_hz,
                                              uint32_t duration_ms,
                                              uint32_t hop_ms,
                                              uint8_t volume) {
    if (min_freq_hz < 20 || max_freq_hz > 20000 || max_freq_hz < min_freq_hz ||
        duration_ms == 0u || hop_ms == 0u) {
        return false;
    }
    uint32_t total_samples = (uint32_t)((HOST_SOUND_SAMPLE_RATE * (double)duration_ms) / 1000.0);
    uint32_t segment_total = (uint32_t)((HOST_SOUND_SAMPLE_RATE * (double)hop_ms) / 1000.0);
    if (total_samples == 0u) total_samples = 1u;
    if (segment_total == 0u) segment_total = 1u;

    host_sound_stop_debug_helpers();
    atomic_store_explicit(&g_debug_ramp_noise_min_freq, min_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_max_freq, max_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_volume, volume, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_total_samples, total_samples, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_segment_total, segment_total, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_segment_remaining, 0u, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_segment_start_freq, min_freq_hz, memory_order_release);
    atomic_store_explicit(&g_debug_ramp_noise_segment_target_freq, min_freq_hz, memory_order_release);
    g_debug_ramp_noise_phase = 0.0f;
    atomic_store_explicit(&g_debug_ramp_noise_enabled, true, memory_order_release);

    host_sound_ensure_output_running();
    return true;
}

bool sound_backend_host_debug_ramp_noise_active(void) {
    return atomic_load_explicit(&g_debug_ramp_noise_enabled, memory_order_acquire);
}

#else

void sound_backend_init(int voice_count) {
    (void)voice_count;
}

void sound_backend_shutdown(void) {
}

void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume, bool retrigger) {
    (void)voice_index;
    (void)freq_hz;
    (void)volume;
    (void)retrigger;
}

bool sound_backend_keepalive_active(void) {
    return false;
}

bool sound_backend_set_min_stable_duty(uint8_t duty) {
    (void)duty;
    return false;
}

uint8_t sound_backend_get_min_stable_duty(void) {
    return 0u;
}

bool sound_backend_supports_min_stable_duty(void) {
    return false;
}

void sound_tick_start(void) {
    (void)sound_engine_tick_mark_running();
}

void sound_tick_stop(void) {
    sound_engine_tick_mark_stopped();
}

void sound_tick_sleep(void) {
    sound_engine_tick_mark_stopped();
}

void sound_tick_kick(void) {
    if (!sound_engine_tick_is_running()) {
        sound_tick_start();
    }
}

bool sound_backend_host_get_status(SoundHostStatus *out) {
    if (!out) return false;
    out->backend_available = false;
    out->sound_running = false;
    out->tick_enabled = false;
    out->tick_thread_running = false;
    out->voice_count = 0;
    out->debug_noise_active = false;
    out->debug_ramp_active = false;
    out->debug_ramp_noise_active = false;
    return false;
}

bool sound_backend_host_play_debug_noise(uint16_t min_freq_hz,
                                         uint16_t max_freq_hz,
                                         uint32_t duration_ms,
                                         uint32_t hop_ms,
                                         uint8_t volume) {
    (void)min_freq_hz;
    (void)max_freq_hz;
    (void)duration_ms;
    (void)hop_ms;
    (void)volume;
    return false;
}

bool sound_backend_host_debug_noise_active(void) {
    return false;
}

bool sound_backend_host_play_debug_ramp(uint16_t start_freq_hz,
                                        uint16_t end_freq_hz,
                                        uint32_t duration_ms,
                                        uint8_t volume) {
    (void)start_freq_hz;
    (void)end_freq_hz;
    (void)duration_ms;
    (void)volume;
    return false;
}

bool sound_backend_host_debug_ramp_active(void) {
    return false;
}

bool sound_backend_host_play_debug_ramp_noise(uint16_t min_freq_hz,
                                              uint16_t max_freq_hz,
                                              uint32_t duration_ms,
                                              uint32_t hop_ms,
                                              uint8_t volume) {
    (void)min_freq_hz;
    (void)max_freq_hz;
    (void)duration_ms;
    (void)hop_ms;
    (void)volume;
    return false;
}

bool sound_backend_host_debug_ramp_noise_active(void) {
    return false;
}

#endif

#endif /* !ESP32_BUILD */
