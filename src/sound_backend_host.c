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
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__)
#include <AudioUnit/AudioUnit.h>
#include <mach/mach_time.h>
#include <pthread.h>
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
static pthread_t g_tick_thread;
static atomic_bool g_tick_thread_running = false;
#endif
static atomic_bool g_tick_enabled = false;
static atomic_bool g_sound_running = false;
static atomic_bool g_sound_available = false;
static int g_host_voice_count = 0;
static float g_voice_phase[HOST_SOUND_MAX_VOICES];
static _Atomic uint32_t g_voice_freq[HOST_SOUND_MAX_VOICES];
static _Atomic uint32_t g_voice_volume[HOST_SOUND_MAX_VOICES];
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
    if (atomic_load_explicit(&g_sound_available, memory_order_acquire) && g_output_unit) {
        (void)AudioOutputUnitStart(g_output_unit);
    }
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
static mach_timebase_info_data_t g_host_sound_mach_timebase = {0};

static void host_sound_init_mach_timebase(void) {
    if (g_host_sound_mach_timebase.denom == 0) {
        (void)mach_timebase_info(&g_host_sound_mach_timebase);
    }
}

static uint64_t ns_to_mach_abs(uint64_t ns) {
    host_sound_init_mach_timebase();
    if (g_host_sound_mach_timebase.numer == 0) {
        return ns;
    }
    return (ns * g_host_sound_mach_timebase.denom) / g_host_sound_mach_timebase.numer;
}

