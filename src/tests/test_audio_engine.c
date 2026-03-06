/*
 * Audio Engine Tests
 *
 * Tests for trk1 format parsing, SPSC queue, track registry,
 * streaming engine, native API wiring, and ownership contracts.
 */

#include "tests_common.h"
#include "../audio_engine.h"
#include "../audio_tick_scheduler.h"
#include "../event_loop.h"
#include "byte_array.h"

/* ========================================================================= */
/* Helper: build a minimal trk1 byte array                                   */
/* ========================================================================= */

static int encode_test_varuint(uint32_t value, uint8_t *out, int cap) {
  int len = 0;
  if (!out || cap <= 0) {
    return 0;
  }
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value) {
      byte |= 0x80;
    }
    if (len >= cap) {
      return 0;
    }
    out[len++] = byte;
  } while (value);
  return len;
}

static void write_test_trk1_header(uint8_t *data, uint8_t channel_count, uint8_t flags, int stream_len) {
  TEST_ASSERT_NOT_NULL(data);
  data[0] = 'T';
  data[1] = 'R';
  data[2] = 'K';
  data[3] = '1';
  data[4] = TRK1_VERSION;
  data[5] = flags;
  data[6] = channel_count;
  data[7] = 0;
  data[8] = 480 & 0xFF;
  data[9] = (480 >> 8) & 0xFF;
  data[10] = 120 & 0xFF;
  data[11] = (120 >> 8) & 0xFF;
  data[12] = stream_len & 0xFF;
  data[13] = (stream_len >> 8) & 0xFF;
  data[14] = (stream_len >> 16) & 0xFF;
  data[15] = (stream_len >> 24) & 0xFF;
  data[16] = 0;
  data[17] = 0;
  data[18] = 0;
  data[19] = 0;
}

static ID make_test_trk1_common(uint8_t note,
                                uint32_t gate_ticks,
                                uint32_t delay_ticks,
                                bool include_delay,
                                uint8_t channel_count,
                                uint8_t flags) {
  uint8_t gate_buf[4];
  uint8_t delay_buf[4];
  int gate_len = encode_test_varuint(gate_ticks, gate_buf, (int)sizeof(gate_buf));
  int delay_len = include_delay ? encode_test_varuint(delay_ticks, delay_buf, (int)sizeof(delay_buf)) : 0;
  TEST_ASSERT_TRUE(gate_len > 0);
  TEST_ASSERT_TRUE(!include_delay || delay_len > 0);

  int stream_len = 1 + 1 + gate_len + delay_len + 1;
  int total_len = TRK1_HEADER_SIZE + stream_len;

  CljByteArray *ba = make_byte_array(total_len);
  uint8_t *d = ba->data;
  write_test_trk1_header(d, channel_count, flags, stream_len);

  int off = TRK1_HEADER_SIZE;
  d[off++] = ((include_delay ? 1 : 0) << 7) | (TRK1_EVT_NOTE << 4);
  d[off++] = note;
  for (int i = 0; i < gate_len; i++) {
    d[off++] = gate_buf[i];
  }
  for (int i = 0; i < delay_len; i++) {
    d[off++] = delay_buf[i];
  }
  d[off++] = (TRK1_EVT_END << 4);
  return (ID)ba;
}

/* Build a minimal valid trk1 with a single NOTE event + END.
 * channel_count channels, bpm=120, tpq=480.
 * Returns a retained CljByteArray. Caller must RELEASE. */
static ID make_test_trk1(uint8_t note, uint32_t gate_ticks, uint8_t channel_count, uint8_t flags) {
  return make_test_trk1_common(note, gate_ticks, 0, false, channel_count, flags);
}

/* Build trk1 with NOTE + delay + END (for multi-tick streaming tests). */
static ID make_test_trk1_with_delay(uint8_t note, uint32_t gate_ticks, uint32_t delay_ticks,
                                    uint8_t channel_count) {
  return make_test_trk1_common(note, gate_ticks, delay_ticks, true, channel_count, 0);
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
  data[0] = 'T';
  data[1] = 'R';
  data[2] = 'K';
  data[3] = '1';
  data[4] = 99; /* wrong version */
  Trk1Header h;
  TEST_ASSERT_FALSE(trk1_parse_header(data, TRK1_HEADER_SIZE, &h));
}

