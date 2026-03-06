/*
 * Audio Engine implementation (trk1 streaming, SPSC queue, track registry).
 *
 * Platform-agnostic: ESP32 LEDC and host stubs are in separate files.
 */

#include "audio_engine.h"
#include "byte_array.h"
#include "memory.h"
#include "event_loop.h"
#include "symbol.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global engine instance */
AudioEngine g_audio_engine;
static ID KW_SOURCE = NULL;
static ID KW_KIND = NULL;
static ID KW_TRACK_ID = NULL;
static ID KW_AUDIO = NULL;
static ID KW_FINISHED = NULL;

static bool audio_interp_debug_enabled(void) {
    static int init = 0;
    static bool enabled = false;
    if (!init) {
        const char *env = getenv("TINYCLJ_AUDIO_INTERP_DEBUG");
        enabled = (env && env[0] != '0');
        init = 1;
    }
    return enabled;
}

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

/* ========================================================================= */
/* Track registry helpers                                                    */
/* ========================================================================= */

static AudioTrackEntry *find_track(ID track_id) {
    for (int i = 0; i < g_audio_engine.track_count; i++) {
        if (g_audio_engine.tracks[i].track_id == track_id)
            return &g_audio_engine.tracks[i];
    }
    return NULL;
}

static void audio_ensure_finished_event_keywords(void) {
    if (KW_SOURCE) return;
    KW_SOURCE = intern_symbol_global(":source");
    KW_KIND = intern_symbol_global(":kind");
    KW_TRACK_ID = intern_symbol_global(":track-id");
    KW_AUDIO = intern_symbol_global(":audio");
    KW_FINISHED = intern_symbol_global(":finished");
}

static ID audio_make_finished_event(ID track_id) {
    audio_ensure_finished_event_keywords();
    return make_map_from_kv(3,
                            KW_SOURCE, KW_AUDIO,
                            KW_KIND, KW_FINISHED,
                            KW_TRACK_ID, track_id);
}

/* ========================================================================= */
/* Stream helpers                                                            */
/* ========================================================================= */

static void stream_init(AudioStream *s, AudioTrackEntry *t, int32_t repeat) {
    s->cursor = t->header.stream_start;
    s->stream_end = t->header.stream_start + t->header.stream_len_bytes;
    s->stream_start = t->header.stream_start;
    s->current_tick = 0;
    s->next_event_tick = 0;
    s->repeat_remaining = repeat;
    s->track_volume = 255;
    s->active = true;
    s->track_id = t->track_id;
    s->channel_count = t->header.channel_count;
}

