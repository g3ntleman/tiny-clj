/*
 * Sound engine implementation (trk1 streaming, SPSC queue, push-based callbacks).
 *
 * Platform-agnostic: ESP32 LEDC and host stubs are in separate files.
 */

#include "sound_engine.h"
#include "byte_array.h"
#include "event_loop.h"
#include "eval.h"
#include "memory.h"
#include "symbol.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global sound engine instance */
SoundEngine g_sound_engine;
static ID KW_SOURCE = NULL;
static ID KW_KIND = NULL;
static ID KW_TRACK_ID = NULL;
static ID KW_AUDIO = NULL;
static ID KW_FINISHED = NULL;
static const IdSymbolCacheEntry g_sound_finished_event_kw_cache[] = {
    {&KW_SOURCE, ":source"},
    {&KW_KIND, ":kind"},
    {&KW_TRACK_ID, ":track-id"},
    {&KW_AUDIO, ":audio"},
    {&KW_FINISHED, ":finished"},
};

#if defined(DEBUG) || defined(TINY_CLJ_TEST_RUNNER)
__attribute__((weak)) void tinyclj_sound_backend_observe_set_voice(int voice_index,
                                                                   uint16_t freq_hz,
                                                                   uint8_t volume,
                                                                   bool retrigger) {
    (void)voice_index;
    (void)freq_hz;
    (void)volume;
    (void)retrigger;
}
#else
static inline void tinyclj_sound_backend_observe_set_voice(int voice_index,
                                                           uint16_t freq_hz,
                                                           uint8_t volume,
                                                           bool retrigger) {
    (void)voice_index;
    (void)freq_hz;
    (void)volume;
    (void)retrigger;
}
#endif

#ifdef DEBUG
static bool sound_interp_debug_enabled(void) {
    static int init = 0;
    static bool enabled = false;
    if (!init) {
        const char *env = getenv("TINYCLJ_SOUND_INTERP_DEBUG");
        enabled = (env && env[0] != '0');
        init = 1;
    }
    return enabled;
}
#else
static inline bool sound_interp_debug_enabled(void) {
    return false;
}
#endif

/* ========================================================================= */
/* MIDI note -> frequency table (A4 = 440 Hz, equal temperament)             */
/* Notes 0..127; note 0 is treated as rest (freq 0).                         */
/* ========================================================================= */

static const uint16_t g_midi_freq_table[128] = {
       0,    9,    9,   10,   10,   11,   12,   12,   13,   14,   15,   15, /*   0-11 */
      16,   17,   18,   19,   21,   22,   23,   25,   26,   28,   29,   31, /*  12-23 */
      33,   35,   37,   39,   41,   44,   46,   49,   52,   55,   58,   62, /*  24-35 */
      65,   69,   73,   78,   82,   87,   92,   98,  104,  110,  117,  123, /*  36-47 */
     131,  139,  147,  156,  165,  175,  185,  196,  208,  220,  233,  247, /*  48-59 */
     262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494, /*  60-71 */
     523,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988, /*  72-83 */
    1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976, /* 84-95 */
    2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951, /* 96-107 */
    4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902, /* 108-119 */
    8372, 8870, 9397, 9956,10548,11175,11840,12544                           /* 120-127 */
};

uint16_t midi_note_to_freq(uint8_t note) {
    if (note > 127) return 0;
    return g_midi_freq_table[note];
}

/* ========================================================================= */
/* trk1 header parsing                                                       */
/* ========================================================================= */

bool trk1_parse_header(const uint8_t *data, int len, Trk1Header *out) {
    if (!data || !out || len < TRK1_HEADER_SIZE) return false;

    if (data[0] != TRK1_MAGIC_0 || data[1] != TRK1_MAGIC_1 ||
        data[2] != TRK1_MAGIC_2 || data[3] != TRK1_MAGIC_3)
        return false;

    if (data[4] != TRK1_VERSION) return false;

    out->version = data[4];
    out->flags = data[5];
    out->channel_count = data[6];
    if (out->channel_count == 0 || out->channel_count > TRK1_MAX_CHANNELS) return false;

    /* reserved byte must be 0 */
    if (data[7] != 0) return false;

    out->tpq = (uint16_t)(data[8] | (data[9] << 8));
    out->bpm = (uint16_t)(data[10] | (data[11] << 8));
    out->stream_len_bytes = (uint32_t)(data[12] | (data[13] << 8) |
                                       (data[14] << 16) | (data[15] << 24));
    out->crc32 = (uint32_t)(data[16] | (data[17] << 8) |
                             (data[18] << 16) | (data[19] << 24));

    if (out->tpq == 0 || out->bpm == 0) return false;

    /* stream must fit within data */
    if ((int)(TRK1_HEADER_SIZE + out->stream_len_bytes) > len) return false;

    out->stream_start = data + TRK1_HEADER_SIZE;
    return true;
}

/* ========================================================================= */
/* Varuint decoder (base-128)                                                */
/* ========================================================================= */

bool trk1_decode_varuint(const uint8_t **cursor, const uint8_t *end, uint32_t *out) {
    if (!cursor || !*cursor || !end || !out) return false;

    const uint8_t *p = *cursor;
    uint32_t result = 0;
    int shift = 0;

    while (p < end) {
        uint8_t byte = *p++;
        result |= (uint32_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) {
            if (result > TRK1_VARUINT_MAX) {
                return false;
            }
            *out = result;
            *cursor = p;
            return true;
        }
        shift += 7;
        if (shift >= 28) {
            /* Would overflow on next iteration */
            return false;
        }
    }
    return false; /* ran out of data */
}

static void sound_ensure_finished_event_keywords(void) {
    (void)id_symbol_cache_init_global(
        g_sound_finished_event_kw_cache,
        sizeof(g_sound_finished_event_kw_cache) / sizeof(g_sound_finished_event_kw_cache[0]));
}