TEST(test_audio_trk1_parse_header_zero_channels) {
  uint8_t data[TRK1_HEADER_SIZE] = {0};
  data[0] = 'T';
  data[1] = 'R';
  data[2] = 'K';
  data[3] = '1';
  data[4] = TRK1_VERSION;
  data[5] = 0;
  data[6] = 0; /* 0 channels */
  data[8] = 1;
  data[10] = 120; /* tpq=1, bpm=120 */
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

static void init_audio_cmd_test_queue(AudioCmdQueue *q) {
  TEST_ASSERT_NOT_NULL(q);
  memset(q->slots, 0, sizeof(q->slots));
  TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q->spsc, q->slots, AUDIO_CMD_QUEUE_CAP, sizeof(q->slots[0])));
}

TEST(test_audio_cmd_queue_push_pop) {
  AudioCmdQueue q;
  init_audio_cmd_test_queue(&q);

  AudioCmd cmd = {.type = AUDIO_CMD_PLAY_TRACK, .track_id = NULL, .int_param = 1};
  TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));

  AudioCmd out;
  TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q.spsc, &out));
  TEST_ASSERT_EQUAL_INT(AUDIO_CMD_PLAY_TRACK, out.type);
  TEST_ASSERT_EQUAL_INT(1, out.int_param);
}

TEST(test_audio_cmd_queue_empty) {
  AudioCmdQueue q;
  init_audio_cmd_test_queue(&q);

  TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q.spsc));

  AudioCmd cmd = {.type = AUDIO_CMD_STOP_ALL};
  TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));
  TEST_ASSERT_FALSE(lockfree_spsc_queue_empty(&q.spsc));

  AudioCmd out;
  TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q.spsc, &out));
  TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q.spsc));
}

TEST(test_audio_cmd_queue_full) {
  AudioCmdQueue q;
  init_audio_cmd_test_queue(&q);

  for (int i = 0; i < AUDIO_CMD_QUEUE_CAP; i++) {
    AudioCmd cmd = {.type = AUDIO_CMD_STOP_ALL};
    TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));
  }

  /* Queue full: next push should fail */
  AudioCmd cmd = {.type = AUDIO_CMD_STOP_ALL};
  TEST_ASSERT_FALSE(lockfree_spsc_queue_push(&q.spsc, &cmd));
}

TEST(test_audio_cmd_queue_wraparound) {
  AudioCmdQueue q;
  init_audio_cmd_test_queue(&q);

  /* Fill and drain multiple times to test wraparound */
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < AUDIO_CMD_QUEUE_CAP; i++) {
      AudioCmd cmd = {.type = AUDIO_CMD_PLAY_TRACK, .int_param = round * 100 + i};
      TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));
    }
    for (int i = 0; i < AUDIO_CMD_QUEUE_CAP; i++) {
      AudioCmd out;
      TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q.spsc, &out));
      TEST_ASSERT_EQUAL_INT(round * 100 + i, out.int_param);
    }
    TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q.spsc));
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
    if (g_audio_engine.sfx[i].stream.active)
      sfx_active = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(sfx_active, "SFX should be active after first tick");

  /* Tick through: SFX should eventually end */
  for (int i = 0; i < 20; i++) {
    audio_engine_tick();
  }

  sfx_active = false;
  for (int i = 0; i < AUDIO_MAX_SFX; i++) {
    if (g_audio_engine.sfx[i].stream.active)
      sfx_active = true;
  }
  TEST_ASSERT_FALSE_MESSAGE(sfx_active, "SFX should have ended after enough ticks");

  RELEASE(ba);
  audio_engine_shutdown();
}

