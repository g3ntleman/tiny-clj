/*
 * Audio Engine Tests
 *
 * Tests for trk1 format parsing, SPSC queue, track registry,
 * streaming engine, native API wiring, and ownership contracts.
 */

#include "tests_common.h"
#include "../audio_engine.h"
#include "byte_array.h"

/* ========================================================================= */
/* Helper: build a minimal trk1 byte array                                   */
/* ========================================================================= */

/* Build a minimal valid trk1 with a single NOTE event + END.
 * channel_count channels, bpm=120, tpq=480.
 * Returns a retained CljByteArray. Caller must RELEASE. */
static ID make_test_trk1(uint8_t note, uint32_t gate_ticks, uint8_t channel_count, uint8_t flags) {
    /* Encode gate_ticks as varuint */
    uint8_t gate_buf[4];
    int gate_len = 0;
    {
        uint32_t v = gate_ticks;
        do {
            uint8_t byte = v & 0x7F;
            v >>= 7;
            if (v) byte |= 0x80;
            gate_buf[gate_len++] = byte;
        } while (v);
    }

    /* Event stream: NOTE(ch0, has_delay=0) + note + gate + END(ch0, has_delay=0) */
    int stream_len = 1 + 1 + gate_len + 1; /* control + note + gate_varuint + END control */
    int total_len = TRK1_HEADER_SIZE + stream_len;

    CljByteArray *ba = make_byte_array(total_len);
    uint8_t *d = ba->data;

    /* Header */
    d[0] = 'T'; d[1] = 'R'; d[2] = 'K'; d[3] = '1';
    d[4] = TRK1_VERSION;
    d[5] = flags;
    d[6] = channel_count;
    d[7] = 0; /* reserved */
    d[8] = 480 & 0xFF; d[9] = (480 >> 8) & 0xFF; /* tpq = 480 */
    d[10] = 120 & 0xFF; d[11] = (120 >> 8) & 0xFF; /* bpm = 120 */
    d[12] = stream_len & 0xFF; d[13] = (stream_len >> 8) & 0xFF;
    d[14] = (stream_len >> 16) & 0xFF; d[15] = (stream_len >> 24) & 0xFF;
    d[16] = d[17] = d[18] = d[19] = 0; /* crc32 = 0 */

    /* Event stream */
    int off = TRK1_HEADER_SIZE;

    /* NOTE event: has_delay=0, type=NOTE(0), channel=0 */
    d[off++] = (0 << 7) | (TRK1_EVT_NOTE << 4) | 0;
    d[off++] = note;
    for (int i = 0; i < gate_len; i++) d[off++] = gate_buf[i];

    /* END event: has_delay=0, type=END(2), channel=0 */
    d[off++] = (0 << 7) | (TRK1_EVT_END << 4) | 0;

    return (ID)ba;
}

/* Build trk1 with NOTE + delay + END (for multi-tick streaming tests). */
static ID make_test_trk1_with_delay(uint8_t note, uint32_t gate_ticks, uint32_t delay_ticks,
                                     uint8_t channel_count) {
    /* Encode gate as varuint */
    uint8_t gate_buf[4];
    int gate_len = 0;
    {
        uint32_t v = gate_ticks;
        do {
            uint8_t byte = v & 0x7F;
            v >>= 7;
            if (v) byte |= 0x80;
            gate_buf[gate_len++] = byte;
        } while (v);
    }
    /* Encode delay as varuint */
    uint8_t delay_buf[4];
    int delay_len = 0;
    {
        uint32_t v = delay_ticks;
        do {
            uint8_t byte = v & 0x7F;
            v >>= 7;
            if (v) byte |= 0x80;
            delay_buf[delay_len++] = byte;
        } while (v);
    }

    int stream_len = 1 + 1 + gate_len + delay_len + 1; /* control+note+gate+delay + END */
    int total_len = TRK1_HEADER_SIZE + stream_len;

    CljByteArray *ba = make_byte_array(total_len);
    uint8_t *d = ba->data;

    d[0] = 'T'; d[1] = 'R'; d[2] = 'K'; d[3] = '1';
    d[4] = TRK1_VERSION; d[5] = 0; d[6] = channel_count; d[7] = 0;
    d[8] = 480 & 0xFF; d[9] = (480 >> 8) & 0xFF;
    d[10] = 120 & 0xFF; d[11] = (120 >> 8) & 0xFF;
    d[12] = stream_len & 0xFF; d[13] = (stream_len >> 8) & 0xFF;
    d[14] = (stream_len >> 16) & 0xFF; d[15] = (stream_len >> 24) & 0xFF;
    d[16] = d[17] = d[18] = d[19] = 0;

    int off = TRK1_HEADER_SIZE;

    /* NOTE with has_delay=1 */
    d[off++] = (1 << 7) | (TRK1_EVT_NOTE << 4) | 0;
    d[off++] = note;
    for (int i = 0; i < gate_len; i++) d[off++] = gate_buf[i];
    for (int i = 0; i < delay_len; i++) d[off++] = delay_buf[i];

    /* END */
    d[off++] = (0 << 7) | (TRK1_EVT_END << 4) | 0;

    return (ID)ba;
}