static bool stream_parse_event(AudioStream *s, AudioVoice *voices, int voice_count) {
    if (!s->active || s->cursor >= s->stream_end) return false;

    uint8_t control = *s->cursor++;
    bool has_delay = (control >> 7) & 1;
    uint8_t event_type = (control >> 4) & 0x07;
    uint8_t channel = control & 0x0F;

    switch (event_type) {
    case TRK1_EVT_NOTE: {
        if (s->cursor >= s->stream_end) { s->active = false; return false; }
        uint8_t note = *s->cursor++;
        uint32_t gate_ticks = 0;
        if (!trk1_decode_varuint(&s->cursor, s->stream_end, &gate_ticks)) {
            s->active = false;
            return false;
        }
        /* Map channel to voice (modulo available voices) */
        if (voice_count > 0) {
            int vi = channel % voice_count;
            voices[vi].freq_hz = midi_note_to_freq(note);
            voices[vi].gate_remaining_ticks = gate_ticks;
            voices[vi].volume = s->track_volume;
            voices[vi].active = true;
            if (audio_interp_debug_enabled()) {
                fprintf(stderr,
                        "[audio-interp] NOTE tick=%" PRIu32 " ch=%u->v=%d note=%u freq=%u gate=%u vol=%u\n",
                        s->current_tick,
                        channel,
                        vi,
                        note,
                        (unsigned int)voices[vi].freq_hz,
                        (unsigned int)gate_ticks,
                        (unsigned int)voices[vi].volume);
            }
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
    case TRK1_EVT_END: {
        if (audio_interp_debug_enabled()) {
            fprintf(stderr,
                    "[audio-interp] END tick=%" PRIu32 " repeat_remaining=%d\n",
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
        uint32_t delta = 0;
        if (!trk1_decode_varuint(&s->cursor, s->stream_end, &delta)) {
            s->active = false;
            return false;
        }
        s->next_event_tick = s->current_tick + delta;
    }

    return true;
}

/* ========================================================================= */
/* Finished notification (enqueue scheduler task)                            */
/* ========================================================================= */

static void notify_finished(ID track_id) {
    ID fn = RETAIN(g_audio_engine.on_finished_fn);
    if (!fn) return;

    ID event_payload = audio_make_finished_event(track_id);
    if (!event_payload) {
        g_audio_engine.telemetry.finished_drop_count++;
        RELEASE(fn);
        return;
    }

    if (!event_loop_enqueue_ingress_call(fn, event_payload)) {
        g_audio_engine.telemetry.finished_drop_count++;
    }
    RELEASE(event_payload);
    RELEASE(fn);
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void audio_engine_init(int voice_count) {
    memset(&g_audio_engine, 0, sizeof(g_audio_engine));
    audio_ensure_finished_event_keywords();
    if (voice_count < 1) voice_count = 1;
    if (voice_count > AUDIO_MAX_VOICES) voice_count = AUDIO_MAX_VOICES;
    g_audio_engine.voice_count = voice_count;
    g_audio_engine.music_volume = 255;
    g_audio_engine.us_per_tick = 1000; /* 1ms default */
    memset(g_audio_engine.cmd_queue.slots, 0, sizeof(g_audio_engine.cmd_queue.slots));
    {
        bool ok = lockfree_spsc_queue_init(&g_audio_engine.cmd_queue.spsc,
                                           g_audio_engine.cmd_queue.slots,
                                           AUDIO_CMD_QUEUE_CAP,
                                           sizeof(g_audio_engine.cmd_queue.slots[0]));
        CLJ_ASSERT(ok);
        if (!ok) return;
    }
    audio_backend_init(voice_count);
}

void audio_engine_shutdown(void) {
    /* Stop tick */
    if (g_audio_engine.tick_running) {
        audio_tick_stop();
    }

    /* Release all loaded tracks */
    for (int i = 0; i < g_audio_engine.track_count; i++) {
        RELEASE(g_audio_engine.tracks[i].retained_obj);
    }

    /* Release on-finished callback */
    RELEASE(g_audio_engine.on_finished_fn);

    audio_backend_shutdown();
    memset(&g_audio_engine, 0, sizeof(g_audio_engine));
}

bool audio_engine_load_track(ID track_id, ID byte_array_obj) {
    if (!track_id || !byte_array_obj) return false;
    if (TAG(byte_array_obj) != CLJ_BYTE_ARRAY) return false;

    CljByteArray *ba = as_byte_array(byte_array_obj);
    const uint8_t *data = ba->data;
    int len = ba->length;

    Trk1Header header;
    if (!trk1_parse_header(data, len, &header)) return false;

    /* Check if already loaded (replace) */
    AudioTrackEntry *existing = find_track(track_id);
    if (existing) {
        RELEASE(existing->retained_obj);
        existing->retained_obj = RETAIN(byte_array_obj);
        existing->data_ptr = data;
        existing->len = len;
        existing->header = header;
        return true;
    }

    /* New entry */
    if (g_audio_engine.track_count >= AUDIO_MAX_TRACKS) return false;

    AudioTrackEntry *entry = &g_audio_engine.tracks[g_audio_engine.track_count++];
    entry->track_id = track_id;
    entry->retained_obj = RETAIN(byte_array_obj);
    entry->data_ptr = data;
    entry->len = len;
    entry->header = header;
    return true;
}

bool audio_engine_unload_track(ID track_id) {
    if (!track_id) return false;

    /* Stop if this track is currently playing */
    if (g_audio_engine.music_stream.active &&
        g_audio_engine.music_stream.track_id == track_id) {
        g_audio_engine.music_stream.active = false;
    }

    for (int i = 0; i < g_audio_engine.track_count; i++) {
        if (g_audio_engine.tracks[i].track_id == track_id) {
            RELEASE(g_audio_engine.tracks[i].retained_obj);
            /* Compact: move last entry into this slot */
            g_audio_engine.track_count--;
            if (i < g_audio_engine.track_count) {
                g_audio_engine.tracks[i] = g_audio_engine.tracks[g_audio_engine.track_count];
            }
            memset(&g_audio_engine.tracks[g_audio_engine.track_count], 0, sizeof(AudioTrackEntry));
            return true;
        }
    }
    return false;
}

bool audio_engine_play_music(ID track_id, int32_t repeat) {
    AudioCmd cmd = { .type = AUDIO_CMD_PLAY_TRACK, .track_id = track_id, .int_param = repeat };
    bool ok = lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd);
    if (!ok) g_audio_engine.telemetry.cmd_drop_count++;
    if (ok && !g_audio_engine.tick_running) {
        audio_tick_start();
    }
    return ok;
}

bool audio_engine_stop_track(ID track_id) {
    AudioCmd cmd = { .type = AUDIO_CMD_STOP_TRACK, .track_id = track_id };
    bool ok = lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd);
    if (!ok) g_audio_engine.telemetry.cmd_drop_count++;
    return ok;
}

void audio_engine_stop_music(void) {
    AudioCmd cmd = { .type = AUDIO_CMD_STOP_MUSIC };
    if (!lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd)) {
        g_audio_engine.telemetry.cmd_drop_count++;
    }
}

bool audio_engine_play_sfx(ID sfx_id) {
    if (!sfx_id) return false;
    if (!find_track(sfx_id)) return false;

    bool has_free_slot = false;
    for (int i = 0; i < AUDIO_MAX_SFX; i++) {
        if (!g_audio_engine.sfx[i].stream.active) {
            has_free_slot = true;
            break;
        }
    }
    if (!has_free_slot) {
        g_audio_engine.telemetry.sfx_drop_count++;
        return false;
    }

    AudioCmd cmd = { .type = AUDIO_CMD_PLAY_SFX, .track_id = sfx_id };
    bool ok = lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd);
    if (!ok) g_audio_engine.telemetry.cmd_drop_count++;
    if (ok && !g_audio_engine.tick_running) {
        audio_tick_start();
    }
    return ok;
}

void audio_engine_stop_all(void) {
    AudioCmd cmd = { .type = AUDIO_CMD_STOP_ALL };
    if (!lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd)) {
        g_audio_engine.telemetry.cmd_drop_count++;
    }
}

bool audio_engine_set_track_volume(ID track_id, int32_t vol) {
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    AudioCmd cmd = { .type = AUDIO_CMD_SET_TRACK_VOL, .track_id = track_id, .int_param = vol };
    bool ok = lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd);
    if (!ok) g_audio_engine.telemetry.cmd_drop_count++;
    return ok;
}

