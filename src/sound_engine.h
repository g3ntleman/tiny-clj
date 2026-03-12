/*
 * Sound Engine for ESP32 piezo output (trk1 / MUS-lite)
 *
 * Streams trk1 byte arrays directly (no full-decode). Supports N voices,
 * SPSC command queue (Clojure -> Tick), and finished notification via
 * generic event-loop ingress event maps (Tick -> Clojure).
 *
 * Host builds use stub backends; ESP32 builds drive LEDC PWM.
 */

#ifndef TINY_CLJ_SOUND_ENGINE_H
#define TINY_CLJ_SOUND_ENGINE_H

#include "object.h"
#include "lockfree_spsc_queue.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================= */
/* trk1 format constants                                                     */
/* ========================================================================= */

#define TRK1_MAGIC_0 'T'
#define TRK1_MAGIC_1 'R'
#define TRK1_MAGIC_2 'K'
#define TRK1_MAGIC_3 '1'
#define TRK1_VERSION 1
#define TRK1_HEADER_SIZE 20

#define TRK1_FLAG_DEFAULT_LOOP  0x01
#define TRK1_FLAG_RESERVED_BIT1 0x02

#define TRK1_EVT_NOTE    0
#define TRK1_EVT_SET_VOL 1
#define TRK1_EVT_END     2

#define TRK1_MAX_CHANNELS 16

/* Varuint max value: 2^28-1 */
#define TRK1_VARUINT_MAX 0x0FFFFFFFU

/* ========================================================================= */
/* trk1 header (parsed)                                                      */
/* ========================================================================= */

typedef struct {
    uint8_t  version;
    uint8_t  flags;
    uint8_t  channel_count;
    uint16_t tpq;
    uint16_t bpm;
    uint32_t stream_len_bytes;
    uint32_t crc32;
    const uint8_t *stream_start;
} Trk1Header;

/* Validate and parse a trk1 header from raw bytes.
 * Returns true on success, false on invalid data. */
bool trk1_parse_header(const uint8_t *data, int len, Trk1Header *out);

/* Decode a base-128 varuint starting at *cursor.
 * Advances *cursor past the varuint. Returns false on overflow or OOB. */
bool trk1_decode_varuint(const uint8_t **cursor, const uint8_t *end, uint32_t *out);

/* ========================================================================= */
/* SPSC command queue (lock-free, bounded)                                   */
/* ========================================================================= */

#define SOUND_CMD_QUEUE_CAP 8
#define SOUND_FINISHED_QUEUE_CAP 8

typedef enum {
    SOUND_CMD_PLAY_TRACK,
    SOUND_CMD_STOP_TRACK,
    SOUND_CMD_STOP_MUSIC,
    SOUND_CMD_PLAY_SFX,
    SOUND_CMD_SET_TRACK_VOL,
    SOUND_CMD_SET_MUSIC_VOL,
    SOUND_CMD_STOP_ALL,
} SoundCmdType;

typedef struct {
    SoundCmdType type;
    ID           track_id;       /* interned symbol/keyword (not retained in queue) */
    int32_t      int_param;      /* repeat count or volume */
} SoundCmd;

typedef struct {
    SoundCmd           slots[SOUND_CMD_QUEUE_CAP];
    LockFreeSpscQueue  spsc;
} SoundCmdQueue;

/* ========================================================================= */
/* Track registry                                                            */
/* ========================================================================= */

#define SOUND_MAX_TRACKS 8

typedef struct {
    ID       track_id;       /* interned symbol/keyword; pointer comparison */
    ID       retained_obj;   /* retained CljByteArray */
    const uint8_t *data_ptr;
    int      len;
    Trk1Header header;
} SoundTrackEntry;

/* ========================================================================= */
/* Stream cursor (active playback)                                           */
/* ========================================================================= */

typedef struct {
    const uint8_t *cursor;
    const uint8_t *stream_end;
    uint32_t current_tick;
    uint32_t next_event_tick;
    int32_t  repeat_remaining;  /* 0 = infinite, >0 = remaining plays */
    uint8_t  track_volume;      /* 0..255 */
    bool     active;
    ID       track_id;          /* for finished notification */
    const uint8_t *stream_start; /* for repeat/loop rewind */
    uint8_t  channel_count;
} SoundStream;

/* ========================================================================= */
/* Voice state                                                               */
/* ========================================================================= */

#define SOUND_MAX_VOICES 4

typedef struct {
    uint16_t freq_hz;
    uint16_t applied_freq_hz;
    uint32_t gate_remaining_ticks;
    uint8_t  volume;             /* per-voice volume 0..255 */
    uint8_t  applied_volume;
    bool     active;
} SoundVoice;

/* ========================================================================= */
/* SFX instance                                                              */
/* ========================================================================= */

#define SOUND_MAX_SFX 2

typedef struct {
    SoundStream stream;
    uint8_t     priority;
    int         voice_index;    /* assigned voice, -1 if none */
} SoundSfxInstance;

/* ========================================================================= */
/* Telemetry counters                                                        */
/* ========================================================================= */

typedef struct {
    uint32_t cmd_drop_count;
    uint32_t tick_overrun_count;
    uint32_t queue_high_watermark;
    uint32_t sfx_drop_count;
    uint32_t finished_drop_count;
} SoundTelemetry;