/* ========================================================================= */
/* trk1 header parsing tests                                                 */
/* ========================================================================= */

TEST(test_audio_trk1_parse_header_valid) {
    ID ba = make_test_trk1(69, 100, 2, 0);
    CljByteArray *arr = as_byte_array(ba);

    Trk1Header h;
    TEST_ASSERT_TRUE(trk1_parse_header(arr->data, arr->length, &h));
    TEST_ASSERT_EQUAL_UINT8(TRK1_VERSION, h.version);
    TEST_ASSERT_EQUAL_UINT8(2, h.channel_count);
    TEST_ASSERT_EQUAL_UINT16(480, h.tpq);
    TEST_ASSERT_EQUAL_UINT16(120, h.bpm);
    TEST_ASSERT_NOT_NULL(h.stream_start);

    RELEASE(ba);
}

TEST(test_audio_trk1_parse_header_bad_magic) {
    uint8_t data[TRK1_HEADER_SIZE] = {0};
    data[0] = 'X'; /* wrong magic */
    Trk1Header h;
    TEST_ASSERT_FALSE(trk1_parse_header(data, TRK1_HEADER_SIZE, &h));
}

TEST(test_audio_trk1_parse_header_bad_version) {
    uint8_t data[TRK1_HEADER_SIZE] = {0};
    data[0] = 'T'; data[1] = 'R'; data[2] = 'K'; data[3] = '1';
    data[4] = 99; /* wrong version */
    Trk1Header h;
    TEST_ASSERT_FALSE(trk1_parse_header(data, TRK1_HEADER_SIZE, &h));
}

TEST(test_audio_trk1_parse_header_zero_channels) {
    uint8_t data[TRK1_HEADER_SIZE] = {0};
    data[0] = 'T'; data[1] = 'R'; data[2] = 'K'; data[3] = '1';
    data[4] = TRK1_VERSION; data[5] = 0; data[6] = 0; /* 0 channels */
    data[8] = 1; data[10] = 120; /* tpq=1, bpm=120 */
    Trk1Header h;
    TEST_ASSERT_FALSE(trk1_parse_header(data, TRK1_HEADER_SIZE, &h));
}

TEST(test_audio_trk1_parse_header_too_short) {
    uint8_t data[10] = {0};
    Trk1Header h;
    TEST_ASSERT_FALSE(trk1_parse_header(data, 10, &h));
}

TEST(test_audio_trk1_parse_header_null) {
    Trk1Header h;
    TEST_ASSERT_FALSE(trk1_parse_header(NULL, 0, &h));
    TEST_ASSERT_FALSE(trk1_parse_header(NULL, 100, NULL));
}

/* ========================================================================= */
/* Varuint decoder tests                                                     */
/* ========================================================================= */

TEST(test_audio_varuint_single_byte) {
    uint8_t data[] = {42};
    const uint8_t *cursor = data;
    uint32_t val = 0;
    TEST_ASSERT_TRUE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
    TEST_ASSERT_EQUAL_UINT32(42, val);
    TEST_ASSERT_EQUAL_PTR(data + 1, cursor);
}