static ID sound_make_finished_event(ID track_id) {
    sound_ensure_finished_event_keywords();
    return make_map_from_kv(3,
                            KW_SOURCE, KW_AUDIO,
                            KW_KIND, KW_FINISHED,
                            KW_TRACK_ID, track_id);
}

typedef struct {
    ID callback_fn;
    ID track_id;
    uint32_t epoch;
} SoundFinishedIngressCtx;

static uint32_t g_sound_engine_callback_epoch = 0u;
static uint32_t g_sound_engine_callback_epoch_counter = 0u;

#define SOUND_NON_LEGATO_INTER_NOTE_GAP_DEFAULT_TICKS 1u

static void sound_deferred_release_run(void *ctx, EvalState *st) {
    (void)st;
    ID retained_obj = ctx;
    RELEASE(retained_obj);
}

static void sound_release_retained_obj(ID retained_obj) {
    if (!retained_obj) {
        return;
    }
    if (event_loop_dispatch_native(sound_deferred_release_run,
                                   retained_obj,
                                   NULL)) {
        return;
    }
    RELEASE(retained_obj);
}

static void sound_finished_ingress_run(void *ctx, EvalState *st) {
    SoundFinishedIngressCtx *finished_ctx = (SoundFinishedIngressCtx *)ctx;
    if (!finished_ctx || !finished_ctx->callback_fn || !finished_ctx->track_id) {
        return;
    }
    if (finished_ctx->epoch != g_sound_engine_callback_epoch) {
        return;
    }

    ID event_payload = sound_make_finished_event(finished_ctx->track_id);
    if (!event_payload) {
        g_sound_engine.telemetry.finished_drop_count++;
        return;
    }

    ID args[1] = { event_payload };
    (void)eval_function_call(finished_ctx->callback_fn, args, 1u, NULL, st);
    RELEASE(event_payload);
}

static void sound_finished_ingress_cleanup(void *ctx) {
    SoundFinishedIngressCtx *finished_ctx = (SoundFinishedIngressCtx *)ctx;
    if (!finished_ctx) {
        return;
    }
    RELEASE(finished_ctx->callback_fn);
    CLJ_FREE(finished_ctx);
}

/* ========================================================================= */
/* Stream helpers                                                            */
/* ========================================================================= */

static void stream_release(SoundStream *s) {
    if (!s) {
        return;
    }
    sound_release_retained_obj(s->retained_obj);
    memset(s, 0, sizeof(*s));
}

static void stream_init(SoundStream *s,
                        ID track_id,
                        ID retained_obj,
                        const Trk1Header *header,
                        int32_t repeat) {
    if (!s || !header) {
        return;
    }
    stream_release(s);
    s->cursor = header->stream_start;
    s->stream_end = header->stream_start + header->stream_len_bytes;
    s->stream_start = header->stream_start;
    s->current_tick = 0;
    s->next_event_tick = 0;
    s->repeat_remaining = repeat;
    s->track_volume = 255;
    s->active = true;
    s->track_id = track_id;
    s->retained_obj = retained_obj;
    memset(&s->envelope, 0, sizeof(s->envelope));
    uint8_t configured_gap_ticks =
        (uint8_t)((header->flags & TRK1_FLAG_INTER_NOTE_GAP_MASK) >> TRK1_FLAG_INTER_NOTE_GAP_SHIFT);
    if ((header->flags & TRK1_FLAG_INTER_NOTE_GAP_EXT) != 0u) {
        configured_gap_ticks = (uint8_t)(configured_gap_ticks + 16u);
    }
    s->inter_note_gap_ticks =
        configured_gap_ticks > 0u ? configured_gap_ticks : SOUND_NON_LEGATO_INTER_NOTE_GAP_DEFAULT_TICKS;
}

static bool sound_envelope_apply_points(SoundEnvelope *env,
                                        const uint8_t *levels,
                                        uint8_t point_count) {
    if (!env || !levels || point_count == 0u || point_count > TRK1_MAX_ENVELOPE_POINTS) {
        return false;
    }
    memset(env, 0, sizeof(*env));
    env->point_count = point_count;
    uint8_t current_level = levels[0];
    uint8_t current_width = 1u;
    for (uint8_t i = 1u; i < point_count; i++) {
        uint8_t level = levels[i];
        if (level == current_level) {
            current_width++;
            continue;
        }
        env->segment_levels[env->segment_count] = current_level;
        env->segment_point_widths[env->segment_count] = current_width;
        env->segment_count++;
        current_level = level;
        current_width = 1u;
    }
    env->segment_levels[env->segment_count] = current_level;
    env->segment_point_widths[env->segment_count] = current_width;
    env->segment_count++;
    env->enabled = (env->segment_count > 1u);
    return true;
}

static void sound_voice_reset_envelope(SoundVoice *voice) {
    if (!voice) {
        return;
    }
    voice->envelope_enabled = false;
    voice->envelope_stage_count = 0u;
    voice->envelope_stage_index = 0u;
    voice->envelope_stage_remaining_ticks = 0u;
    memset(voice->envelope_stage_levels, 0, sizeof(voice->envelope_stage_levels));
    memset(voice->envelope_stage_ticks, 0, sizeof(voice->envelope_stage_ticks));
}

static uint8_t sound_scale_volume_u8(uint8_t base_volume, uint8_t level) {
    return (uint8_t)(((uint16_t)base_volume * (uint16_t)level) / 255u);
}

