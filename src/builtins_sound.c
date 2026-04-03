/*
 * Native sound builtins for Clojure API.
 *
 * sound-play-music!, sound-stop-track!, sound-stop-music!, sound-play-sfx!,
 * sound-stop-all!, sound-set-track-volume!, sound-set-music-volume!,
 * sound-on-finished!
 *
 * DEBUG-only helpers:
 * play-test-tone!, play-test-ramp!, play-test-ramp-noise! (DEBUG only), host-status!
 */

#include <string.h>

#include "sound_engine.h"
#include "builtins_sound.h"
#include "validation.h"
#include "value.h"
#include "symbol_cache.h"
#include "symbol.h"
#include "map.h"
#include "byte_array.h"
#include "exception.h"

#define TINYCLJ_SOUND_NATIVE_NS "tiny-fx.sound"
#define TINYCLJ_SOUND_DEBUG_NS "tiny-fx.sound-debug"

/* ========================================================================= */
/* Native function implementations                                           */
/* ========================================================================= */

static bool tinyclj_sound_ns_matches(const char *cname, size_t ns_len, const char *ns_name) {
    size_t expected_len = strlen(ns_name);
    return cname && ns_len == expected_len && strncmp(cname, ns_name, expected_len) == 0;
}

#ifdef DEBUG
static CljSymbol *g_sound_kw_backend_available = NULL;
static CljSymbol *g_sound_kw_sound_running = NULL;
static CljSymbol *g_sound_kw_tick_enabled = NULL;
static CljSymbol *g_sound_kw_tick_thread_running = NULL;
static CljSymbol *g_sound_kw_tick_running = NULL;
static CljSymbol *g_sound_kw_voice_count = NULL;
static CljSymbol *g_sound_kw_debug_noise_active = NULL;
static CljSymbol *g_sound_kw_debug_ramp_active = NULL;
static CljSymbol *g_sound_kw_debug_ramp_noise_active = NULL;
static CljSymbol *g_sound_kw_engine_voice_freqs = NULL;
static CljSymbol *g_sound_kw_engine_voice_gates = NULL;
static CljSymbol *g_sound_kw_engine_voice_active = NULL;
static CljSymbol *g_sound_kw_supported = NULL;
static CljSymbol *g_sound_kw_host_test_tone = NULL;
static const SymbolCacheEntry g_sound_symbol_cache[] = {
    {&g_sound_kw_backend_available, ":backend-available", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_sound_running, ":sound-running", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_tick_enabled, ":tick-enabled", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_tick_thread_running, ":tick-thread-running", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_tick_running, ":tick-running", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_voice_count, ":voice-count", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_debug_noise_active, ":debug-noise-active", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_debug_ramp_active, ":debug-ramp-active", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_debug_ramp_noise_active, ":debug-ramp-noise-active", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_engine_voice_freqs, ":engine-voice-freqs", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_engine_voice_gates, ":engine-voice-gates", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_engine_voice_active, ":engine-voice-active", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_supported, ":supported", SYMBOL_CACHE_SCOPE_GLOBAL},
    {&g_sound_kw_host_test_tone, ":__host-test-tone", SYMBOL_CACHE_SCOPE_GLOBAL},
};
#endif

static void builtins_sound_init_symbols(void) {
#ifdef DEBUG
    (void)symbol_cache_init_global(g_sound_symbol_cache,
                                   sizeof(g_sound_symbol_cache) / sizeof(g_sound_symbol_cache[0]));
#endif
}

#if TINY_FX_ENABLED
static void ensure_sound_engine_initialized(void) {
    if (g_sound_engine.voice_count > 0) return;
#ifdef ESP32_BUILD
    sound_engine_init(2);
#else
    sound_engine_init(SOUND_MAX_VOICES);
#endif
}

ID native_sound_play_music(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 3, "sound-play-music!")) return NULL;

    ensure_sound_engine_initialized();

    ID track_id = args[0];
    ID bytes_obj = args[1];
    ID repeat_arg = args[2];

    if (!track_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "sound-play-music! track-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t repeat = 1;
    if (repeat_arg && is_fixnum((CljValue)repeat_arg)) {
        repeat = (int32_t)as_fixnum((CljValue)repeat_arg);
    }

    bool ok = sound_engine_play_music(track_id, bytes_obj, repeat);
    return ok ? clj_true : clj_false;
}