void audio_engine_set_music_volume(int32_t vol) {
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;
    AudioCmd cmd = { .type = AUDIO_CMD_SET_MUSIC_VOL, .int_param = vol };
    if (!lockfree_spsc_queue_push(&g_audio_engine.cmd_queue.spsc, &cmd)) {
        g_audio_engine.telemetry.cmd_drop_count++;
    }
}

void audio_engine_on_finished(ID callback_fn) {
    ID old = g_audio_engine.on_finished_fn;
    g_audio_engine.on_finished_fn = RETAIN(callback_fn);
    RELEASE(old);
}

/* ========================================================================= */
/* Tick implementation                                                       */
/* ========================================================================= */

static void tick_drain_commands(void) {
    AudioCmd cmd;
    int drained = 0;
    while (drained < AUDIO_CMD_QUEUE_CAP && lockfree_spsc_queue_pop(&g_audio_engine.cmd_queue.spsc, &cmd)) {
        drained++;
        switch (cmd.type) {
        case AUDIO_CMD_PLAY_TRACK: {
            AudioTrackEntry *t = find_track(cmd.track_id);
            if (t) {
                stream_init(&g_audio_engine.music_stream, t, cmd.int_param);
                /* Apply default_loop from header if repeat not specified */
                if (cmd.int_param == 0 && (t->header.flags & TRK1_FLAG_DEFAULT_LOOP)) {
                    g_audio_engine.music_stream.repeat_remaining = 0; /* infinite */
                }
            }
            break;
        }
        case AUDIO_CMD_STOP_TRACK:
            if (g_audio_engine.music_stream.active &&
                g_audio_engine.music_stream.track_id == cmd.track_id) {
                g_audio_engine.music_stream.active = false;
                /* No finished notification for manual stop */
            }
            /* Also stop matching SFX */
            for (int i = 0; i < AUDIO_MAX_SFX; i++) {
                if (g_audio_engine.sfx[i].stream.active &&
                    g_audio_engine.sfx[i].stream.track_id == cmd.track_id) {
                    g_audio_engine.sfx[i].stream.active = false;
                    if (g_audio_engine.sfx[i].voice_index >= 0) {
                        g_audio_engine.voices[g_audio_engine.sfx[i].voice_index].active = false;
                    }
                }
            }
            break;

        case AUDIO_CMD_STOP_MUSIC:
            g_audio_engine.music_stream.active = false;
            break;

        case AUDIO_CMD_PLAY_SFX: {
            AudioTrackEntry *t = find_track(cmd.track_id);
            if (!t) break;
            /* Find free SFX slot */
            int slot = -1;
            for (int i = 0; i < AUDIO_MAX_SFX; i++) {
                if (!g_audio_engine.sfx[i].stream.active) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                g_audio_engine.telemetry.sfx_drop_count++;
                break;
            }
            /* Find free voice */
            int vi = -1;
            for (int i = 0; i < g_audio_engine.voice_count; i++) {
                if (!g_audio_engine.voices[i].active) {
                    vi = i;
                    break;
                }
            }
            if (vi < 0) {
                /* Steal: take the voice with the least remaining gate */
                uint32_t min_gate = UINT32_MAX;
                for (int i = 0; i < g_audio_engine.voice_count; i++) {
                    if (g_audio_engine.voices[i].gate_remaining_ticks < min_gate) {
                        min_gate = g_audio_engine.voices[i].gate_remaining_ticks;
                        vi = i;
                    }
                }
            }
            if (vi < 0) break;
            stream_init(&g_audio_engine.sfx[slot].stream, t, 1); /* one-shot */
            g_audio_engine.sfx[slot].voice_index = vi;
            g_audio_engine.sfx[slot].priority = 128; /* default priority */
            break;
        }

        case AUDIO_CMD_SET_TRACK_VOL:
            if (g_audio_engine.music_stream.active &&
                g_audio_engine.music_stream.track_id == cmd.track_id) {
                g_audio_engine.music_stream.track_volume = (uint8_t)cmd.int_param;
            }
            break;

        case AUDIO_CMD_SET_MUSIC_VOL:
            g_audio_engine.music_volume = (uint8_t)cmd.int_param;
            break;

        case AUDIO_CMD_STOP_ALL:
            g_audio_engine.music_stream.active = false;
            for (int i = 0; i < AUDIO_MAX_SFX; i++) {
                g_audio_engine.sfx[i].stream.active = false;
            }
            for (int i = 0; i < g_audio_engine.voice_count; i++) {
                g_audio_engine.voices[i].active = false;
                audio_backend_set_voice(i, 0, 0);
            }
            break;
        }
    }

    if (!lockfree_spsc_queue_empty(&g_audio_engine.cmd_queue.spsc)) {
        g_audio_engine.telemetry.tick_overrun_count++;
    }

    /* Update watermark */
    uint32_t pending = lockfree_spsc_queue_count(&g_audio_engine.cmd_queue.spsc);
    if (pending > g_audio_engine.telemetry.queue_high_watermark) {
        g_audio_engine.telemetry.queue_high_watermark = pending;
    }
}