static void sound_voice_apply_envelope_profile(SoundVoice *voice,
                                               const SoundEnvelope *env,
                                               uint32_t gate_ticks,
                                               bool disable_envelope) {
    if (!voice) {
        return;
    }
    sound_voice_reset_envelope(voice);
    if (!env || !env->enabled || disable_envelope || gate_ticks == 0u) {
        voice->volume = voice->base_volume;
        return;
    }

    uint8_t stage_count = 0u;
    uint32_t allocated = 0u;
    for (uint8_t i = 0u; i < env->segment_count; i++) {
        uint32_t stage_ticks =
            (uint32_t)(((uint64_t)gate_ticks * (uint64_t)env->segment_point_widths[i]) /
                       (uint64_t)env->point_count);
        if (i == (uint8_t)(env->segment_count - 1u)) {
            stage_ticks = gate_ticks - allocated;
        } else {
            allocated += stage_ticks;
        }
        if (stage_ticks == 0u) {
            continue;
        }
        voice->envelope_stage_levels[stage_count] = env->segment_levels[i];
        voice->envelope_stage_ticks[stage_count] = stage_ticks;
        stage_count++;
    }

    if (stage_count <= 1u) {
        voice->volume = voice->base_volume;
        return;
    }

    voice->envelope_enabled = true;
    voice->envelope_stage_count = stage_count;
    voice->envelope_stage_index = 0u;
    voice->envelope_stage_remaining_ticks = voice->envelope_stage_ticks[0];
    voice->volume = sound_scale_volume_u8(voice->base_volume, voice->envelope_stage_levels[0]);
}

static uint32_t sound_note_effective_gate_ticks(uint32_t gate_ticks,
                                                uint32_t delay_ticks,
                                                uint8_t inter_note_gap_ticks,
                                                uint16_t freq_hz,
                                                uint8_t note_flags) {
    if (freq_hz == 0u || (note_flags & TRK1_NOTE_FLAG_LEGATO) != 0u || delay_ticks == 0u) {
        return gate_ticks;
    }
    if (inter_note_gap_ticks == 0u) {
        return gate_ticks;
    }
    if (delay_ticks <= inter_note_gap_ticks) {
        return 0u;
    }
    uint32_t max_gate_ticks = delay_ticks - inter_note_gap_ticks;
    return gate_ticks < max_gate_ticks ? gate_ticks : max_gate_ticks;
}

static inline void sound_voice_apply_note(SoundVoice *voice,
                                          const SoundEnvelope *env,
                                          uint16_t freq_hz,
                                          uint32_t gate_ticks,
                                          uint8_t volume,
                                          uint8_t note_flags) {
    if (!voice) {
        return;
    }
    voice->freq_hz = freq_hz;
    voice->gate_remaining_ticks = gate_ticks;
    voice->base_volume = volume;
    voice->volume = volume;
    voice->hold_until_next_note = ((note_flags & TRK1_NOTE_FLAG_LEGATO) != 0u) && (freq_hz > 0u);
    if ((note_flags & TRK1_NOTE_FLAG_RETRIGGER) != 0u && freq_hz > 0u) {
        voice->attack_generation++;
    }
    sound_voice_apply_envelope_profile(voice,
                                       env,
                                       gate_ticks,
                                       voice->hold_until_next_note || freq_hz == 0u);
    voice->active = true;
}

static inline void sound_voice_release_hold_if_idle(SoundVoice *voice) {
    if (!voice || !voice->hold_until_next_note) {
        return;
    }
    voice->hold_until_next_note = false;
    if (voice->gate_remaining_ticks == 0u) {
        voice->freq_hz = 0u;
        voice->active = false;
    }
}

static inline void sound_voice_force_silence(SoundVoice *voice) {
    if (!voice) {
        return;
    }
    voice->active = false;
    voice->freq_hz = 0u;
    voice->gate_remaining_ticks = 0u;
    voice->hold_until_next_note = false;
    voice->base_volume = 0u;
    voice->volume = 0u;
    sound_voice_reset_envelope(voice);
}

static void sound_engine_release_all_voice_holds(void) {
    for (int i = 0; i < g_sound_engine.voice_count; i++) {
        sound_voice_release_hold_if_idle(&g_sound_engine.voices[i]);
    }
}

static bool stream_decode_note_event(SoundStream *s,
                                     uint8_t event_type,
                                     uint16_t *out_freq_hz,
                                     uint32_t *out_gate_ticks,
                                     uint8_t *out_note_flags,
                                     uint8_t *out_note) {
    bool hz_note = (event_type == TRK1_EVT_NOTE_HZ || event_type == TRK1_EVT_NOTE_HZ_EX);
    bool ex_note = (event_type == TRK1_EVT_NOTE_EX || event_type == TRK1_EVT_NOTE_HZ_EX);
    *out_freq_hz = 0u;
    *out_gate_ticks = 0u;
    *out_note_flags = 0u;
    *out_note = 0u;

    if (hz_note) {
        if (s->cursor + 2 > s->stream_end) {
            return false;
        }
        *out_freq_hz = (uint16_t)(s->cursor[0] | (s->cursor[1] << 8));
        s->cursor += 2;
    } else {
        if (s->cursor >= s->stream_end) {
            return false;
        }
        *out_note = *s->cursor++;
        *out_freq_hz = midi_note_to_freq(*out_note);
    }

    if (!trk1_decode_varuint(&s->cursor, s->stream_end, out_gate_ticks)) {
        return false;
    }

    if (ex_note) {
        if (s->cursor >= s->stream_end) {
            return false;
        }
        *out_note_flags = *s->cursor++;
    }

    return true;
}

static void stream_log_note_event(const SoundStream *s,
                                  uint8_t event_type,
                                  uint8_t channel,
                                  int voice_index,
                                  uint8_t note,
                                  uint16_t freq_hz,
                                  uint32_t gate_ticks,
                                  uint8_t note_flags,
                                  uint8_t volume) {
    if (!sound_interp_debug_enabled()) {
        return;
    }
    fprintf(stderr,
            "[sound-interp] %s tick=%" PRIu32 " ch=%u->v=%d note=%u freq=%u gate=%u flags=%u vol=%u\n",
            (event_type == TRK1_EVT_NOTE_HZ_EX) ? "NOTE_HZ_EX" :
            (event_type == TRK1_EVT_NOTE_EX) ? "NOTE_EX" :
            (event_type == TRK1_EVT_NOTE_HZ) ? "NOTE_HZ" : "NOTE",
            s->current_tick,
            channel,
            voice_index,
            note,
            (unsigned int)freq_hz,
            (unsigned int)gate_ticks,
            (unsigned int)note_flags,
            (unsigned int)volume);
}

