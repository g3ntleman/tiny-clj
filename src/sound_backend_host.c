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

#include "sound_engine.h"
#include "sound_tick_scheduler.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
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

        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        left[i] = sample;
        right[i] = sample;
    }

    return noErr;
}

static bool host_sound_init_unit(void) {
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) return false;
    if (AudioComponentInstanceNew(comp, &g_output_unit) != noErr || !g_output_unit) return false;

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

    if (AudioUnitSetProperty(g_output_unit,
                             kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input,
                             0,
                             &asbd,
                             sizeof(asbd)) != noErr) {
        return false;
    }

    AURenderCallbackStruct cb;
    cb.inputProc = host_sound_render;
    cb.inputProcRefCon = NULL;
    if (AudioUnitSetProperty(g_output_unit,
                             kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input,
                             0,
                             &cb,
                             sizeof(cb)) != noErr) {
        return false;
    }

    if (AudioUnitInitialize(g_output_unit) != noErr) return false;
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
    g_host_voice_count = voice_count;
    for (int i = 0; i < HOST_SOUND_MAX_VOICES; i++) {
        g_voice_phase[i] = 0.0f;
        atomic_store_explicit(&g_voice_freq[i], 0, memory_order_relaxed);
        atomic_store_explicit(&g_voice_volume[i], 0, memory_order_relaxed);
    }
    sound_tick_scheduler_init(&g_tick_scheduler, HOST_SOUND_TICK_NS, HOST_SOUND_MAX_CATCHUP_TICKS);

    if (!host_sound_init_unit()) {
        host_sound_dispose_unit();
        return;
    }

    atomic_store_explicit(&g_sound_available, true, memory_order_release);

#ifndef TINY_CLJ_TEST_RUNNER
    atomic_store_explicit(&g_tick_thread_running, true, memory_order_release);
    if (pthread_create(&g_tick_thread, NULL, host_tick_thread_main, NULL) != 0) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
        atomic_store_explicit(&g_sound_available, false, memory_order_release);
        host_sound_dispose_unit();
    }
#endif
}

void sound_backend_shutdown(void) {
    sound_tick_stop();

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
    return true;
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
    return false;
}

#endif

#endif /* !ESP32_BUILD */