TEST(test_audio_varuint_multibyte) {
    /* 300 = 0b100101100 -> 0xAC 0x02 in base-128 */
    uint8_t data[] = {0xAC, 0x02};
    const uint8_t *cursor = data;
    uint32_t val = 0;
    TEST_ASSERT_TRUE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
    TEST_ASSERT_EQUAL_UINT32(300, val);
}

TEST(test_audio_varuint_max_value) {
    /* TRK1_VARUINT_MAX = 0x0FFFFFFF = 268435455 */
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0x7F}; /* 0x0FFFFFFF in varuint */
    const uint8_t *cursor = data;
    uint32_t val = 0;
    TEST_ASSERT_TRUE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
    TEST_ASSERT_EQUAL_UINT32(TRK1_VARUINT_MAX, val);
}

TEST(test_audio_varuint_overflow) {
    /* 5-byte varuint would overflow */
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    const uint8_t *cursor = data;
    uint32_t val = 0;
    TEST_ASSERT_FALSE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
}

TEST(test_audio_varuint_truncated) {
    uint8_t data[] = {0x80}; /* continuation bit set, but no more data */
    const uint8_t *cursor = data;
    uint32_t val = 0;
    TEST_ASSERT_FALSE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
}

/* ========================================================================= */
/* SPSC command queue tests                                                  */
/* ========================================================================= */

TEST(test_audio_cmd_queue_push_pop) {
    AudioCmdQueue q;
    audio_cmd_queue_init(&q);

    AudioCmd cmd = {.type = AUDIO_CMD_PLAY_TRACK, .track_id = NULL, .int_param = 1};
    TEST_ASSERT_TRUE(audio_cmd_push(&q, cmd));

    AudioCmd out;
    TEST_ASSERT_TRUE(audio_cmd_pop(&q, &out));
    TEST_ASSERT_EQUAL_INT(AUDIO_CMD_PLAY_TRACK, out.type);
    TEST_ASSERT_EQUAL_INT(1, out.int_param);
}

TEST(test_audio_cmd_queue_empty) {
    AudioCmdQueue q;
    audio_cmd_queue_init(&q);

    TEST_ASSERT_TRUE(audio_cmd_queue_empty(&q));

    AudioCmd cmd = {.type = AUDIO_CMD_STOP_ALL};
    audio_cmd_push(&q, cmd);
    TEST_ASSERT_FALSE(audio_cmd_queue_empty(&q));

    AudioCmd out;
    audio_cmd_pop(&q, &out);
    TEST_ASSERT_TRUE(audio_cmd_queue_empty(&q));
}

TEST(test_audio_cmd_queue_full) {
    AudioCmdQueue q;
    audio_cmd_queue_init(&q);
    g_audio_engine.telemetry.cmd_drop_count = 0;

    for (int i = 0; i < AUDIO_CMD_QUEUE_CAP; i++) {
        AudioCmd cmd = {.type = AUDIO_CMD_STOP_ALL};
        TEST_ASSERT_TRUE(audio_cmd_push(&q, cmd));
    }

    /* Queue full: next push should fail */
    AudioCmd cmd = {.type = AUDIO_CMD_STOP_ALL};
    TEST_ASSERT_FALSE(audio_cmd_push(&q, cmd));
    TEST_ASSERT_EQUAL_UINT32(1, g_audio_engine.telemetry.cmd_drop_count);
}

TEST(test_audio_cmd_queue_wraparound) {
    AudioCmdQueue q;
    audio_cmd_queue_init(&q);

    /* Fill and drain multiple times to test wraparound */
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < AUDIO_CMD_QUEUE_CAP; i++) {
            AudioCmd cmd = {.type = AUDIO_CMD_PLAY_TRACK, .int_param = round * 100 + i};
            TEST_ASSERT_TRUE(audio_cmd_push(&q, cmd));
        }
        for (int i = 0; i < AUDIO_CMD_QUEUE_CAP; i++) {
            AudioCmd out;
            TEST_ASSERT_TRUE(audio_cmd_pop(&q, &out));
            TEST_ASSERT_EQUAL_INT(round * 100 + i, out.int_param);
        }
        TEST_ASSERT_TRUE(audio_cmd_queue_empty(&q));
    }
}