static bool stream_parse_event(SoundStream *s, SoundVoice *voices, int voice_count) {
    if (!s->active || s->cursor >= s->stream_end) return false;

    uint8_t control = *s->cursor++;
    bool has_delay = (control >> 7) & 1;
    uint8_t event_type = (control >> 4) & 0x07;
    uint8_t channel = control & 0x0F;
    uint32_t delay_ticks = 0u;
    bool note_event = false;
    uint16_t note_freq_hz = 0u;
    uint32_t note_gate_ticks = 0u;
    uint8_t note_flags = 0u;
    uint8_t note = 0u;
    int note_voice_index = -1;

    switch (event_type) {
    case TRK1_EVT_NOTE:
    case TRK1_EVT_NOTE_HZ:
    case TRK1_EVT_NOTE_EX:
    case TRK1_EVT_NOTE_HZ_EX: {
        if (!stream_decode_note_event(s,
                                      event_type,
                                      &note_freq_hz,
                                      &note_gate_ticks,
                                      &note_flags,
                                      &note)) {
            s->active = false;
            return false;
        }
        note_event = true;
        if (voice_count > 0) {
            note_voice_index = channel % voice_count;
        }
        break;
    }
    case TRK1_EVT_SET_VOL: {
        if (s->cursor >= s->stream_end) { s->active = false; return false; }
        uint8_t vol = *s->cursor++;
        if (voice_count > 0) {
            int vi = channel % voice_count;
            voices[vi].volume = vol;
        }
        break;
    }
    case TRK1_EVT_SET_ENV: {
        if (s->cursor >= s->stream_end) { s->active = false; return false; }
        uint8_t point_count = *s->cursor++;
        if (point_count == 0u || point_count > TRK1_MAX_ENVELOPE_POINTS ||
            (s->cursor + point_count) > s->stream_end) {
            s->active = false;
            return false;
        }
        if (!sound_envelope_apply_points(&s->envelope, s->cursor, point_count)) {
            s->active = false;
            return false;
        }
        s->cursor += point_count;
        break;
    }
    case TRK1_EVT_END: {
        if (sound_interp_debug_enabled()) {
            fprintf(stderr,
                    "[sound-interp] END tick=%" PRIu32 " repeat_remaining=%d\n",
                    s->current_tick,
                    (int)s->repeat_remaining);
        }
        /* Handle repeat */
        if (s->repeat_remaining == 0) {
            /* Infinite loop: rewind */
            s->cursor = s->stream_start;
            s->current_tick = 0;
            s->next_event_tick = 0;
        } else if (s->repeat_remaining > 1) {
            s->repeat_remaining--;
            s->cursor = s->stream_start;
            s->current_tick = 0;
            s->next_event_tick = 0;
        } else {
            /* Last play done */
            s->active = false;
            return false; /* signal stream ended */
        }
        break;
    }
    default:
        /* Unknown event type: skip (best effort) */
        s->active = false;
        return false;
    }

    /* Parse delay if present */
    if (has_delay && s->active) {
        if (!trk1_decode_varuint(&s->cursor, s->stream_end, &delay_ticks)) {
            s->active = false;
            return false;
        }
        s->next_event_tick = s->current_tick + delay_ticks;
    }

    if (note_event && note_voice_index >= 0) {
        uint32_t effective_gate_ticks =
            sound_note_effective_gate_ticks(note_gate_ticks,
                                            delay_ticks,
                                            s->inter_note_gap_ticks,
                                            note_freq_hz,
                                            note_flags);
        sound_voice_apply_note(&voices[note_voice_index],
                               &s->envelope,
                               note_freq_hz,
                               effective_gate_ticks,
                               s->track_volume,
                               note_flags);
        stream_log_note_event(s,
                              event_type,
                              channel,
                              note_voice_index,
                              note,
                              note_freq_hz,
                              effective_gate_ticks,
                              note_flags,
                              voices[note_voice_index].volume);
    }

    return true;
}

/* ========================================================================= */
/* Finished notification (tick-safe queue + ingress enqueue)                  */
/* ========================================================================= */

static void sound_queue_finished_notification(ID track_id) {
    if (!track_id) {
        return;
    }
    SoundFinishedNotification notification = {
        .track_id = track_id,
    };
    if (!lockfree_spsc_queue_push(&g_sound_engine.finished_queue.spsc, &notification)) {
        g_sound_engine.telemetry.finished_drop_count++;
    }
}

static void sound_engine_enqueue_finished_notifications(void) {
    SoundFinishedNotification notification = {0};
    while (lockfree_spsc_queue_pop(&g_sound_engine.finished_queue.spsc, &notification)) {
        ID fn = RETAIN(g_sound_engine.on_finished_fn);
        if (!fn || !notification.track_id) {
            RELEASE(fn);
            continue;
        }

        SoundFinishedIngressCtx *ctx =
            (SoundFinishedIngressCtx *)CLJ_MALLOC(sizeof(SoundFinishedIngressCtx));
        if (!ctx) {
            g_sound_engine.telemetry.finished_drop_count++;
            RELEASE(fn);
            continue;
        }
        ctx->callback_fn = fn;
        ctx->track_id = notification.track_id;
        ctx->epoch = g_sound_engine_callback_epoch;

        if (!event_loop_enqueue_ingress_native(sound_finished_ingress_run,
                                               ctx,
                                               sound_finished_ingress_cleanup)) {
            g_sound_engine.telemetry.finished_drop_count++;
            sound_finished_ingress_cleanup(ctx);
        }
    }
}