static void host_sound_sleep_until_ns(uint64_t deadline_ns) {
    uint64_t now_ns = host_sound_monotonic_now_ns();
    if (deadline_ns <= now_ns) {
        return;
    }

#if defined(__APPLE__)
    uint64_t wait_delta_ns = deadline_ns - now_ns;
    uint64_t deadline_abs = mach_absolute_time() + ns_to_mach_abs(wait_delta_ns);
    (void)mach_wait_until(deadline_abs);
#else
    uint64_t wait_ns = deadline_ns - now_ns;
    struct timespec ts;
    ts.tv_sec = (time_t)(wait_ns / 1000000000ull);
    ts.tv_nsec = (long)(wait_ns % 1000000000ull);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
#endif
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
    for (UInt32 i = 0; i < in_number_frames; i++) {
        float sample = 0.0f;
        if (running) {
            for (int v = 0; v < g_host_voice_count; v++) {
                uint32_t freq = atomic_load_explicit(&g_voice_freq[v], memory_order_relaxed);
                uint32_t vol = atomic_load_explicit(&g_voice_volume[v], memory_order_relaxed);
                if (freq == 0 || vol == 0) continue;

                float amp = ((float)vol / 255.0f) * 0.20f;
                float phase = g_voice_phase[v];
                float voice_sample = (phase < 0.5f ? 1.0f : -1.0f) * amp;
                sample += voice_sample;

                phase += ((float)freq / (float)HOST_SOUND_SAMPLE_RATE);
                if (phase >= 1.0f) phase -= 1.0f;
                g_voice_phase[v] = phase;
            }
        }

        if (atomic_load_explicit(&g_debug_noise_enabled, memory_order_relaxed)) {
            uint32_t remaining = atomic_load_explicit(&g_debug_noise_remaining_samples, memory_order_relaxed);
            if (remaining == 0u) {
                atomic_store_explicit(&g_debug_noise_enabled, false, memory_order_relaxed);
            } else {
                uint32_t samples_until_hop = atomic_load_explicit(&g_debug_noise_samples_until_hop, memory_order_relaxed);
                if (samples_until_hop == 0u) {
                    uint32_t lfsr = host_debug_noise_next_lfsr(
                        atomic_load_explicit(&g_debug_noise_lfsr, memory_order_relaxed));
                    uint32_t min_freq = atomic_load_explicit(&g_debug_noise_min_freq, memory_order_relaxed);
                    uint32_t max_freq = atomic_load_explicit(&g_debug_noise_max_freq, memory_order_relaxed);
                    uint32_t span = (max_freq >= min_freq) ? (max_freq - min_freq + 1u) : 1u;
                    uint32_t next_freq = min_freq + (lfsr % span);
                    uint32_t hold_samples = atomic_load_explicit(&g_debug_noise_hold_samples, memory_order_relaxed);
                    atomic_store_explicit(&g_debug_noise_lfsr, lfsr, memory_order_relaxed);
                    atomic_store_explicit(&g_debug_noise_current_freq, next_freq, memory_order_relaxed);
                    atomic_store_explicit(&g_debug_noise_samples_until_hop, hold_samples > 0u ? hold_samples : 1u,
                                          memory_order_relaxed);
                    samples_until_hop = hold_samples > 0u ? hold_samples : 1u;
                }

                uint32_t freq = atomic_load_explicit(&g_debug_noise_current_freq, memory_order_relaxed);
                uint32_t vol = atomic_load_explicit(&g_debug_noise_volume, memory_order_relaxed);
                float amp = ((float)vol / 255.0f) * 0.20f;
                float phase = g_debug_noise_phase;
                sample += (phase < 0.5f ? 1.0f : -1.0f) * amp;
                phase += ((float)freq / (float)HOST_SOUND_SAMPLE_RATE);
                if (phase >= 1.0f) phase -= 1.0f;
                g_debug_noise_phase = phase;

                atomic_store_explicit(&g_debug_noise_samples_until_hop, samples_until_hop - 1u, memory_order_relaxed);
                atomic_store_explicit(&g_debug_noise_remaining_samples, remaining - 1u, memory_order_relaxed);
            }
        }

        if (atomic_load_explicit(&g_debug_ramp_enabled, memory_order_relaxed)) {
            uint32_t remaining = atomic_load_explicit(&g_debug_ramp_remaining_samples, memory_order_relaxed);
            if (remaining == 0u) {
                atomic_store_explicit(&g_debug_ramp_enabled, false, memory_order_relaxed);
            } else {
                uint32_t total = atomic_load_explicit(&g_debug_ramp_total_samples, memory_order_relaxed);
                uint32_t start_freq = atomic_load_explicit(&g_debug_ramp_start_freq, memory_order_relaxed);
                uint32_t end_freq = atomic_load_explicit(&g_debug_ramp_end_freq, memory_order_relaxed);
                uint32_t vol = atomic_load_explicit(&g_debug_ramp_volume, memory_order_relaxed);
                uint32_t elapsed = (total > remaining) ? (total - remaining) : 0u;
                float amp = ((float)vol / 255.0f) * 0.20f;
                float phase = g_debug_ramp_phase;
                float t = (total > 1u) ? ((float)elapsed / (float)(total - 1u)) : 1.0f;
                float freq = (float)start_freq + (((float)end_freq - (float)start_freq) * t);
                sample += (phase < 0.5f ? 1.0f : -1.0f) * amp;
                phase += (freq / (float)HOST_SOUND_SAMPLE_RATE);
                if (phase >= 1.0f) phase -= 1.0f;
                g_debug_ramp_phase = phase;
                atomic_store_explicit(&g_debug_ramp_remaining_samples, remaining - 1u, memory_order_relaxed);
            }
        }

        if (atomic_load_explicit(&g_debug_ramp_noise_enabled, memory_order_relaxed)) {
            uint32_t total_remaining =
                atomic_load_explicit(&g_debug_ramp_noise_total_samples, memory_order_relaxed);
            if (total_remaining == 0u) {
                atomic_store_explicit(&g_debug_ramp_noise_enabled, false, memory_order_relaxed);
            } else {
                uint32_t segment_remaining =
                    atomic_load_explicit(&g_debug_ramp_noise_segment_remaining, memory_order_relaxed);
                uint32_t segment_total =
                    atomic_load_explicit(&g_debug_ramp_noise_segment_total, memory_order_relaxed);
                if (segment_remaining == 0u) {
                    uint32_t lfsr = host_debug_noise_next_lfsr(
                        atomic_load_explicit(&g_debug_ramp_noise_lfsr, memory_order_relaxed));
                    uint32_t min_freq =
                        atomic_load_explicit(&g_debug_ramp_noise_min_freq, memory_order_relaxed);
                    uint32_t max_freq =
                        atomic_load_explicit(&g_debug_ramp_noise_max_freq, memory_order_relaxed);
                    uint32_t span = (max_freq >= min_freq) ? (max_freq - min_freq + 1u) : 1u;
                    uint32_t next_target = min_freq + (lfsr % span);
                    uint32_t prev_target =
                        atomic_load_explicit(&g_debug_ramp_noise_segment_target_freq, memory_order_relaxed);
                    atomic_store_explicit(&g_debug_ramp_noise_lfsr, lfsr, memory_order_relaxed);
                    atomic_store_explicit(&g_debug_ramp_noise_segment_start_freq,
                                          prev_target > 0u ? prev_target : min_freq,
                                          memory_order_relaxed);
                    atomic_store_explicit(&g_debug_ramp_noise_segment_target_freq,
                                          next_target,
                                          memory_order_relaxed);
                    atomic_store_explicit(&g_debug_ramp_noise_segment_remaining,
                                          segment_total > 0u ? segment_total : 1u,
                                          memory_order_relaxed);
                    segment_remaining = segment_total > 0u ? segment_total : 1u;
                }

                uint32_t start_freq =
                    atomic_load_explicit(&g_debug_ramp_noise_segment_start_freq, memory_order_relaxed);
                uint32_t target_freq =
                    atomic_load_explicit(&g_debug_ramp_noise_segment_target_freq, memory_order_relaxed);
                uint32_t vol = atomic_load_explicit(&g_debug_ramp_noise_volume, memory_order_relaxed);
                uint32_t elapsed = (segment_total > segment_remaining) ? (segment_total - segment_remaining) : 0u;
                float amp = ((float)vol / 255.0f) * 0.20f;
                float phase = g_debug_ramp_noise_phase;
                float t = (segment_total > 1u) ? ((float)elapsed / (float)(segment_total - 1u)) : 1.0f;
                float freq = (float)start_freq + (((float)target_freq - (float)start_freq) * t);
                sample += (phase < 0.5f ? 1.0f : -1.0f) * amp;
                phase += (freq / (float)HOST_SOUND_SAMPLE_RATE);
                if (phase >= 1.0f) phase -= 1.0f;
                g_debug_ramp_noise_phase = phase;

                atomic_store_explicit(&g_debug_ramp_noise_segment_remaining,
                                      segment_remaining - 1u,
                                      memory_order_relaxed);
                atomic_store_explicit(&g_debug_ramp_noise_total_samples,
                                      total_remaining - 1u,
                                      memory_order_relaxed);
            }
        }

        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        left[i] = sample;
        right[i] = sample;
    }

    return noErr;
}

