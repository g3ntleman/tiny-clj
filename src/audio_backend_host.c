/*
 * Audio backend stub for host builds (macOS/Linux).
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

#include "audio_engine.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <AudioUnit/AudioUnit.h>
#include <pthread.h>
#include <unistd.h>

#define HOST_AUDIO_SAMPLE_RATE 48000.0
#define HOST_AUDIO_CHANNELS 2
#define HOST_AUDIO_MAX_VOICES AUDIO_MAX_VOICES

static AudioComponentInstance g_output_unit = NULL;
#ifndef TINY_CLJ_TEST_RUNNER
static pthread_t g_tick_thread;
static atomic_bool g_tick_thread_running = false;
#endif
static atomic_bool g_tick_enabled = false;
static atomic_bool g_audio_running = false;
static atomic_bool g_audio_available = false;
static int g_host_voice_count = 0;
static float g_voice_phase[HOST_AUDIO_MAX_VOICES];
static _Atomic uint32_t g_voice_freq[HOST_AUDIO_MAX_VOICES];
static _Atomic uint32_t g_voice_volume[HOST_AUDIO_MAX_VOICES];

static OSStatus host_audio_render(void *in_ref_con,
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

    bool running = atomic_load_explicit(&g_audio_running, memory_order_relaxed);
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

                phase += ((float)freq / (float)HOST_AUDIO_SAMPLE_RATE);
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

static bool host_audio_init_unit(void) {
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
    asbd.mSampleRate = HOST_AUDIO_SAMPLE_RATE;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    asbd.mBytesPerPacket = sizeof(float);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = sizeof(float);
    asbd.mChannelsPerFrame = HOST_AUDIO_CHANNELS;
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
    cb.inputProc = host_audio_render;
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
        if (atomic_load_explicit(&g_tick_enabled, memory_order_relaxed)) {
            audio_engine_tick();
        }
        usleep(1000);
    }
    return NULL;
}
#endif

void audio_backend_init(int voice_count) {
    if (voice_count < 1) voice_count = 1;
    if (voice_count > HOST_AUDIO_MAX_VOICES) voice_count = HOST_AUDIO_MAX_VOICES;
    g_host_voice_count = voice_count;
    for (int i = 0; i < HOST_AUDIO_MAX_VOICES; i++) {
        g_voice_phase[i] = 0.0f;
        atomic_store_explicit(&g_voice_freq[i], 0, memory_order_relaxed);
        atomic_store_explicit(&g_voice_volume[i], 0, memory_order_relaxed);
    }

    if (!host_audio_init_unit()) {
        if (g_output_unit) {
            AudioComponentInstanceDispose(g_output_unit);
            g_output_unit = NULL;
        }
        return;
    }

    atomic_store_explicit(&g_audio_available, true, memory_order_release);

#ifndef TINY_CLJ_TEST_RUNNER
    atomic_store_explicit(&g_tick_thread_running, true, memory_order_release);
    if (pthread_create(&g_tick_thread, NULL, host_tick_thread_main, NULL) != 0) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
    }
#endif
}

void audio_backend_shutdown(void) {
    audio_tick_stop();

#ifndef TINY_CLJ_TEST_RUNNER
    if (atomic_load_explicit(&g_tick_thread_running, memory_order_acquire)) {
        atomic_store_explicit(&g_tick_thread_running, false, memory_order_release);
        (void)pthread_join(g_tick_thread, NULL);
    }
#endif

    if (g_output_unit) {
        AudioOutputUnitStop(g_output_unit);
        AudioUnitUninitialize(g_output_unit);
        AudioComponentInstanceDispose(g_output_unit);
        g_output_unit = NULL;
    }
    atomic_store_explicit(&g_audio_available, false, memory_order_release);
}

void audio_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume) {
    if (voice_index < 0 || voice_index >= g_host_voice_count) return;
    atomic_store_explicit(&g_voice_freq[voice_index], (uint32_t)freq_hz, memory_order_relaxed);
    atomic_store_explicit(&g_voice_volume[voice_index], (uint32_t)volume, memory_order_relaxed);
}

void audio_tick_start(void) {
    g_audio_engine.tick_running = true;
    atomic_store_explicit(&g_tick_enabled, true, memory_order_release);
    atomic_store_explicit(&g_audio_running, true, memory_order_release);
    if (atomic_load_explicit(&g_audio_available, memory_order_acquire) && g_output_unit) {
        (void)AudioOutputUnitStart(g_output_unit);
    }
}

void audio_tick_stop(void) {
    g_audio_engine.tick_running = false;
    atomic_store_explicit(&g_tick_enabled, false, memory_order_release);
    atomic_store_explicit(&g_audio_running, false, memory_order_release);
    if (g_output_unit) {
        (void)AudioOutputUnitStop(g_output_unit);
    }
}

bool audio_backend_host_get_status(AudioHostStatus *out) {
    if (!out) return false;
    out->backend_available = atomic_load_explicit(&g_audio_available, memory_order_acquire);
    out->audio_running = atomic_load_explicit(&g_audio_running, memory_order_acquire);
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

void audio_backend_init(int voice_count) {
    (void)voice_count;
}

void audio_backend_shutdown(void) {
}

void audio_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume) {
    (void)voice_index;
    (void)freq_hz;
    (void)volume;
}

void audio_tick_start(void) {
    g_audio_engine.tick_running = true;
}

void audio_tick_stop(void) {
    g_audio_engine.tick_running = false;
}

bool audio_backend_host_get_status(AudioHostStatus *out) {
    if (!out) return false;
    out->backend_available = false;
    out->audio_running = false;
    out->tick_enabled = false;
    out->tick_thread_running = false;
    out->voice_count = 0;
    return false;
}

#endif

#endif /* !ESP32_BUILD */