static void notify_finished(ID track_id) {
    if (!g_sound_engine.on_finished_fn) {
        return;
    }
    sound_queue_finished_notification(track_id);
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void sound_engine_init(int voice_count) {
    memset(&g_sound_engine, 0, sizeof(g_sound_engine));
    g_sound_engine_callback_epoch = ++g_sound_engine_callback_epoch_counter;
    sound_ensure_finished_event_keywords();
    if (voice_count < 1) voice_count = 1;
    if (voice_count > SOUND_MAX_VOICES) voice_count = SOUND_MAX_VOICES;
    g_sound_engine.music_volume = 255;
    memset(g_sound_engine.cmd_queue.slots, 0, sizeof(g_sound_engine.cmd_queue.slots));
    {
        bool ok = lockfree_spsc_queue_init(&g_sound_engine.cmd_queue.spsc,
                                           g_sound_engine.cmd_queue.slots,
                                           SOUND_CMD_QUEUE_CAP,
                                           sizeof(g_sound_engine.cmd_queue.slots[0]));
        CLJ_ASSERT(ok);
        if (!ok) return;
    }
    memset(g_sound_engine.finished_queue.slots, 0, sizeof(g_sound_engine.finished_queue.slots));
    {
        bool ok = lockfree_spsc_queue_init(&g_sound_engine.finished_queue.spsc,
                                           g_sound_engine.finished_queue.slots,
                                           SOUND_FINISHED_QUEUE_CAP,
                                           sizeof(g_sound_engine.finished_queue.slots[0]));
        CLJ_ASSERT(ok);
        if (!ok) return;
    }
    sound_backend_init(voice_count);
    g_sound_engine.voice_count = voice_count;
}

void sound_engine_shutdown(void) {
    g_sound_engine_callback_epoch = ++g_sound_engine_callback_epoch_counter;

    /* Stop tick */
    if (g_sound_engine.tick_running) {
        sound_tick_stop();
    }

    stream_release(&g_sound_engine.music_stream);
    for (int i = 0; i < SOUND_MAX_SFX; i++) {
        stream_release(&g_sound_engine.sfx[i].stream);
        g_sound_engine.sfx[i].voice_index = -1;
    }

    /* Release on-finished callback */
    RELEASE(g_sound_engine.on_finished_fn);

    sound_backend_shutdown();
    memset(&g_sound_engine, 0, sizeof(g_sound_engine));
}

bool sound_engine_play_music(ID track_id, ID byte_array_obj, int32_t repeat) {
    if (!track_id || !byte_array_obj || TAG(byte_array_obj) != CLJ_BYTE_ARRAY) return false;

    CljByteArray *ba = as_byte_array(byte_array_obj);
    Trk1Header header;
    if (!trk1_parse_header(ba->data, ba->length, &header)) return false;

    SoundCmd cmd = {
        .type = SOUND_CMD_PLAY_TRACK,
        .track_id = track_id,
        .retained_obj = RETAIN(byte_array_obj),
        .header = header,
        .int_param = repeat
    };
    bool ok = lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd);
    if (!ok) {
        g_sound_engine.telemetry.cmd_drop_count++;
        RELEASE(cmd.retained_obj);
    }
    if (ok) {
        sound_tick_kick();
    }
    return ok;
}

bool sound_engine_stop_track(ID track_id) {
    SoundCmd cmd = { .type = SOUND_CMD_STOP_TRACK, .track_id = track_id };
    bool ok = lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd);
    if (!ok) g_sound_engine.telemetry.cmd_drop_count++;
    if (ok) {
        sound_tick_kick();
    }
    return ok;
}

void sound_engine_stop_music(void) {
    SoundCmd cmd = { .type = SOUND_CMD_STOP_MUSIC };
    if (!lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd)) {
        g_sound_engine.telemetry.cmd_drop_count++;
    } else {
        sound_tick_kick();
    }
}

bool sound_engine_play_sfx(ID sfx_id, ID byte_array_obj) {
    if (!sfx_id || !byte_array_obj || TAG(byte_array_obj) != CLJ_BYTE_ARRAY) return false;

    CljByteArray *ba = as_byte_array(byte_array_obj);
    Trk1Header header;
    if (!trk1_parse_header(ba->data, ba->length, &header)) return false;

    SoundCmd cmd = {
        .type = SOUND_CMD_PLAY_SFX,
        .track_id = sfx_id,
        .retained_obj = RETAIN(byte_array_obj),
        .header = header
    };
    bool ok = lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd);
    if (!ok) {
        g_sound_engine.telemetry.cmd_drop_count++;
        g_sound_engine.telemetry.sfx_drop_count++;
        RELEASE(cmd.retained_obj);
    }
    if (ok) {
        sound_tick_kick();
    }
    return ok;
}

void sound_engine_stop_all(void) {
    SoundCmd cmd = { .type = SOUND_CMD_STOP_ALL };
    if (!lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd)) {
        g_sound_engine.telemetry.cmd_drop_count++;
    } else {
        sound_tick_kick();
    }
}

bool sound_engine_set_track_volume(ID track_id, int32_t vol) {
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    SoundCmd cmd = { .type = SOUND_CMD_SET_TRACK_VOL, .track_id = track_id, .int_param = vol };
    bool ok = lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd);
    if (!ok) g_sound_engine.telemetry.cmd_drop_count++;
    if (ok) {
        sound_tick_kick();
    }
    return ok;
}

void sound_engine_set_music_volume(int32_t vol) {
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    SoundCmd cmd = { .type = SOUND_CMD_SET_MUSIC_VOL, .int_param = vol };
    if (!lockfree_spsc_queue_push(&g_sound_engine.cmd_queue.spsc, &cmd)) {
        g_sound_engine.telemetry.cmd_drop_count++;
    } else {
        sound_tick_kick();
    }
}