ID native_sound_stop_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "sound-stop-track!")) return NULL;

    ensure_sound_engine_initialized();

    ID track_id = args[0];
    if (!track_id) return clj_false;

    bool ok = sound_engine_stop_track(track_id);
    return ok ? clj_true : clj_false;
}

ID native_sound_stop_music(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "sound-stop-music!")) return NULL;
    (void)args;

    ensure_sound_engine_initialized();

    sound_engine_stop_music();
    return NULL;
}

ID native_sound_play_sfx(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "sound-play-sfx!")) return NULL;

    ensure_sound_engine_initialized();

    ID sfx_id = args[0];
    ID bytes_obj = args[1];
    if (!sfx_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "sound-play-sfx! sfx-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    bool ok = sound_engine_play_sfx(sfx_id, bytes_obj);
    return ok ? clj_true : clj_false;
}

ID native_sound_stop_all(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "sound-stop-all!")) return NULL;
    (void)args;

    ensure_sound_engine_initialized();

    sound_engine_stop_all();
    return NULL;
}

ID native_sound_set_track_volume(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "sound-set-track-volume!")) return NULL;

    ensure_sound_engine_initialized();

    ID track_id = args[0];
    ID vol_arg = args[1];

    if (!track_id) return clj_false;
    if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "sound-set-track-volume! volume must be integer 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t vol = (int32_t)as_fixnum((CljValue)vol_arg);
    bool ok = sound_engine_set_track_volume(track_id, vol);
    return ok ? clj_true : clj_false;
}

ID native_sound_set_music_volume(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "sound-set-music-volume!")) return NULL;

    ensure_sound_engine_initialized();

    ID vol_arg = args[0];
    if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "sound-set-music-volume! volume must be integer 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t vol = (int32_t)as_fixnum((CljValue)vol_arg);
    sound_engine_set_music_volume(vol);
    return NULL;
}

ID native_sound_on_finished(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "sound-on-finished!")) return NULL;

    ensure_sound_engine_initialized();

    ID fn = args[0];
    sound_engine_on_finished(fn);
    return NULL;
}

#ifdef DEBUG
ID native_sound_host_status(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "host-status!")) return NULL;
    (void)args;

    builtins_sound_init_symbols();
    ensure_sound_engine_initialized();

    SoundHostStatus st = {0};
    bool ok = sound_backend_host_get_status(&st);

    ID v_backend_available = st.backend_available ? clj_true : clj_false;
    ID v_sound_running = st.sound_running ? clj_true : clj_false;
    ID v_tick_enabled = st.tick_enabled ? clj_true : clj_false;
    ID v_tick_thread_running = st.tick_thread_running ? clj_true : clj_false;
    ID v_tick_running = g_sound_engine.tick_running ? clj_true : clj_false;
    ID v_voice_count = fixnum(st.voice_count);
    ID v_debug_noise_active = st.debug_noise_active ? clj_true : clj_false;
    ID v_debug_ramp_active = st.debug_ramp_active ? clj_true : clj_false;
    ID v_debug_ramp_noise_active = st.debug_ramp_noise_active ? clj_true : clj_false;
    ID v_supported = ok ? clj_true : clj_false;
    CljPersistentVector *engine_voice_freqs = make_vector(g_sound_engine.voice_count, STRONG);
    CljPersistentVector *engine_voice_gates = make_vector(g_sound_engine.voice_count, STRONG);
    CljPersistentVector *engine_voice_active = make_vector(g_sound_engine.voice_count, STRONG);
    for (int i = 0; i < g_sound_engine.voice_count; i++) {
        SoundVoice *v = &g_sound_engine.voices[i];
        engine_voice_freqs = vector_conj(engine_voice_freqs, fixnum(v->freq_hz));
        engine_voice_gates = vector_conj(engine_voice_gates, fixnum((int32_t)v->gate_remaining_ticks));
        engine_voice_active = vector_conj(engine_voice_active, v->active ? clj_true : clj_false);
    }

    return make_map_from_kv(13,
                            g_sound_kw_supported, v_supported,
                            g_sound_kw_backend_available, v_backend_available,
                            g_sound_kw_sound_running, v_sound_running,
                            g_sound_kw_tick_enabled, v_tick_enabled,
                            g_sound_kw_tick_thread_running, v_tick_thread_running,
                            g_sound_kw_tick_running, v_tick_running,
                            g_sound_kw_voice_count, v_voice_count,
                            g_sound_kw_debug_noise_active, v_debug_noise_active,
                            g_sound_kw_debug_ramp_active, v_debug_ramp_active,
                            g_sound_kw_debug_ramp_noise_active, v_debug_ramp_noise_active,
                            g_sound_kw_engine_voice_freqs, engine_voice_freqs,
                            g_sound_kw_engine_voice_gates, engine_voice_gates,
                            g_sound_kw_engine_voice_active, engine_voice_active);
}