TEST(test_audio_sfx_drop_when_all_slots_busy) {
  audio_engine_init(2);

  ID sfx_sym = (ID)intern_symbol_global(":sfx-drop");
  ID ba = make_test_trk1_with_delay(80, 100, 30, 1);

  TEST_ASSERT_TRUE(audio_engine_load_track(sfx_sym, ba));
  g_audio_engine.telemetry.sfx_drop_count = 0;

  TEST_ASSERT_TRUE(audio_engine_play_sfx(sfx_sym));
  audio_engine_tick();
  TEST_ASSERT_TRUE(audio_engine_play_sfx(sfx_sym));
  audio_engine_tick();

  TEST_ASSERT_FALSE(audio_engine_play_sfx(sfx_sym));
  TEST_ASSERT_EQUAL_UINT32(1, g_audio_engine.telemetry.sfx_drop_count);

  RELEASE(ba);
  audio_engine_shutdown();
}

TEST(test_audio_finished_callback_runs_via_event_loop) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  audio_engine_init(1);
  event_loop_clear();

  ID track_sym = (ID)intern_symbol_global(":finish-test");
  ID ba = make_test_trk1(69, 2, 1, 0);
  TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));

  ID atom_def = eval_string("(def audio-finished-track (atom nil))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(atom_def);
  ID cb_def = eval_string(
      "(def audio-finished-cb "
      "  (fn [event] "
      "    (reset! audio-finished-track [(:source event) (:kind event) (:track-id event)]) "
      "    nil))",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(cb_def);
  ID cb_fn = eval_string("audio-finished-cb", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(cb_fn);

  audio_engine_on_finished(cb_fn);
  TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 1));

  audio_engine_tick();
  TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
  TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

  ID done = eval_string("(= @audio-finished-track [:audio :finished :finish-test])", g_test_eval_state);
  TEST_ASSERT_EQUAL(clj_true, done);

  RELEASE(ba);
  audio_engine_shutdown();
}

TEST(test_audio_native_on_finished_callback_receives_event_map_shape) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  audio_engine_init(1);
  event_loop_clear();

  ID track_sym = (ID)intern_symbol_global(":finish-shape-test");
  ID ba = make_test_trk1(72, 2, 1, 0);
  TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));

  ID setup = eval_string(
      "(do "
      "  (def audio-finished-shape (atom nil)) "
      "  (tiny-fx.sound/audio-on-finished! "
      "    (fn [event] "
      "      (reset! audio-finished-shape "
      "              [(map? event) (:source event) (:kind event) (:track-id event)]) "
      "      nil)) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(setup);

  TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 1));
  audio_engine_tick();
  TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
  TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

  ID done = eval_string("(= @audio-finished-shape [true :audio :finished :finish-shape-test])",
                        g_test_eval_state);
  TEST_ASSERT_EQUAL(clj_true, done);

  RELEASE(ba);
  audio_engine_shutdown();
}

/* ========================================================================= */
/* Native API wiring tests (via eval_string)                                 */
/* ========================================================================= */

TEST(test_audio_native_lookup) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  /* All audio builtins in tiny-fx.sound should be resolvable */
  const char *names[] = {
      "tiny-fx.sound/audio-load-track!",
      "tiny-fx.sound/audio-unload-track!",
      "tiny-fx.sound/audio-play-music!",
      "tiny-fx.sound/audio-stop-track!",
      "tiny-fx.sound/audio-stop-music!",
      "tiny-fx.sound/audio-play-sfx!",
      "tiny-fx.sound/audio-stop-all!",
      "tiny-fx.sound/audio-set-track-volume!",
      "tiny-fx.sound/audio-set-music-volume!",
      "tiny-fx.sound/audio-on-finished!",
      "tiny-fx.sound/audio-play-test-tone!",
      "tiny-fx.sound/audio-host-status!",
  };

  for (int i = 0; i < 12; i++) {
    char buf[192];
    test_snprintf(buf, sizeof(buf), "(do (require 'tiny-fx.sound) (fn? %s))", names[i]);
    ID result = NULL;
    TRY {
      result = eval_string(buf, g_test_eval_state);
    }
    CATCH(ex) {
      char msg[256];
      test_snprintf(msg, sizeof(msg), "Failed to resolve: %s", names[i]);
      TEST_FAIL_MESSAGE(msg);
    }
    END_TRY
    TEST_ASSERT_NOT_NULL_MESSAGE(result, names[i]);
  }
}