void sound_engine_on_finished(ID callback_fn) {
    ID old = g_sound_engine.on_finished_fn;
    g_sound_engine.on_finished_fn = RETAIN(callback_fn);
    RELEASE(old);
}

/* ========================================================================= */
/* Tick implementation                                                       */
/* ========================================================================= */

bool sound_engine_tick_mark_running(void) {
    bool was_running = g_sound_engine.tick_running;
    g_sound_engine.tick_running = true;
    return !was_running;
}

void sound_engine_tick_mark_stopped(void) {
    g_sound_engine.tick_running = false;
}

bool sound_engine_tick_is_running(void) {
    return g_sound_engine.tick_running;
}

static void tick_drain_commands(void) {
    SoundCmd cmd;
    int drained = 0;
    while (drained < SOUND_CMD_QUEUE_CAP && lockfree_spsc_queue_pop(&g_sound_engine.cmd_queue.spsc, &cmd)) {
        drained++;
        switch (cmd.type) {
        case SOUND_CMD_PLAY_TRACK: {
            stream_init(&g_sound_engine.music_stream, cmd.track_id, cmd.retained_obj, &cmd.header, cmd.int_param);
            cmd.retained_obj = NULL;
            /* Apply default_loop from header if repeat not specified */
            if (cmd.int_param == 0 && (cmd.header.flags & TRK1_FLAG_DEFAULT_LOOP)) {
                g_sound_engine.music_stream.repeat_remaining = 0; /* infinite */
            }
            break;
        }
        case SOUND_CMD_STOP_TRACK:
            if (g_sound_engine.music_stream.active &&
                g_sound_engine.music_stream.track_id == cmd.track_id) {
                g_sound_engine.music_stream.active = false;
                sound_engine_release_all_voice_holds();
                stream_release(&g_sound_engine.music_stream);
                /* No finished notification for manual stop */
            }
            /* Also stop matching SFX */
            for (int i = 0; i < SOUND_MAX_SFX; i++) {
                if (g_sound_engine.sfx[i].stream.active &&
                    g_sound_engine.sfx[i].stream.track_id == cmd.track_id) {
                    g_sound_engine.sfx[i].stream.active = false;
                    if (g_sound_engine.sfx[i].voice_index >= 0) {
                        sound_voice_force_silence(&g_sound_engine.voices[g_sound_engine.sfx[i].voice_index]);
                    }
                    g_sound_engine.sfx[i].voice_index = -1;
                    stream_release(&g_sound_engine.sfx[i].stream);
                }
            }
            break;

        case SOUND_CMD_STOP_MUSIC:
            g_sound_engine.music_stream.active = false;
            sound_engine_release_all_voice_holds();
            stream_release(&g_sound_engine.music_stream);
            break;

        case SOUND_CMD_PLAY_SFX: {
            /* Find free SFX slot; if none, evict the oldest (lowest current_tick). */
            int slot = -1;
            for (int i = 0; i < SOUND_MAX_SFX; i++) {
                if (!g_sound_engine.sfx[i].stream.active) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                uint32_t oldest_tick = 0;
                for (int i = 0; i < SOUND_MAX_SFX; i++) {
                    if (slot < 0 || g_sound_engine.sfx[i].stream.current_tick > oldest_tick) {
                        oldest_tick = g_sound_engine.sfx[i].stream.current_tick;
                        slot = i;
                    }
                }
                g_sound_engine.sfx[slot].stream.active = false;
                if (g_sound_engine.sfx[slot].voice_index >= 0) {
                    sound_voice_force_silence(&g_sound_engine.voices[g_sound_engine.sfx[slot].voice_index]);
                    g_sound_engine.sfx[slot].voice_index = -1;
                }
                stream_release(&g_sound_engine.sfx[slot].stream);
            }
            /* Find free voice */
            int vi = -1;
            for (int i = 0; i < g_sound_engine.voice_count; i++) {
                if (!g_sound_engine.voices[i].active) {
                    vi = i;
                    break;
                }
            }
            if (vi < 0) {
                /* Steal: take the voice with the least remaining gate */
                uint32_t min_gate = UINT32_MAX;
                for (int i = 0; i < g_sound_engine.voice_count; i++) {
                    if (g_sound_engine.voices[i].gate_remaining_ticks < min_gate) {
                        min_gate = g_sound_engine.voices[i].gate_remaining_ticks;
                        vi = i;
                    }
                }
            }
            if (vi < 0) break;
            /* Ensure one voice is owned by at most one active SFX stream. */
            for (int i = 0; i < SOUND_MAX_SFX; i++) {
                if (i == slot) {
                    continue;
                }
                if (g_sound_engine.sfx[i].stream.active &&
                    g_sound_engine.sfx[i].voice_index == vi) {
                    g_sound_engine.sfx[i].stream.active = false;
                    g_sound_engine.sfx[i].voice_index = -1;
                    stream_release(&g_sound_engine.sfx[i].stream);
                }
            }
            sound_voice_force_silence(&g_sound_engine.voices[vi]);
            stream_init(&g_sound_engine.sfx[slot].stream, cmd.track_id, cmd.retained_obj, &cmd.header, 1); /* one-shot */
            cmd.retained_obj = NULL;
            g_sound_engine.sfx[slot].voice_index = vi;
            break;
        }

        case SOUND_CMD_SET_TRACK_VOL:
            if (g_sound_engine.music_stream.active &&
                g_sound_engine.music_stream.track_id == cmd.track_id) {
                g_sound_engine.music_stream.track_volume = (uint8_t)cmd.int_param;
            }
            for (int i = 0; i < SOUND_MAX_SFX; i++) {
                if (g_sound_engine.sfx[i].stream.active &&
                    g_sound_engine.sfx[i].stream.track_id == cmd.track_id) {
                    g_sound_engine.sfx[i].stream.track_volume = (uint8_t)cmd.int_param;
                }
            }
            break;

        case SOUND_CMD_SET_MUSIC_VOL:
            g_sound_engine.music_volume = (uint8_t)cmd.int_param;
            break;

        case SOUND_CMD_STOP_ALL:
            g_sound_engine.music_stream.active = false;
            stream_release(&g_sound_engine.music_stream);
            for (int i = 0; i < SOUND_MAX_SFX; i++) {
                g_sound_engine.sfx[i].stream.active = false;
                g_sound_engine.sfx[i].voice_index = -1;
                stream_release(&g_sound_engine.sfx[i].stream);
            }
            for (int i = 0; i < g_sound_engine.voice_count; i++) {
                g_sound_engine.voices[i].active = false;
                g_sound_engine.voices[i].freq_hz = 0;
                g_sound_engine.voices[i].gate_remaining_ticks = 0;
                g_sound_engine.voices[i].hold_until_next_note = false;
                sound_backend_set_voice(i, 0, 0, false);
                g_sound_engine.voices[i].applied_freq_hz = 0;
                g_sound_engine.voices[i].applied_volume = 0;
                g_sound_engine.voices[i].applied_attack_generation =
                    g_sound_engine.voices[i].attack_generation;
            }
            break;
        }
        RELEASE(cmd.retained_obj);
    }

    if (!lockfree_spsc_queue_empty(&g_sound_engine.finished_queue.spsc)) {
        sound_tick_kick();
    }

    if (!lockfree_spsc_queue_empty(&g_sound_engine.cmd_queue.spsc)) {
        g_sound_engine.telemetry.tick_overrun_count++;
    }

    /* Update watermark */
    uint32_t pending = lockfree_spsc_queue_count(&g_sound_engine.cmd_queue.spsc);
    if (pending > g_sound_engine.telemetry.queue_high_watermark) {
        g_sound_engine.telemetry.queue_high_watermark = pending;
    }
}