/* Maximum events per tick to prevent infinite loops (e.g. NOTE+END with no delay in infinite loop) */
#define MAX_EVENTS_PER_TICK 64

static void tick_advance_stream(AudioStream *s) {
    if (!s->active) return;

    int events_this_tick = 0;
    while (s->active && s->current_tick >= s->next_event_tick && events_this_tick < MAX_EVENTS_PER_TICK) {
        events_this_tick++;
        bool had_more = stream_parse_event(s, g_audio_engine.voices, g_audio_engine.voice_count);
        if (!had_more && !s->active) {
            /* Stream ended naturally */
            notify_finished(s->track_id);
            break;
        }
    }
    if (s->active && s->current_tick >= s->next_event_tick && events_this_tick >= MAX_EVENTS_PER_TICK) {
        g_audio_engine.telemetry.tick_overrun_count++;
    }
    s->current_tick++;
}

static void tick_advance_sfx(void) {
    for (int i = 0; i < AUDIO_MAX_SFX; i++) {
        AudioSfxInstance *sfx = &g_audio_engine.sfx[i];
        if (!sfx->stream.active) continue;

        int vi = sfx->voice_index;
        if (vi < 0) continue;

        int events_this_tick = 0;
        while (sfx->stream.active && sfx->stream.current_tick >= sfx->stream.next_event_tick
               && events_this_tick < MAX_EVENTS_PER_TICK) {
            events_this_tick++;
            AudioVoice temp_voice = g_audio_engine.voices[vi];
            bool had_more = stream_parse_event(&sfx->stream, &temp_voice, 1);
            g_audio_engine.voices[vi] = temp_voice;
            if (!had_more && !sfx->stream.active) {
                break;
            }
        }
        if (sfx->stream.active && sfx->stream.current_tick >= sfx->stream.next_event_tick
            && events_this_tick >= MAX_EVENTS_PER_TICK) {
            g_audio_engine.telemetry.tick_overrun_count++;
        }
        sfx->stream.current_tick++;

        if (!sfx->stream.active) {
            /* Release voice */
            g_audio_engine.voices[vi].active = false;
            g_audio_engine.voices[vi].freq_hz = 0;
            sfx->voice_index = -1;
        }
    }
}