TEST(test_audio_native_play_music_initializes_engine_if_needed) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  audio_engine_shutdown();
  TEST_ASSERT_EQUAL_INT(0, g_audio_engine.voice_count);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do "
        "  (require 'tiny-fx.sound) "
        "  (= :playing "
        "     (tiny-fx.sound/play-steps! :lazy-init "
        "       [{:notes [:G5 :D5] :dur :s}] "
        "       {:channel-count 2 :volumes [220 180]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("audio native play path should initialize engine lazily");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
  TEST_ASSERT_TRUE(g_audio_engine.voice_count > 0);

  audio_engine_shutdown();
}

TEST(test_audio_native_play_test_tone_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string("(tiny-fx.sound/audio-play-test-tone! 440 100)", g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("audio-play-test-tone! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_audio_native_play_test_tone_with_volume_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string("(tiny-fx.sound/audio-play-test-tone! 523 120 64)", g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("audio-play-test-tone! with volume should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_audio_native_host_status_returns_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string("(tiny-fx.sound/audio-host-status!)", g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("audio-host-status! should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
}

TEST(test_audio_tiny_fx_sound_namespace_compile_and_play) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do "
        "  (require 'tiny-fx.sound) "
        "  (= :playing "
        "     (tiny-fx.sound/play-steps! :dsl-test "
        "       [{:notes [:G5 :D5] :dur :s} {:notes [:Bb5 :F5] :dur :e}] "
        "       {:channel-count 2 :volumes [200 120]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound namespace should compile and play steps");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_play_starwars_title_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) (= :playing (tiny-fx.sound/play-starwars-title!)))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-starwars-title! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_play_steps_one_voice_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :one-voice "
        "         [{:notes [:G5] :dur :s} {:notes [:D5] :dur :s}] "
        "         {:channel-count 1 :volumes [210]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! one-voice should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_play_steps_musical_durations_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :musical-dur "
        "         [{:notes [:G5 :D5] :dur :q} "
        "          {:notes [:Bb5 :F5] :dur :dq} "
        "          {:notes [:A5 :E5] :dur :et}] "
        "         {:channel-count 2 :volumes [220 170] :tempo-bpm 104})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with musical durations should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_play_steps_rest_shorthand_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :rest-shorthand "
        "         [{:notes [:G5 :D5] :dur :q} "
        "          {:rest :e} "
        "          {:notes [:Bb5 :F5] :dur :q}] "
        "         {:channel-count 2 :volumes [220 170] :tempo-bpm 104})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with rest shorthand should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_numeric_duration_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :numeric-dur "
        "      [{:notes [:G5 :D5] :dur 120}] "
        "      {:channel-count 2 :volumes [220 170]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_audio_tiny_fx_sound_rest_with_dur_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :bad-rest "
        "      [{:notes [:G5 :D5] :dur :q} "
        "       {:rest :e :dur :e}] "
        "      {:channel-count 2 :volumes [220 170]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_audio_tiny_fx_sound_play_steps_four_voices_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :four-voice "
        "         [{:notes [:G5 :D5 :Bb4 :F4] :dur :e} {:notes [:A5 :E5 :C5 :G4] :dur :q}] "
        "         {:channel-count 4 :volumes [220 170 130 100]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! four-voice should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_play_steps_two_voices_activate_two_voices) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :two-voice-activation "
        "         [{:notes [:G5 :D5] :dur :q}] "
        "         {:channel-count 2 :volumes [220 170]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! two-voice should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
  audio_engine_tick(); /* Drain command queue and parse first step */
  TEST_ASSERT_TRUE(g_audio_engine.voices[0].active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[1].active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[0].freq_hz > 0);
  TEST_ASSERT_TRUE(g_audio_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_audio_engine.voices[0].freq_hz != g_audio_engine.voices[1].freq_hz);
}