static bool host_sound_init_unit(void) {
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
        host_sound_throw_init_failure("component-find", -1);
        return false;
    }

    if (host_sound_init_failpoint_enabled("instance-new")) {
        host_sound_throw_init_failure("instance-new", -1);
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(comp, &g_output_unit);
    if (status != noErr || !g_output_unit) {
        host_sound_throw_init_failure("instance-new", (int)status);
        return false;
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
        host_sound_throw_init_failure("stream-format", (int)status);
        return false;
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
        host_sound_throw_init_failure("render-callback", (int)status);
        return false;
    }

    if (host_sound_init_failpoint_enabled("unit-initialize")) {
        host_sound_throw_init_failure("unit-initialize", -1);
        return false;
    }

    status = AudioUnitInitialize(g_output_unit);
    if (status != noErr) {
        host_sound_throw_init_failure("unit-initialize", (int)status);
        return false;
    }
    return true;
}

#ifndef TINY_CLJ_TEST_RUNNER
static void *host_tick_thread_main(void *arg) {
    (void)arg;
    while (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire)) {
        if (!atomic_load_explicit(&g_tick_enabled, memory_order_relaxed)) {
            host_sound_sleep_until_ns(host_sound_monotonic_now_ns() + HOST_SOUND_IDLE_SLEEP_NS);
            continue;
        }

        uint64_t now_ns = host_sound_monotonic_now_ns();
        uint32_t skipped_ticks = 0u;
        uint32_t due = sound_tick_scheduler_ticks_due(&g_tick_scheduler, now_ns, &skipped_ticks);
        if (skipped_ticks > 0u) {
            g_sound_engine.telemetry.tick_overrun_count += skipped_ticks;
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
    return NULL;
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
    }
    host_sound_stop_debug_helpers();
    sound_tick_scheduler_init(&g_tick_scheduler, HOST_SOUND_TICK_NS, HOST_SOUND_MAX_CATCHUP_TICKS);

    if (!host_sound_init_unit()) {
        return;
    }

    atomic_store_explicit(&g_sound_available, true, memory_order_release);

#ifndef TINY_CLJ_TEST_RUNNER
    if (host_sound_init_failpoint_enabled("tick-thread")) {
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        host_sound_throw_init_failure("tick-thread", -1);
        return;
    }

    atomic_store_explicit(&g_tick_thread_running, true, memory_order_release);
    if (pthread_create(&g_tick_thread, NULL, host_tick_thread_main, NULL) != 0) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        host_sound_throw_init_failure("tick-thread", -1);
    }
#endif
}

void sound_backend_shutdown(void) {
    sound_tick_stop();
    host_sound_stop_debug_helpers();

#ifndef TINY_CLJ_TEST_RUNNER
    if (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire)) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
        (void)pthread_join(g_tick_thread, NULL);
    }
#endif

    host_sound_dispose_unit();
    atomic_store_explicit(&g_sound_available, false, memory_order_release);
}

void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume) {
    if (voice_index < 0 || voice_index >= g_host_voice_count) return;
    atomic_store_explicit(&g_voice_freq[voice_index], (uint32_t)freq_hz, memory_order_relaxed);
    atomic_store_explicit(&g_voice_volume[voice_index], (uint32_t)volume, memory_order_relaxed);
}

void sound_tick_start(void) {
    g_sound_engine.tick_running = true;
    sound_tick_scheduler_start(&g_tick_scheduler, host_sound_monotonic_now_ns());
    atomic_store_explicit(&g_tick_enabled, true, memory_order_release);
    atomic_store_explicit(&g_sound_running, true, memory_order_release);
    if (atomic_load_explicit(&g_sound_available, memory_order_acquire) && g_output_unit) {
        (void)AudioOutputUnitStart(g_output_unit);
    }
}

void sound_tick_stop(void) {
    g_sound_engine.tick_running = false;
    sound_tick_scheduler_stop(&g_tick_scheduler);
    atomic_store_explicit(&g_tick_enabled, false, memory_order_release);
    atomic_store_explicit(&g_sound_running, false, memory_order_release);
    if (g_output_unit) {
        (void)AudioOutputUnitStop(g_output_unit);
    }
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

void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume) {
    (void)voice_index;
    (void)freq_hz;
    (void)volume;
}

void sound_tick_start(void) {
    g_sound_engine.tick_running = true;
}

void sound_tick_stop(void) {
    g_sound_engine.tick_running = false;
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