/* Maximum events per tick to prevent infinite loops (e.g. NOTE+END with no delay in infinite loop) */
#define MAX_EVENTS_PER_TICK 64

static inline void tick_apply_voice_output(int voice_index, SoundVoice *voice) {
    uint16_t target_freq = 0;
    uint8_t target_vol = 0;
    bool retrigger = false;
    if (voice->active &&
        (voice->gate_remaining_ticks > 0 || voice->hold_until_next_note) &&
        voice->freq_hz > 0) {
        target_freq = voice->freq_hz;
        target_vol = (uint8_t)(((uint16_t)voice->volume * g_sound_engine.music_volume) >> 8);
    }
    retrigger = voice->attack_generation != voice->applied_attack_generation;
    if (!retrigger && voice->applied_freq_hz == target_freq && voice->applied_volume == target_vol) {
        return;
    }
    sound_backend_set_voice(voice_index, target_freq, target_vol, retrigger);
    tinyclj_sound_backend_observe_set_voice(voice_index, target_freq, target_vol, retrigger);
    voice->applied_freq_hz = target_freq;
    voice->applied_volume = target_vol;
    voice->applied_attack_generation = voice->attack_generation;
}

static uint32_t tick_voice_ticks_until_envelope_boundary(const SoundVoice *voice) {
    if (!voice || !voice->active || !voice->envelope_enabled) {
        return UINT32_MAX;
    }
    if (voice->envelope_stage_index + 1u >= voice->envelope_stage_count) {
        return UINT32_MAX;
    }
    return voice->envelope_stage_remaining_ticks > 0u ? voice->envelope_stage_remaining_ticks : 0u;
}

static void tick_voice_advance_envelope(SoundVoice *voice) {
    if (!voice || !voice->envelope_enabled) {
        return;
    }
    while (voice->envelope_stage_index + 1u < voice->envelope_stage_count &&
           voice->envelope_stage_remaining_ticks == 0u) {
        voice->envelope_stage_index++;
        voice->envelope_stage_remaining_ticks = voice->envelope_stage_ticks[voice->envelope_stage_index];
        voice->volume =
            sound_scale_volume_u8(voice->base_volume,
                                  voice->envelope_stage_levels[voice->envelope_stage_index]);
    }
}

static void tick_release_sfx_voice(SoundSfxInstance *sfx) {
    if (!sfx) {
        return;
    }
    int vi = sfx->voice_index;
    if (vi >= 0 && vi < g_sound_engine.voice_count) {
        sound_voice_force_silence(&g_sound_engine.voices[vi]);
    }
    sfx->voice_index = -1;
}

static void tick_process_due_stream(SoundStream *s) {
    if (!s || !s->active) {
        return;
    }

    int events_this_tick = 0;
    while (s->active && s->current_tick >= s->next_event_tick && events_this_tick < MAX_EVENTS_PER_TICK) {
        events_this_tick++;
        bool had_more = stream_parse_event(s, g_sound_engine.voices, g_sound_engine.voice_count);
        if (!had_more && !s->active) {
            notify_finished(s->track_id);
            stream_release(s);
            break;
        }
    }
    if (s->active && s->current_tick >= s->next_event_tick && events_this_tick >= MAX_EVENTS_PER_TICK) {
        g_sound_engine.telemetry.tick_overrun_count++;
    }
}

static void tick_process_due_sfx(void) {
    for (int i = 0; i < SOUND_MAX_SFX; i++) {
        SoundSfxInstance *sfx = &g_sound_engine.sfx[i];
        if (!sfx->stream.active) {
            continue;
        }

        int vi = sfx->voice_index;
        if (vi < 0) {
            continue;
        }

        int events_this_tick = 0;
        while (sfx->stream.active && sfx->stream.current_tick >= sfx->stream.next_event_tick &&
               events_this_tick < MAX_EVENTS_PER_TICK) {
            events_this_tick++;
            bool had_more = stream_parse_event(&sfx->stream, &g_sound_engine.voices[vi], 1);
            if (!had_more && !sfx->stream.active) {
                break;
            }
        }
        if (sfx->stream.active && sfx->stream.current_tick >= sfx->stream.next_event_tick &&
            events_this_tick >= MAX_EVENTS_PER_TICK) {
            g_sound_engine.telemetry.tick_overrun_count++;
        }

        if (!sfx->stream.active) {
            tick_release_sfx_voice(sfx);
            stream_release(&sfx->stream);
        }
    }
}

