/*
 * Native audio builtins for Clojure API.
 *
 * audio-load-track!, audio-unload-track!, audio-play-music!,
 * audio-stop-track!, audio-stop-music!, audio-play-sfx!,
 * audio-stop-all!, audio-set-track-volume!, audio-set-music-volume!,
 * audio-on-finished!, audio-play-test-tone!, audio-host-status!
 */

#include "audio_engine.h"
#include "builtins.h"
#include "validation.h"
#include "value.h"
#include "symbol.h"
#include "map.h"
#include "byte_array.h"
#include "exception.h"

/* ========================================================================= */
/* Native function implementations                                           */
/* ========================================================================= */

static void ensure_audio_engine_initialized(void) {
    if (g_audio_engine.voice_count > 0) return;
#ifdef ESP32_BUILD
    audio_engine_init(2);
#else
    audio_engine_init(AUDIO_MAX_VOICES);
#endif
}

ID native_audio_load_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "audio-load-track!")) return NULL;

    ensure_audio_engine_initialized();

    ID track_id = args[0];
    ID bytes_obj = args[1];

    if (!track_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-load-track! track-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    bool ok = audio_engine_load_track(track_id, bytes_obj);
    return ok ? clj_true : clj_false;
}

ID native_audio_unload_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-unload-track!")) return NULL;

    ensure_audio_engine_initialized();

    ID track_id = args[0];
    if (!track_id) return clj_false;

    bool ok = audio_engine_unload_track(track_id);
    return ok ? clj_true : clj_false;
}

ID native_audio_play_music(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "audio-play-music!")) return NULL;

    ensure_audio_engine_initialized();

    ID track_id = args[0];
    ID repeat_arg = args[1];

    if (!track_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-play-music! track-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t repeat = 1;
    if (repeat_arg && is_fixnum((CljValue)repeat_arg)) {
        repeat = (int32_t)as_fixnum((CljValue)repeat_arg);
    }

    bool ok = audio_engine_play_music(track_id, repeat);
    return ok ? clj_true : clj_false;
}

ID native_audio_stop_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-stop-track!")) return NULL;

    ensure_audio_engine_initialized();

    ID track_id = args[0];
    if (!track_id) return clj_false;

    bool ok = audio_engine_stop_track(track_id);
    return ok ? clj_true : clj_false;
}

ID native_audio_stop_music(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "audio-stop-music!")) return NULL;
    (void)args;

    ensure_audio_engine_initialized();

    audio_engine_stop_music();
    return NULL;
}

ID native_audio_play_sfx(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-play-sfx!")) return NULL;

    ensure_audio_engine_initialized();

    ID sfx_id = args[0];
    if (!sfx_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-play-sfx! sfx-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    bool ok = audio_engine_play_sfx(sfx_id);
    return ok ? clj_true : clj_false;
}

ID native_audio_stop_all(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "audio-stop-all!")) return NULL;
    (void)args;

    ensure_audio_engine_initialized();

    audio_engine_stop_all();
    return NULL;
}

ID native_audio_set_track_volume(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "audio-set-track-volume!")) return NULL;

    ensure_audio_engine_initialized();

    ID track_id = args[0];
    ID vol_arg = args[1];

    if (!track_id) return clj_false;
    if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-set-track-volume! volume must be integer 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t vol = (int32_t)as_fixnum((CljValue)vol_arg);
    bool ok = audio_engine_set_track_volume(track_id, vol);
    return ok ? clj_true : clj_false;
}

ID native_audio_set_music_volume(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-set-music-volume!")) return NULL;

    ensure_audio_engine_initialized();

    ID vol_arg = args[0];
    if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-set-music-volume! volume must be integer 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t vol = (int32_t)as_fixnum((CljValue)vol_arg);
    audio_engine_set_music_volume(vol);
    return NULL;
}

ID native_audio_on_finished(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-on-finished!")) return NULL;

    ensure_audio_engine_initialized();

    ID fn = args[0];
    audio_engine_on_finished(fn);
    return NULL;
}

ID native_audio_host_status(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "audio-host-status!")) return NULL;
    (void)args;

    ensure_audio_engine_initialized();

    AudioHostStatus st = {0};
    bool ok = audio_backend_host_get_status(&st);
    ID k_backend_available = intern_symbol_global(":backend-available");
    ID k_audio_running = intern_symbol_global(":audio-running");
    ID k_tick_enabled = intern_symbol_global(":tick-enabled");
    ID k_tick_thread_running = intern_symbol_global(":tick-thread-running");
    ID k_tick_running = intern_symbol_global(":tick-running");
    ID k_voice_count = intern_symbol_global(":voice-count");
    ID k_engine_voice_freqs = intern_symbol_global(":engine-voice-freqs");
    ID k_engine_voice_gates = intern_symbol_global(":engine-voice-gates");
    ID k_engine_voice_active = intern_symbol_global(":engine-voice-active");
    ID k_supported = intern_symbol_global(":supported");

    ID v_backend_available = st.backend_available ? clj_true : clj_false;
    ID v_audio_running = st.audio_running ? clj_true : clj_false;
    ID v_tick_enabled = st.tick_enabled ? clj_true : clj_false;
    ID v_tick_thread_running = st.tick_thread_running ? clj_true : clj_false;
    ID v_tick_running = g_audio_engine.tick_running ? clj_true : clj_false;
    ID v_voice_count = fixnum(st.voice_count);
    ID v_supported = ok ? clj_true : clj_false;
    CljPersistentVector *engine_voice_freqs = make_vector(g_audio_engine.voice_count, STRONG);
    CljPersistentVector *engine_voice_gates = make_vector(g_audio_engine.voice_count, STRONG);
    CljPersistentVector *engine_voice_active = make_vector(g_audio_engine.voice_count, STRONG);
    for (int i = 0; i < g_audio_engine.voice_count; i++) {
        AudioVoice *v = &g_audio_engine.voices[i];
        engine_voice_freqs = vector_conj(engine_voice_freqs, fixnum(v->freq_hz));
        engine_voice_gates = vector_conj(engine_voice_gates, fixnum((int32_t)v->gate_remaining_ticks));
        engine_voice_active = vector_conj(engine_voice_active, v->active ? clj_true : clj_false);
    }

    return make_map_from_kv(10,
                            k_supported, v_supported,
                            k_backend_available, v_backend_available,
                            k_audio_running, v_audio_running,
                            k_tick_enabled, v_tick_enabled,
                            k_tick_thread_running, v_tick_thread_running,
                            k_tick_running, v_tick_running,
                            k_voice_count, v_voice_count,
                            k_engine_voice_freqs, engine_voice_freqs,
                            k_engine_voice_gates, engine_voice_gates,
                            k_engine_voice_active, engine_voice_active);
}