/* ========================================================================= */
/* Track registry + ownership tests                                          */
/* ========================================================================= */

TEST(test_audio_load_unload_contract) {
    audio_engine_init(2);

    ID track_sym = (ID)intern_symbol_global(":test-track");
    ID ba = make_test_trk1(69, 100, 2, 0);

    /* Load: retain count should increase */
    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_EQUAL_INT(1, g_audio_engine.track_count);

    /* Unload: should release */
    TEST_ASSERT_TRUE(audio_engine_unload_track(track_sym));
    TEST_ASSERT_EQUAL_INT(0, g_audio_engine.track_count);

    /* Unload non-existent: false */
    TEST_ASSERT_FALSE(audio_engine_unload_track(track_sym));

    RELEASE(ba);
    audio_engine_shutdown();
}

TEST(test_audio_load_invalid_data) {
    audio_engine_init(2);

    ID track_sym = (ID)intern_symbol_global(":bad-track");

    /* Non-byte-array should fail */
    TEST_ASSERT_FALSE(audio_engine_load_track(track_sym, fixnum(42)));

    /* Short data should fail */
    CljByteArray *short_ba = make_byte_array(5);
    TEST_ASSERT_FALSE(audio_engine_load_track(track_sym, (ID)short_ba));
    RELEASE(short_ba);

    /* NULL args should fail */
    TEST_ASSERT_FALSE(audio_engine_load_track(NULL, NULL));

    audio_engine_shutdown();
}

TEST(test_audio_load_replaces_existing) {
    audio_engine_init(2);

    ID track_sym = (ID)intern_symbol_global(":replace-track");
    ID ba1 = make_test_trk1(60, 100, 1, 0);
    ID ba2 = make_test_trk1(72, 200, 1, 0);

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba1));
    TEST_ASSERT_EQUAL_INT(1, g_audio_engine.track_count);

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba2));
    TEST_ASSERT_EQUAL_INT(1, g_audio_engine.track_count);

    RELEASE(ba1);
    RELEASE(ba2);
    audio_engine_shutdown();
}

/* ========================================================================= */
/* Streaming + tick tests                                                    */
/* ========================================================================= */

TEST(test_audio_play_and_tick_basic) {
    audio_engine_init(2);

    ID track_sym = (ID)intern_symbol_global(":tick-test");
    ID ba = make_test_trk1(69, 5, 1, 0); /* A4, gate=5 ticks */

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 1));

    /* First tick: drains command, parses NOTE + END, sets voice.
     * NOTE has no delay, so END is parsed in the same tick.
     * Stream ends (repeat=1), but voice was set. */
    audio_engine_tick();
    /* Stream may already be inactive (NOTE+END parsed in same tick),
     * but the voice should have been set by the NOTE. */
    TEST_ASSERT_EQUAL_UINT16(440, g_audio_engine.voices[0].freq_hz);
    TEST_ASSERT_TRUE(g_audio_engine.voices[0].gate_remaining_ticks > 0);

    /* Tick through gate */
    for (int i = 0; i < 6; i++) {
        audio_engine_tick();
    }

    /* After gate expires: voice should be silent */
    TEST_ASSERT_EQUAL_UINT16(0, g_audio_engine.voices[0].freq_hz);

    RELEASE(ba);
    audio_engine_shutdown();
}

TEST(test_audio_play_with_delay) {
    audio_engine_init(1);

    ID track_sym = (ID)intern_symbol_global(":delay-test");
    ID ba = make_test_trk1_with_delay(69, 3, 5, 1); /* note=A4, gate=3, delay=5 before END */

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 1));

    audio_engine_tick(); /* drains play command, parses NOTE (delay=5 before next event) */
    TEST_ASSERT_TRUE(g_audio_engine.music_stream.active);

    /* Stream should still be active during delay period */
    for (int i = 0; i < 4; i++) {
        audio_engine_tick();
        TEST_ASSERT_TRUE(g_audio_engine.music_stream.active);
    }

    /* After delay: END is parsed -> stream ends (repeat=1 = play once) */
    audio_engine_tick(); /* tick 5: current_tick reaches next_event_tick, parses END */
    /* May need one more tick for END processing */
    audio_engine_tick();

    TEST_ASSERT_FALSE(g_audio_engine.music_stream.active);

    RELEASE(ba);
    audio_engine_shutdown();
}