static uint32_t tick_ticks_until_stream_deadline(const SoundStream *s) {
    if (!s || !s->active) {
        return UINT32_MAX;
    }
    if (s->current_tick >= s->next_event_tick) {
        return 0u;
    }
    return s->next_event_tick - s->current_tick;
}

static uint32_t tick_ticks_until_next_boundary(void) {
    uint32_t best = tick_ticks_until_stream_deadline(&g_sound_engine.music_stream);
    for (int i = 0; i < SOUND_MAX_SFX; i++) {
        uint32_t sfx_ticks = tick_ticks_until_stream_deadline(&g_sound_engine.sfx[i].stream);
        if (sfx_ticks < best) {
            best = sfx_ticks;
        }
    }
    for (int i = 0; i < g_sound_engine.voice_count; i++) {
        SoundVoice *v = &g_sound_engine.voices[i];
        if (v->active && v->gate_remaining_ticks > 0 && v->gate_remaining_ticks < best) {
            best = v->gate_remaining_ticks;
        }
        uint32_t env_ticks = tick_voice_ticks_until_envelope_boundary(v);
        if (env_ticks < best) {
            best = env_ticks;
        }
    }
    return best;
}

static void tick_fast_forward(uint32_t ticks) {
    if (ticks == 0u) {
        return;
    }

    if (g_sound_engine.music_stream.active) {
        g_sound_engine.music_stream.current_tick += ticks;
    }
    for (int i = 0; i < SOUND_MAX_SFX; i++) {
        if (g_sound_engine.sfx[i].stream.active) {
            g_sound_engine.sfx[i].stream.current_tick += ticks;
        }
    }
    for (int i = 0; i < g_sound_engine.voice_count; i++) {
        SoundVoice *v = &g_sound_engine.voices[i];
        if (v->active && v->gate_remaining_ticks > 0) {
            if (v->gate_remaining_ticks <= ticks) {
                v->gate_remaining_ticks = 0;
                if (v->hold_until_next_note && v->freq_hz > 0u) {
                    if (sound_interp_debug_enabled()) {
                        fprintf(stderr,
                                "[sound-interp] LEGATO_HOLD voice=%d tick_done gate=0\n",
                                i);
                    }
                } else {
                    v->freq_hz = 0;
                    if (sound_interp_debug_enabled()) {
                        fprintf(stderr,
                                "[sound-interp] NOTE_OFF voice=%d tick_done gate=0\n",
                                i);
                    }
                }
            } else {
                v->gate_remaining_ticks -= ticks;
            }
        }
        if (v->active && v->envelope_enabled && v->envelope_stage_remaining_ticks > 0u) {
            if (v->envelope_stage_remaining_ticks <= ticks) {
                v->envelope_stage_remaining_ticks = 0u;
            } else {
                v->envelope_stage_remaining_ticks -= ticks;
            }
            tick_voice_advance_envelope(v);
        }
        tick_apply_voice_output(i, v);
    }
}

static void tick_apply_all_voice_outputs(void) {
    for (int i = 0; i < g_sound_engine.voice_count; i++) {
        tick_apply_voice_output(i, &g_sound_engine.voices[i]);
    }
}

static bool tick_has_active_audio(void) {
    if (g_sound_engine.music_stream.active) return true;
    for (int i = 0; i < SOUND_MAX_SFX; i++) {
        if (g_sound_engine.sfx[i].stream.active) return true;
    }
    for (int i = 0; i < g_sound_engine.voice_count; i++) {
        if (g_sound_engine.voices[i].active &&
            (g_sound_engine.voices[i].gate_remaining_ticks > 0 ||
             g_sound_engine.voices[i].hold_until_next_note) &&
            g_sound_engine.voices[i].freq_hz > 0) {
            return true;
        }
    }
    return false;
}

static void tick_maybe_stop_when_idle(void) {
    if (!tick_has_active_audio() &&
        lockfree_spsc_queue_empty(&g_sound_engine.cmd_queue.spsc) &&
        !sound_backend_keepalive_active()) {
        sound_tick_sleep();
    }
}

uint32_t sound_engine_ticks_until_deadline(void) {
    if (!lockfree_spsc_queue_empty(&g_sound_engine.cmd_queue.spsc)) {
        return 0u;
    }
    uint32_t ticks = tick_ticks_until_next_boundary();
    return ticks == UINT32_MAX ? 0u : ticks;
}

void sound_engine_advance_ticks(uint32_t ticks) {
    uint32_t remaining = ticks;

    while (true) {
        tick_drain_commands();
        tick_process_due_stream(&g_sound_engine.music_stream);
        tick_process_due_sfx();

        if (remaining == 0u) {
            tick_apply_all_voice_outputs();
            break;
        }

        uint32_t step = tick_ticks_until_next_boundary();
        if (step == UINT32_MAX) {
            tick_fast_forward(remaining);
            remaining = 0u;
            continue;
        }
        if (step == 0u) {
            step = 1u;
        }
        if (step > remaining) {
            step = remaining;
        }
        tick_fast_forward(step);
        remaining -= step;
    }

    sound_engine_enqueue_finished_notifications();
    tick_maybe_stop_when_idle();
}

void sound_engine_tick(void) {
    tick_drain_commands();
    tick_process_due_stream(&g_sound_engine.music_stream);
    tick_process_due_sfx();
    tick_fast_forward(1u);
    sound_engine_enqueue_finished_notifications();
    tick_maybe_stop_when_idle();
}