static uint8_t sound_encode_varuint(uint32_t value, uint8_t *out) {
    uint8_t len = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7FU);
        value >>= 7;
        if (value) byte |= 0x80U;
        out[len++] = byte;
    } while (value && len < 5);
    return len;
}

static uint8_t sound_nearest_midi_note(uint16_t freq_hz) {
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

ID native_sound_play_test_tone(ID *args, unsigned int argc) {
    if (argc != 2 && argc != 3) {
        throw_exception(EXCEPTION_ARITY,
                        "play-test-tone! expects [freq-hz duration-ms] or [freq-hz duration-ms volume]",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID freq_arg = args[0];
    ID duration_arg = args[1];
    if (!freq_arg || !is_fixnum((CljValue)freq_arg) || !duration_arg || !is_fixnum((CljValue)duration_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-tone! expects [freq-hz duration-ms] as integers",
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
                            "play-test-tone! volume must be integer 0..255",
                            __FILE__, __LINE__, 0);
            return NULL;
        }
        volume = (int32_t)as_fixnum((CljValue)vol_arg);
    }

    if (freq_hz < 20 || freq_hz > 20000 || duration_ms < 1 || duration_ms > 60000 || volume < 0 || volume > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-tone! valid ranges: freq 20..20000 Hz, duration 1..60000 ms, volume 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    uint8_t gate_varuint[5] = {0};
    uint8_t gate_len = sound_encode_varuint((uint32_t)duration_ms, gate_varuint);
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
    d[off++] = sound_nearest_midi_note((uint16_t)freq_hz);
    for (uint8_t i = 0; i < gate_len; i++) d[off++] = gate_varuint[i];
    d[off++] = (uint8_t)((TRK1_EVT_END << 4) | 0);

    ensure_sound_engine_initialized();

    builtins_sound_init_symbols();
    ID track_id = g_sound_kw_host_test_tone;
    ID ba_id = ba;
    bool played = sound_engine_play_music(track_id, ba_id, 1);
    RELEASE(ba_id);
    return played ? clj_true : clj_false;
}

ID native_sound_play_test_noise(ID *args, unsigned int argc) {
    if (argc != 5) {
        throw_exception(EXCEPTION_ARITY,
                        "play-test-noise! expects [min-freq-hz max-freq-hz duration-ms hop-ms volume]",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID min_freq_arg = args[0];
    ID max_freq_arg = args[1];
    ID duration_arg = args[2];
    ID hop_arg = args[3];
    ID volume_arg = args[4];
    if (!min_freq_arg || !is_fixnum((CljValue)min_freq_arg) ||
        !max_freq_arg || !is_fixnum((CljValue)max_freq_arg) ||
        !duration_arg || !is_fixnum((CljValue)duration_arg) ||
        !hop_arg || !is_fixnum((CljValue)hop_arg) ||
        !volume_arg || !is_fixnum((CljValue)volume_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-noise! expects five integers",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t min_freq_hz = (int32_t)as_fixnum((CljValue)min_freq_arg);
    int32_t max_freq_hz = (int32_t)as_fixnum((CljValue)max_freq_arg);
    int32_t duration_ms = (int32_t)as_fixnum((CljValue)duration_arg);
    int32_t hop_ms = (int32_t)as_fixnum((CljValue)hop_arg);
    int32_t volume = (int32_t)as_fixnum((CljValue)volume_arg);
    if (min_freq_hz < 20 || max_freq_hz > 20000 || min_freq_hz > max_freq_hz ||
        duration_ms < 1 || duration_ms > 60000 || hop_ms < 1 || hop_ms > 1000 ||
        volume < 0 || volume > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-noise! valid ranges: min/max freq 20..20000 Hz, duration 1..60000 ms, hop 1..1000 ms, volume 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ensure_sound_engine_initialized();
#ifdef ESP32_BUILD
    (void)min_freq_hz; (void)max_freq_hz; (void)duration_ms; (void)hop_ms; (void)volume;
    return clj_false;
#else
    bool ok = sound_backend_host_play_debug_noise((uint16_t)min_freq_hz,
                                                  (uint16_t)max_freq_hz,
                                                  (uint32_t)duration_ms,
                                                  (uint32_t)hop_ms,
                                                  (uint8_t)volume);
    return ok ? clj_true : clj_false;
#endif
}

ID native_sound_play_test_ramp(ID *args, unsigned int argc) {
    if (argc != 4) {
        throw_exception(EXCEPTION_ARITY,
                        "play-test-ramp! expects [start-freq-hz end-freq-hz duration-ms volume]",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID start_arg = args[0];
    ID end_arg = args[1];
    ID duration_arg = args[2];
    ID volume_arg = args[3];
    if (!start_arg || !is_fixnum((CljValue)start_arg) ||
        !end_arg || !is_fixnum((CljValue)end_arg) ||
        !duration_arg || !is_fixnum((CljValue)duration_arg) ||
        !volume_arg || !is_fixnum((CljValue)volume_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-ramp! expects four integers",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t start_freq_hz = (int32_t)as_fixnum((CljValue)start_arg);
    int32_t end_freq_hz = (int32_t)as_fixnum((CljValue)end_arg);
    int32_t duration_ms = (int32_t)as_fixnum((CljValue)duration_arg);
    int32_t volume = (int32_t)as_fixnum((CljValue)volume_arg);
    if (start_freq_hz < 20 || start_freq_hz > 20000 || end_freq_hz < 20 || end_freq_hz > 20000 ||
        duration_ms < 1 || duration_ms > 60000 || volume < 0 || volume > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-ramp! valid ranges: start/end freq 20..20000 Hz, duration 1..60000 ms, volume 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ensure_sound_engine_initialized();
#ifdef ESP32_BUILD
    (void)start_freq_hz; (void)end_freq_hz; (void)duration_ms; (void)volume;
    return clj_false;
#else
    bool ok = sound_backend_host_play_debug_ramp((uint16_t)start_freq_hz,
                                                 (uint16_t)end_freq_hz,
                                                 (uint32_t)duration_ms,
                                                 (uint8_t)volume);
    return ok ? clj_true : clj_false;
#endif
}

ID native_sound_play_test_ramp_noise(ID *args, unsigned int argc) {
    if (argc != 5) {
        throw_exception(EXCEPTION_ARITY,
                        "play-test-ramp-noise! expects [min-freq-hz max-freq-hz duration-ms hop-ms volume]",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ID min_freq_arg = args[0];
    ID max_freq_arg = args[1];
    ID duration_arg = args[2];
    ID hop_arg = args[3];
    ID volume_arg = args[4];
    if (!min_freq_arg || !is_fixnum((CljValue)min_freq_arg) ||
        !max_freq_arg || !is_fixnum((CljValue)max_freq_arg) ||
        !duration_arg || !is_fixnum((CljValue)duration_arg) ||
        !hop_arg || !is_fixnum((CljValue)hop_arg) ||
        !volume_arg || !is_fixnum((CljValue)volume_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-ramp-noise! expects five integers",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t min_freq_hz = (int32_t)as_fixnum((CljValue)min_freq_arg);
    int32_t max_freq_hz = (int32_t)as_fixnum((CljValue)max_freq_arg);
    int32_t duration_ms = (int32_t)as_fixnum((CljValue)duration_arg);
    int32_t hop_ms = (int32_t)as_fixnum((CljValue)hop_arg);
    int32_t volume = (int32_t)as_fixnum((CljValue)volume_arg);
    if (min_freq_hz < 20 || max_freq_hz > 20000 || min_freq_hz > max_freq_hz ||
        duration_ms < 1 || duration_ms > 60000 || hop_ms < 1 || hop_ms > 1000 ||
        volume < 0 || volume > 255) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "play-test-ramp-noise! valid ranges: min/max freq 20..20000 Hz, duration 1..60000 ms, hop 1..1000 ms, volume 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    ensure_sound_engine_initialized();
#ifdef ESP32_BUILD
    (void)min_freq_hz; (void)max_freq_hz; (void)duration_ms; (void)hop_ms; (void)volume;
    return clj_false;
#else
    bool ok = sound_backend_host_play_debug_ramp_noise((uint16_t)min_freq_hz,
                                                       (uint16_t)max_freq_hz,
                                                       (uint32_t)duration_ms,
                                                       (uint32_t)hop_ms,
                                                       (uint8_t)volume);
    return ok ? clj_true : clj_false;
#endif
}
#endif
#else
static ID tinyclj_tiny_fx_disabled_error(const char *fn_name) {
    throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                              "tiny-fx is disabled; %s is unavailable",
                              fn_name ? fn_name : "feature");
    return NULL;
}

#define TINYCLJ_DEFINE_SOUND_DISABLED_STUB(fn_name, display_name) \
    ID fn_name(ID *args, unsigned int argc) {                    \
        (void)args;                                              \
        (void)argc;                                              \
        return tinyclj_tiny_fx_disabled_error(display_name);     \
    }

TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_play_music, "sound-play-music!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_stop_track, "sound-stop-track!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_stop_music, "sound-stop-music!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_play_sfx, "sound-play-sfx!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_stop_all, "sound-stop-all!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_set_track_volume, "sound-set-track-volume!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_set_music_volume, "sound-set-music-volume!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_on_finished, "sound-on-finished!")

#ifdef DEBUG
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_play_test_tone, "play-test-tone!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_play_test_noise, "play-test-noise!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_play_test_ramp, "play-test-ramp!")
TINYCLJ_DEFINE_SOUND_DISABLED_STUB(native_sound_play_test_ramp_noise, "play-test-ramp-noise!")

ID native_sound_host_status(ID *args, unsigned int argc) {
    (void)args;
    (void)argc;
    builtins_sound_init_symbols();
    CljPersistentMap *m = make_map(1);
    if (m && g_sound_kw_supported) {
        map_assoc_inplace(&m, g_sound_kw_supported, clj_false);
    }
    return AUTORELEASE(m);
}
#endif
#undef TINYCLJ_DEFINE_SOUND_DISABLED_STUB
#endif

/* ========================================================================= */
/* Namespace registration helpers                                            */
/* ========================================================================= */

bool builtins_sound_namespace_allowed(const char *cname, size_t ns_len) {
    if (tinyclj_sound_ns_matches(cname, ns_len, TINYCLJ_SOUND_NATIVE_NS)) {
        return true;
    }
#ifdef DEBUG
    if (tinyclj_sound_ns_matches(cname, ns_len, TINYCLJ_SOUND_DEBUG_NS)) {
        return true;
    }
#endif
    return false;
}

/**
 * @brief Register curated native sound builtins in the public tiny-fx namespaces.
 * @param registrar Callback used to bind a qualified cname to a native builtin.
 * @return No return value.
 */
void builtins_sound_register(BuiltinsSoundRegisterFn registrar) {
    if (!registrar) {
        return;
    }
    builtins_sound_init_symbols();
#if TINY_FX_ENABLED
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-play-music!", native_sound_play_music);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-stop-track!", native_sound_stop_track);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-stop-music!", native_sound_stop_music);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-play-sfx!", native_sound_play_sfx);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-stop-all!", native_sound_stop_all);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-set-track-volume!", native_sound_set_track_volume);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-set-music-volume!", native_sound_set_music_volume);
    registrar(TINYCLJ_SOUND_NATIVE_NS "/sound-on-finished!", native_sound_on_finished);
#ifdef DEBUG
    registrar(TINYCLJ_SOUND_DEBUG_NS "/play-test-tone!", native_sound_play_test_tone);
    registrar(TINYCLJ_SOUND_DEBUG_NS "/play-test-noise!", native_sound_play_test_noise);
    registrar(TINYCLJ_SOUND_DEBUG_NS "/play-test-ramp!", native_sound_play_test_ramp);
    registrar(TINYCLJ_SOUND_DEBUG_NS "/play-test-ramp-noise!", native_sound_play_test_ramp_noise);
    registrar(TINYCLJ_SOUND_DEBUG_NS "/host-status!", native_sound_host_status);
#endif
#endif
}