TEST(test_audio_tiny_fx_sound_play_melody_backing_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :mel-backing "
        "         [{:melody :G5 :backing [:D5 :Bb4] :dur :e} "
        "          {:melody :A5 :backing [:E5 :C5] :dur :e}] "
        "         {:channel-count 3 :melody-vol 220 :backing-volumes [160 130]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with melody/backing should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_audio_tiny_fx_sound_play_melody_backing_auto_uses_available_channels) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  audio_engine_shutdown();
  audio_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= :playing "
        "       (tiny-fx.sound/play-steps! :mel-backing-auto "
        "         [{:melody :G5 :backing [:D5 :Bb4] :dur :q}] "
        "         {:channel-count 4 :melody-vol 220 :backing-volumes [170 150 130]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! melody/backing should auto-fill available channels");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
  audio_engine_tick(); /* Drain command queue and parse first step */
  TEST_ASSERT_TRUE(g_audio_engine.voices[0].active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[1].active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[2].active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[3].active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[0].freq_hz > 0);
  TEST_ASSERT_TRUE(g_audio_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_audio_engine.voices[2].freq_hz > 0);
  TEST_ASSERT_TRUE(g_audio_engine.voices[3].freq_hz > 0);
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
  TEST_ASSERT_EQUAL_UINT16(0, midi_note_to_freq(0));    /* rest */
  TEST_ASSERT_EQUAL_UINT16(262, midi_note_to_freq(60)); /* C4 */
  TEST_ASSERT_EQUAL_UINT16(440, midi_note_to_freq(69)); /* A4 */
  TEST_ASSERT_EQUAL_UINT16(880, midi_note_to_freq(81)); /* A5 */
  TEST_ASSERT_EQUAL_UINT16(0, midi_note_to_freq(128));  /* out of range */
}

TEST(test_audio_tick_scheduler_waits_until_deadline) {
  AudioTickScheduler scheduler;
  uint32_t skipped = 99u;

  audio_tick_scheduler_init(&scheduler, 1000000u, 4u);
  audio_tick_scheduler_start(&scheduler, 10000000u);

  TEST_ASSERT_EQUAL_UINT32(0u, audio_tick_scheduler_ticks_due(&scheduler, 10000000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT32(0u, audio_tick_scheduler_ticks_due(&scheduler, 10999999u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT32(1u, audio_tick_scheduler_ticks_due(&scheduler, 11000000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT64(12000000u, scheduler.next_deadline_ns);
}

TEST(test_audio_tick_scheduler_caps_catchup_and_resyncs_deadline) {
  AudioTickScheduler scheduler;
  uint32_t skipped = 0u;

  audio_tick_scheduler_init(&scheduler, 1000000u, 2u);
  audio_tick_scheduler_start(&scheduler, 0u);

  TEST_ASSERT_EQUAL_UINT32(2u, audio_tick_scheduler_ticks_due(&scheduler, 5500000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(3u, skipped);
  TEST_ASSERT_EQUAL_UINT64(6000000u, scheduler.next_deadline_ns);

  TEST_ASSERT_EQUAL_UINT32(0u, audio_tick_scheduler_ticks_due(&scheduler, 5999999u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT32(1u, audio_tick_scheduler_ticks_due(&scheduler, 6000000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
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

TEST(test_audio_tick_stays_running_while_voice_gate_active) {
  audio_engine_init(1);
  TEST_ASSERT_FALSE(g_audio_engine.tick_running);

  ID track_sym = (ID)intern_symbol_global(":gate-lifecycle-test");
  ID ba = make_test_trk1(60, 30, 1, 0); /* NOTE + END same tick, gate remains */
  TEST_ASSERT_TRUE(audio_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(audio_engine_play_music(track_sym, 1));

  audio_engine_tick(); /* parses NOTE + END; stream ends, voice gate still active */
  TEST_ASSERT_FALSE(g_audio_engine.music_stream.active);
  TEST_ASSERT_TRUE(g_audio_engine.voices[0].gate_remaining_ticks > 0);
  TEST_ASSERT_TRUE(g_audio_engine.tick_running);

  /* After gate fully decays, tick should auto-stop */
  for (int i = 0; i < 40; i++) {
    audio_engine_tick();
  }
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