TEST(test_audio_repeat_infinite) {
    audio_engine_init(1);

    ID track_sym = (ID)intern_symbol_global(":repeat-test");
    ID ba = make_test_trk1(60, 2, 1, 0); /* C4, gate=2 */

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 0)); /* 0 = infinite */

    audio_engine_tick(); /* drains command, parses NOTE + END -> loop rewind */
    TEST_ASSERT_TRUE(g_audio_engine.music_stream.active);

    /* Tick many times: should stay active (infinite loop) */
    for (int i = 0; i < 50; i++) {
        audio_engine_tick();
    }
    TEST_ASSERT_TRUE(g_audio_engine.music_stream.active);

    RELEASE(ba);
    audio_engine_shutdown();
}

TEST(test_audio_stop_track_no_finished_callback) {
    audio_engine_init(1);

    ID track_sym = (ID)intern_symbol_global(":stop-test");
    ID ba = make_test_trk1(60, 100, 1, 0);

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 0));

    audio_engine_tick(); /* start playing */
    TEST_ASSERT_TRUE(g_audio_engine.music_stream.active);

    TEST_ASSERT_TRUE(audio_engine_stop_track(track_sym));
    audio_engine_tick(); /* drain stop command */
    TEST_ASSERT_FALSE(g_audio_engine.music_stream.active);

    RELEASE(ba);
    audio_engine_shutdown();
}

TEST(test_audio_set_track_volume_during_playback) {
    audio_engine_init(1);

    ID track_sym = (ID)intern_symbol_global(":vol-test");
    ID ba = make_test_trk1(69, 100, 1, 0);

    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 0));

    audio_engine_tick(); /* start */
    TEST_ASSERT_EQUAL_UINT8(255, g_audio_engine.music_stream.track_volume);

    TEST_ASSERT_TRUE(audio_engine_set_track_volume(track_sym, 128));
    audio_engine_tick(); /* drain volume command */
    TEST_ASSERT_EQUAL_UINT8(128, g_audio_engine.music_stream.track_volume);

    RELEASE(ba);
    audio_engine_shutdown();
}

/* ========================================================================= */
/* SFX one-shot tests                                                        */
/* ========================================================================= */

TEST(test_audio_sfx_oneshot) {
    audio_engine_init(2);

    ID sfx_sym = (ID)intern_symbol_global(":sfx-laser");
    /* Use delay so SFX lasts multiple ticks: NOTE(gate=10) + delay=5 + END */
    ID ba = make_test_trk1_with_delay(80, 10, 5, 1);

    TEST_ASSERT_TRUE(audio_engine_load_track(sfx_sym, ba));
    TEST_ASSERT_TRUE(audio_engine_play_sfx(sfx_sym));

    audio_engine_tick(); /* drains SFX command, parses NOTE (delay=5 before END) */

    bool sfx_active = false;
    for (int i = 0; i < AUDIO_MAX_SFX; i++) {
        if (g_audio_engine.sfx[i].stream.active) sfx_active = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sfx_active, "SFX should be active after first tick");

    /* Tick through: SFX should eventually end */
    for (int i = 0; i < 20; i++) {
        audio_engine_tick();
    }

    sfx_active = false;
    for (int i = 0; i < AUDIO_MAX_SFX; i++) {
        if (g_audio_engine.sfx[i].stream.active) sfx_active = true;
    }
    TEST_ASSERT_FALSE_MESSAGE(sfx_active, "SFX should have ended after enough ticks");

    RELEASE(ba);
    audio_engine_shutdown();
}

/* ========================================================================= */
/* Native API wiring tests (via eval_string)                                 */
/* ========================================================================= */