static uint8_t audio_encode_varuint(uint32_t value, uint8_t *out) {
    uint8_t len = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7FU);
        value >>= 7;
        if (value) byte |= 0x80U;
        out[len++] = byte;
    } while (value && len < 5);
    return len;
}

static uint8_t audio_nearest_midi_note(uint16_t freq_hz) {
    uint8_t best_note = 69;
    uint16_t best_diff = 0xFFFFU;
    for (uint8_t note = 1; note <= 127; note++) {
        uint16_t note_freq = midi_note_to_freq(note);
        uint16_t diff = (note_freq > freq_hz) ? (note_freq - freq_hz) : (freq_hz - note_freq);
        if (diff < best_diff) {
            best_diff = diff;
            best_note = note;
        }
    }
    return best_note;
}

ID native_audio_play_test_tone(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        throw_exception(EXCEPTION_ARITY,
                        "audio-play-test-tone! expects [freq-hz duration-ms] or [freq-hz duration-ms volume]",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID freq_arg = args[0];
    ID duration_arg = args[1];
    if (!freq_arg || !is_fixnum((CljValue)freq_arg) || !duration_arg || !is_fixnum((CljValue)duration_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-play-test-tone! expects [freq-hz duration-ms] as integers",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t freq_hz = (int32_t)as_fixnum((CljValue)freq_arg);
    int32_t duration_ms = (int32_t)as_fixnum((CljValue)duration_arg);
    int32_t volume = 255;
    if (argc == 3) {
        ID vol_arg = args[2];
        if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                            "audio-play-test-tone! volume must be integer 0..255",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        volume = (int32_t)as_fixnum((CljValue)vol_arg);
    }

    if (freq_hz < 20 || freq_hz > 20000 || duration_ms < 1 || duration_ms > 60000 || volume < 0 || volume > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-play-test-tone! valid ranges: freq 20..20000 Hz, duration 1..60000 ms, volume 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    uint8_t gate_varuint[5] = {0};
    uint8_t gate_len = audio_encode_varuint((uint32_t)duration_ms, gate_varuint);
    uint32_t stream_len = (uint32_t)(1 + 1 + 1 + 1 + gate_len + 1);
    uint32_t total_len = TRK1_HEADER_SIZE + stream_len;

    CljByteArray *ba = make_byte_array((int)total_len);
    if (!ba) return clj_false;

    uint8_t *d = ba->data;
    d[0] = 'T'; d[1] = 'R'; d[2] = 'K'; d[3] = '1';
    d[4] = TRK1_VERSION;
    d[5] = 0;
    d[6] = 1;
    d[7] = 0;
    d[8] = 1; d[9] = 0;       /* tpq */
    d[10] = 60; d[11] = 0;    /* bpm */
    d[12] = (uint8_t)(stream_len & 0xFFU);
    d[13] = (uint8_t)((stream_len >> 8) & 0xFFU);
    d[14] = (uint8_t)((stream_len >> 16) & 0xFFU);
    d[15] = (uint8_t)((stream_len >> 24) & 0xFFU);
    d[16] = d[17] = d[18] = d[19] = 0;

    uint32_t off = TRK1_HEADER_SIZE;
    d[off++] = (uint8_t)((TRK1_EVT_SET_VOL << 4) | 0);
    d[off++] = (uint8_t)volume;
    d[off++] = (uint8_t)((TRK1_EVT_NOTE << 4) | 0);
    d[off++] = audio_nearest_midi_note((uint16_t)freq_hz);
    for (uint8_t i = 0; i < gate_len; i++) d[off++] = gate_varuint[i];
    d[off++] = (uint8_t)((TRK1_EVT_END << 4) | 0);

    ensure_audio_engine_initialized();

    ID track_id = intern_symbol_global(":__host-test-tone");
    ID ba_id = ba;
    bool loaded = audio_engine_load_track(track_id, ba_id);
    RELEASE(ba_id);
    if (!loaded) return clj_false;

    bool played = audio_engine_play_music(track_id, 1);
    return played ? clj_true : clj_false;
}