static void tick_update_voices(void) {
    for (int i = 0; i < g_audio_engine.voice_count; i++) {
        AudioVoice *v = &g_audio_engine.voices[i];
        if (v->active && v->gate_remaining_ticks > 0) {
            v->gate_remaining_ticks--;
            if (v->gate_remaining_ticks == 0) {
                /* Note off */
                audio_backend_set_voice(i, 0, 0);
                v->freq_hz = 0;
                if (audio_interp_debug_enabled()) {
                    fprintf(stderr,
                            "[audio-interp] NOTE_OFF voice=%d tick_done gate=0\n",
                            i);
                }
            } else {
                /* Apply global music volume scaling */
                uint8_t effective_vol = (uint8_t)(((uint16_t)v->volume * g_audio_engine.music_volume) >> 8);
                audio_backend_set_voice(i, v->freq_hz, effective_vol);
            }
        } else if (!v->active) {
            audio_backend_set_voice(i, 0, 0);
        }
    }
}

static bool tick_has_active_audio(void) {
    if (g_audio_engine.music_stream.active) return true;
    for (int i = 0; i < AUDIO_MAX_SFX; i++) {
        if (g_audio_engine.sfx[i].stream.active) return true;
    }
    for (int i = 0; i < g_audio_engine.voice_count; i++) {
        if (g_audio_engine.voices[i].active &&
            g_audio_engine.voices[i].gate_remaining_ticks > 0 &&
            g_audio_engine.voices[i].freq_hz > 0) {
            return true;
        }
    }
    return false;
}

void audio_engine_tick(void) {
    tick_drain_commands();
    tick_advance_stream(&g_audio_engine.music_stream);
    tick_advance_sfx();
    tick_update_voices();

    /* On-demand tick lifecycle: stop if nothing active and queue empty */
    if (!tick_has_active_audio() && lockfree_spsc_queue_empty(&g_audio_engine.cmd_queue.spsc)) {
        if (g_audio_engine.tick_running) {
            audio_tick_stop();
            g_audio_engine.tick_running = false;
        }
    }
}