TEST(test_audio_native_lookup) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    /* All audio builtins should be resolvable */
    const char *names[] = {
        "audio-load-track!", "audio-unload-track!",
        "audio-play-music!", "audio-stop-track!",
        "audio-stop-music!", "audio-play-sfx!",
        "audio-stop-all!", "audio-set-track-volume!",
        "audio-set-music-volume!", "audio-on-finished!",
    };

    for (int i = 0; i < 10; i++) {
        char buf[128];
        test_snprintf(buf, sizeof(buf), "(fn? %s)", names[i]);
        ID result = NULL;
        TRY {
            result = eval_string(buf, g_test_eval_state);
        } CATCH(ex) {
            char msg[256];
            test_snprintf(msg, sizeof(msg), "Failed to resolve: %s", names[i]);
            TEST_FAIL_MESSAGE(msg);
        } END_TRY
        TEST_ASSERT_NOT_NULL_MESSAGE(result, names[i]);
    }
}

TEST(test_audio_native_load_unload_via_eval) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    /* Initialize audio engine for this test */
    audio_engine_init(2);

    /* Create a trk1 byte array and load it via the engine directly
     * (eval_string can't easily create binary data) */
    ID track_sym = (ID)intern_symbol_global(":eval-track");
    ID ba = make_test_trk1(69, 10, 2, 0);
    TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
    TEST_ASSERT_EQUAL_INT(1, g_audio_engine.track_count);
    TEST_ASSERT_TRUE(audio_engine_unload_track(track_sym));
    TEST_ASSERT_EQUAL_INT(0, g_audio_engine.track_count);

    RELEASE(ba);
    audio_engine_shutdown();
}

/* ========================================================================= */
/* MIDI note to frequency conversion                                         */
/* ========================================================================= */

TEST(test_audio_midi_note_to_freq) {
    TEST_ASSERT_EQUAL_UINT16(0, midi_note_to_freq(0));     /* rest */
    TEST_ASSERT_EQUAL_UINT16(262, midi_note_to_freq(60));   /* C4 */
    TEST_ASSERT_EQUAL_UINT16(440, midi_note_to_freq(69));   /* A4 */
    TEST_ASSERT_EQUAL_UINT16(880, midi_note_to_freq(81));   /* A5 */
    TEST_ASSERT_EQUAL_UINT16(0, midi_note_to_freq(128));    /* out of range */
}

/* ========================================================================= */
/* Tick on-demand lifecycle tests                                            */
/* ========================================================================= */

TEST(test_audio_tick_starts_on_play) {
    audio_engine_init(1);
    TEST_ASSERT_FALSE(g_audio_engine.tick_running);

    ID track_sym = (ID)intern_symbol_global(":lifecycle-test");
    ID ba = make_test_trk1(60, 2, 1, 0);
    audio_engine_load_track(track_sym, ba);

    audio_engine_play_music(track_sym, 1);
    TEST_ASSERT_TRUE(g_audio_engine.tick_running);

    /* Tick until stream ends */
    for (int i = 0; i < 20; i++) {
        audio_engine_tick();
    }
    /* After stream ends: tick should self-stop */
    TEST_ASSERT_FALSE(g_audio_engine.tick_running);

    RELEASE(ba);
    audio_engine_shutdown();
}

TEST(test_audio_stop_all_silences_voices) {
    audio_engine_init(2);

    ID track_sym = (ID)intern_symbol_global(":stopall-test");
    ID ba = make_test_trk1(69, 1000, 1, 0);
    audio_engine_load_track(track_sym, ba);
    audio_engine_play_music(track_sym, 0);

    audio_engine_tick(); /* start */
    TEST_ASSERT_TRUE(g_audio_engine.voices[0].freq_hz > 0);

    audio_engine_stop_all();
    audio_engine_tick(); /* drain stop_all */

    for (int i = 0; i < g_audio_engine.voice_count; i++) {
        TEST_ASSERT_FALSE(g_audio_engine.voices[i].active);
    }

    RELEASE(ba);
    audio_engine_shutdown();
}