/* ========================================================================= */
/* Audio engine state                                                        */
/* ========================================================================= */

typedef struct {
    /* Track registry */
    SoundTrackEntry tracks[SOUND_MAX_TRACKS];
    int             track_count;

    /* Active music stream (one at a time for MVP) */
    SoundStream     music_stream;

    /* SFX instances */
    SoundSfxInstance sfx[SOUND_MAX_SFX];

    /* Voices (physical outputs) */
    SoundVoice      voices[SOUND_MAX_VOICES];
    int             voice_count;  /* configured output count */

    /* Global state */
    uint8_t         music_volume; /* master music volume 0..255 */

    /* Command queue */
    SoundCmdQueue   cmd_queue;

    /* Finished callback (Clojure function, retained) */
    ID              on_finished_fn;

    /* Tick lifecycle */
    bool            tick_running;

    /* Telemetry */
    SoundTelemetry  telemetry;

    /* Microseconds per tick (derived from BPM/TPQ or default 1000 = 1ms) */
    uint32_t        us_per_tick;
} SoundEngine;

typedef struct {
    bool backend_available;
    bool sound_running;
    bool tick_enabled;
    bool tick_thread_running;
    int voice_count;
    bool debug_noise_active;
    bool debug_ramp_active;
    bool debug_ramp_noise_active;
} SoundHostStatus;

/* Global engine instance */
extern SoundEngine g_sound_engine;

/* ========================================================================= */
/* Public API (called from Clojure native functions)                         */
/* ========================================================================= */

/* Initialize sound engine with given voice/output count. */
void sound_engine_init(int voice_count);

/* Shutdown and release all resources. */
void sound_engine_shutdown(void);

/* Load a track into the registry. Validates header, retains byte array.
 * Returns true on success. */
bool sound_engine_load_track(ID track_id, ID byte_array_obj);

/* Unload a track. Stops if playing, releases byte array.
 * Returns true if track was found and unloaded. */
bool sound_engine_unload_track(ID track_id);

/* Enqueue play command.
 * repeat: 0 = infinite, 1 = once, 2 = twice, etc. */
bool sound_engine_play_music(ID track_id, int32_t repeat);

/* Enqueue stop command for a specific track (no finished callback). */
bool sound_engine_stop_track(ID track_id);

/* Enqueue stop-all-music command. */
void sound_engine_stop_music(void);

/* Enqueue SFX play command. Returns false if queue full. */
bool sound_engine_play_sfx(ID sfx_id);

/* Enqueue stop-all command. */
void sound_engine_stop_all(void);

/* Set per-track volume (immediate if playing). */
bool sound_engine_set_track_volume(ID track_id, int32_t vol);

/* Set global music volume. */
void sound_engine_set_music_volume(int32_t vol);

/* Register on-finished callback. Retains fn, releases previous.
 * Callback receives {:source :audio :kind :finished :track-id ...}. */
void sound_engine_on_finished(ID callback_fn);

/* ========================================================================= */
/* Tick (called from timer ISR or host test harness)                         */
/* ========================================================================= */

/* Process one tick. Drains command queue, advances streams, updates voices.
 * voice_output is filled with freq_hz values for each voice (0 = silent). */
void sound_engine_tick(void);

/* ========================================================================= */
/* Voice backend (platform-specific)                                         */
/* ========================================================================= */

/* Called by tick when voice state changes. Platform implements this. */
void sound_backend_set_voice(int voice_index, uint16_t freq_hz, uint8_t volume);

/* Called on init to set up hardware. */
void sound_backend_init(int voice_count);

/* Called on shutdown. */
void sound_backend_shutdown(void);

/* ========================================================================= */
/* Tick lifecycle (platform-specific)                                         */
/* ========================================================================= */

void sound_tick_start(void);
void sound_tick_stop(void);

/* Host backend status helper (debug).
 * On unsupported platforms returns false. */
bool sound_backend_host_get_status(SoundHostStatus *out);

/* Host DEBUG-only pseudo-noise helper.
 * Returns false on unsupported platforms or invalid arguments. */
bool sound_backend_host_play_debug_noise(uint16_t min_freq_hz,
                                         uint16_t max_freq_hz,
                                         uint32_t duration_ms,
                                         uint32_t hop_ms,
                                         uint8_t volume);
bool sound_backend_host_debug_noise_active(void);
bool sound_backend_host_play_debug_ramp(uint16_t start_freq_hz,
                                        uint16_t end_freq_hz,
                                        uint32_t duration_ms,
                                        uint8_t volume);
bool sound_backend_host_debug_ramp_active(void);
bool sound_backend_host_play_debug_ramp_noise(uint16_t min_freq_hz,
                                              uint16_t max_freq_hz,
                                              uint32_t duration_ms,
                                              uint32_t hop_ms,
                                              uint8_t volume);
bool sound_backend_host_debug_ramp_noise_active(void);

/* ========================================================================= */
/* MIDI note to frequency conversion                                         */
/* ========================================================================= */

/* Convert MIDI note number (0..127) to frequency in Hz. Note 0 = rest. */
uint16_t midi_note_to_freq(uint8_t note);

#endif /* TINY_CLJ_SOUND_ENGINE_H */
