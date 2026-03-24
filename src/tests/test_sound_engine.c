/*
 * Sound Engine Tests
 *
 * Tests for trk1 format parsing, SPSC queue, track registry,
 * streaming engine, native API wiring, and ownership contracts.
 */

#include "tests_common.h"
#include "../sound_engine.h"
#include "../sound_tick_scheduler.h"
#include "../event_loop.h"
#include "byte_array.h"

static uint32_t g_test_backend_set_voice_call_count = 0u;
static uint32_t g_test_backend_set_voice_nonzero_count = 0u;

void tinyclj_sound_backend_observe_set_voice(int voice_index,
                                             uint16_t freq_hz,
                                             uint8_t volume,
                                             bool retrigger) {
  (void)voice_index;
  (void)retrigger;
  g_test_backend_set_voice_call_count++;
  if (freq_hz > 0u && volume > 0u) {
    g_test_backend_set_voice_nonzero_count++;
  }
}

static void reset_test_backend_set_voice_counters(void) {
  g_test_backend_set_voice_call_count = 0u;
  g_test_backend_set_voice_nonzero_count = 0u;
}

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

typedef struct {
  uint8_t event_type;
  uint8_t channel;
  bool has_delay;
  uint8_t midi_note;
  uint16_t freq_hz;
  uint32_t gate_ticks;
  uint8_t note_flags;
  uint8_t volume;
  uint32_t delay_ticks;
  uint8_t envelope_point_count;
  uint8_t envelope_levels[8];
} TestDecodedTrk1Event;

static bool decode_test_trk1_event(const uint8_t **cursor,
                                   const uint8_t *end,
                                   TestDecodedTrk1Event *out) {
  if (!cursor || !*cursor || !end || !out || *cursor >= end) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  uint8_t control = *(*cursor)++;
  out->has_delay = ((control >> 7) & 1u) != 0u;
  out->event_type = (control >> 4) & 0x07u;
  out->channel = control & 0x0Fu;

  switch (out->event_type) {
    case TRK1_EVT_NOTE:
      if (*cursor >= end) {
        return false;
      }
      out->midi_note = *(*cursor)++;
      if (!trk1_decode_varuint(cursor, end, &out->gate_ticks)) {
        return false;
      }
      break;
    case TRK1_EVT_NOTE_HZ:
      if ((*cursor + 2) > end) {
        return false;
      }
      out->freq_hz = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
      *cursor += 2;
      if (!trk1_decode_varuint(cursor, end, &out->gate_ticks)) {
        return false;
      }
      break;
    case TRK1_EVT_NOTE_EX:
      if (*cursor >= end) {
        return false;
      }
      out->midi_note = *(*cursor)++;
      if (!trk1_decode_varuint(cursor, end, &out->gate_ticks)) {
        return false;
      }
      if (*cursor >= end) {
        return false;
      }
      out->note_flags = *(*cursor)++;
      break;
    case TRK1_EVT_NOTE_HZ_EX:
      if ((*cursor + 2) > end) {
        return false;
      }
      out->freq_hz = (uint16_t)((*cursor)[0] | ((*cursor)[1] << 8));
      *cursor += 2;
      if (!trk1_decode_varuint(cursor, end, &out->gate_ticks)) {
        return false;
      }
      if (*cursor >= end) {
        return false;
      }
      out->note_flags = *(*cursor)++;
      break;
    case TRK1_EVT_SET_VOL:
      if (*cursor >= end) {
        return false;
      }
      out->volume = *(*cursor)++;
      break;
    case TRK1_EVT_SET_ENV: {
      if (*cursor >= end) {
        return false;
      }
      uint8_t point_count = *(*cursor)++;
      if (point_count == 0u || point_count > 8u || (*cursor + point_count) > end) {
        return false;
      }
      out->envelope_point_count = point_count;
      for (uint8_t i = 0; i < point_count; i++) {
        out->envelope_levels[i] = *(*cursor)++;
      }
      break;
    }
    case TRK1_EVT_END:
      break;
    default:
      return false;
  }

  if (out->has_delay) {
    if (!trk1_decode_varuint(cursor, end, &out->delay_ticks)) {
      return false;
    }
  }

  return true;
}

static bool decode_test_compiled_event(ID track_bytes,
                                       int event_index,
                                       TestDecodedTrk1Event *out_event) {
  TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(track_bytes));
  CljByteArray *arr = as_byte_array(track_bytes);
  TEST_ASSERT_NOT_NULL(arr);

  Trk1Header header;
  TEST_ASSERT_TRUE(trk1_parse_header(arr->data, arr->length, &header));

  const uint8_t *cursor = header.stream_start;
  const uint8_t *end = header.stream_start + header.stream_len_bytes;
  TestDecodedTrk1Event evt;
  for (int i = 0; i <= event_index; i++) {
    if (!decode_test_trk1_event(&cursor, end, &evt)) {
      return false;
    }
  }
  if (out_event) {
    *out_event = evt;
  }
  return true;
}

static ID make_test_trk1_two_note_ex_track(uint8_t note1,
                                           uint8_t flags1,
                                           uint32_t gate1_ticks,
                                           uint32_t delay1_ticks,
                                           uint8_t note2,
                                           uint8_t flags2,
                                           uint32_t gate2_ticks) {
  uint8_t gate1_buf[4];
  uint8_t delay1_buf[4];
  uint8_t gate2_buf[4];
  int gate1_len = encode_test_varuint(gate1_ticks, gate1_buf, (int)sizeof(gate1_buf));
  int delay1_len = encode_test_varuint(delay1_ticks, delay1_buf, (int)sizeof(delay1_buf));
  int gate2_len = encode_test_varuint(gate2_ticks, gate2_buf, (int)sizeof(gate2_buf));
  TEST_ASSERT_TRUE(gate1_len > 0);
  TEST_ASSERT_TRUE(delay1_len > 0);
  TEST_ASSERT_TRUE(gate2_len > 0);

  int stream_len = 1 + 1 + gate1_len + 1 + delay1_len +
                   1 + 1 + gate2_len + 1 +
                   1;
  int total_len = TRK1_HEADER_SIZE + stream_len;

  CljByteArray *ba = make_byte_array(total_len);
  uint8_t *d = ba->data;
  write_test_trk1_header(d, 1, 0, stream_len);

  int off = TRK1_HEADER_SIZE;
  d[off++] = (1u << 7) | (TRK1_EVT_NOTE_EX << 4);
  d[off++] = note1;
  for (int i = 0; i < gate1_len; i++) {
    d[off++] = gate1_buf[i];
  }
  d[off++] = flags1;
  for (int i = 0; i < delay1_len; i++) {
    d[off++] = delay1_buf[i];
  }

  d[off++] = (TRK1_EVT_NOTE_EX << 4);
  d[off++] = note2;
  for (int i = 0; i < gate2_len; i++) {
    d[off++] = gate2_buf[i];
  }
  d[off++] = flags2;

  d[off++] = (TRK1_EVT_END << 4);
  return (ID)ba;
}

static ID make_test_trk1_with_envelope_and_two_notes(const uint8_t *envelope_levels,
                                                     uint8_t envelope_point_count,
                                                     uint8_t note1,
                                                     uint32_t gate1_ticks,
                                                     uint32_t delay1_ticks,
                                                     uint8_t note2,
                                                     uint32_t gate2_ticks) {
  TEST_ASSERT_NOT_NULL(envelope_levels);
  TEST_ASSERT_TRUE(envelope_point_count > 0u);
  TEST_ASSERT_TRUE(envelope_point_count <= 8u);
  uint8_t gate1_buf[4];
  uint8_t delay1_buf[4];
  uint8_t gate2_buf[4];
  int gate1_len = encode_test_varuint(gate1_ticks, gate1_buf, (int)sizeof(gate1_buf));
  int delay1_len = encode_test_varuint(delay1_ticks, delay1_buf, (int)sizeof(delay1_buf));
  int gate2_len = encode_test_varuint(gate2_ticks, gate2_buf, (int)sizeof(gate2_buf));
  TEST_ASSERT_TRUE(gate1_len > 0);
  TEST_ASSERT_TRUE(delay1_len > 0);
  TEST_ASSERT_TRUE(gate2_len > 0);

  int stream_len = 1 + 1 + envelope_point_count +
                   1 + 1 + gate1_len + delay1_len +
                   1 + 1 + gate2_len +
                   1;
  int total_len = TRK1_HEADER_SIZE + stream_len;

  CljByteArray *ba = make_byte_array(total_len);
  uint8_t *d = ba->data;
  write_test_trk1_header(d, 1, 0, stream_len);

  int off = TRK1_HEADER_SIZE;
  d[off++] = (TRK1_EVT_SET_ENV << 4);
  d[off++] = envelope_point_count;
  for (uint8_t i = 0; i < envelope_point_count; i++) {
    d[off++] = envelope_levels[i];
  }

  d[off++] = (1u << 7) | (TRK1_EVT_NOTE << 4);
  d[off++] = note1;
  for (int i = 0; i < gate1_len; i++) {
    d[off++] = gate1_buf[i];
  }
  for (int i = 0; i < delay1_len; i++) {
    d[off++] = delay1_buf[i];
  }

  d[off++] = (TRK1_EVT_NOTE << 4);
  d[off++] = note2;
  for (int i = 0; i < gate2_len; i++) {
    d[off++] = gate2_buf[i];
  }

  d[off++] = (TRK1_EVT_END << 4);
  return (ID)ba;
}

static ID make_test_trk1_with_envelope_and_note(const uint8_t *envelope_levels,
                                                uint8_t envelope_point_count,
                                                uint8_t note,
                                                uint32_t gate_ticks) {
  TEST_ASSERT_NOT_NULL(envelope_levels);
  TEST_ASSERT_TRUE(envelope_point_count > 0u);
  TEST_ASSERT_TRUE(envelope_point_count <= 8u);

  uint8_t gate_buf[4];
  int gate_len = encode_test_varuint(gate_ticks, gate_buf, (int)sizeof(gate_buf));
  TEST_ASSERT_TRUE(gate_len > 0);

  int stream_len = 1 + 1 + envelope_point_count +
                   1 + 1 + gate_len +
                   1;
  int total_len = TRK1_HEADER_SIZE + stream_len;

  CljByteArray *ba = make_byte_array(total_len);
  uint8_t *d = ba->data;
  write_test_trk1_header(d, 1, 0, stream_len);

  int off = TRK1_HEADER_SIZE;
  d[off++] = (TRK1_EVT_SET_ENV << 4);
  d[off++] = envelope_point_count;
  for (uint8_t i = 0; i < envelope_point_count; i++) {
    d[off++] = envelope_levels[i];
  }

  d[off++] = (TRK1_EVT_NOTE << 4);
  d[off++] = note;
  for (int i = 0; i < gate_len; i++) {
    d[off++] = gate_buf[i];
  }

  d[off++] = (TRK1_EVT_END << 4);
  return (ID)ba;
}

/* ========================================================================= */
/* trk1 header parsing tests                                                 */
/* ========================================================================= */

TEST(test_sound_trk1_parse_header_valid) {
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

TEST(test_sound_trk1_parse_header_bad_magic) {
  uint8_t data[TRK1_HEADER_SIZE] = {0};
  data[0] = 'X'; /* wrong magic */
  Trk1Header h;
  TEST_ASSERT_FALSE(trk1_parse_header(data, TRK1_HEADER_SIZE, &h));
}

TEST(test_sound_trk1_parse_header_bad_version) {
  uint8_t data[TRK1_HEADER_SIZE] = {0};
  data[0] = 'T';
  data[1] = 'R';
  data[2] = 'K';
  data[3] = '1';
  data[4] = 99; /* wrong version */
  Trk1Header h;
  TEST_ASSERT_FALSE(trk1_parse_header(data, TRK1_HEADER_SIZE, &h));
}

TEST(test_sound_trk1_parse_header_zero_channels) {
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

TEST(test_sound_trk1_parse_header_too_short) {
  uint8_t data[10] = {0};
  Trk1Header h;
  TEST_ASSERT_FALSE(trk1_parse_header(data, 10, &h));
}

TEST(test_sound_trk1_parse_header_null) {
  Trk1Header h;
  TEST_ASSERT_FALSE(trk1_parse_header(NULL, 0, &h));
  TEST_ASSERT_FALSE(trk1_parse_header(NULL, 100, NULL));
}

/* ========================================================================= */
/* Varuint decoder tests                                                     */
/* ========================================================================= */

TEST(test_sound_varuint_single_byte) {
  uint8_t data[] = {42};
  const uint8_t *cursor = data;
  uint32_t val = 0;
  TEST_ASSERT_TRUE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
  TEST_ASSERT_EQUAL_UINT32(42, val);
  TEST_ASSERT_EQUAL_PTR(data + 1, cursor);
}

TEST(test_sound_varuint_multibyte) {
  /* 300 = 0b100101100 -> 0xAC 0x02 in base-128 */
  uint8_t data[] = {0xAC, 0x02};
  const uint8_t *cursor = data;
  uint32_t val = 0;
  TEST_ASSERT_TRUE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
  TEST_ASSERT_EQUAL_UINT32(300, val);
}

TEST(test_sound_varuint_max_value) {
  /* TRK1_VARUINT_MAX = 0x0FFFFFFF = 268435455 */
  uint8_t data[] = {0xFF, 0xFF, 0xFF, 0x7F}; /* 0x0FFFFFFF in varuint */
  const uint8_t *cursor = data;
  uint32_t val = 0;
  TEST_ASSERT_TRUE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
  TEST_ASSERT_EQUAL_UINT32(TRK1_VARUINT_MAX, val);
}

TEST(test_sound_varuint_overflow) {
  /* 5-byte varuint would overflow */
  uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
  const uint8_t *cursor = data;
  uint32_t val = 0;
  TEST_ASSERT_FALSE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
}

TEST(test_sound_varuint_truncated) {
  uint8_t data[] = {0x80}; /* continuation bit set, but no more data */
  const uint8_t *cursor = data;
  uint32_t val = 0;
  TEST_ASSERT_FALSE(trk1_decode_varuint(&cursor, data + sizeof(data), &val));
}

/* ========================================================================= */
/* SPSC command queue tests                                                  */
/* ========================================================================= */

static void init_sound_cmd_test_queue(SoundCmdQueue *q) {
  TEST_ASSERT_NOT_NULL(q);
  memset(q->slots, 0, sizeof(q->slots));
  TEST_ASSERT_TRUE(lockfree_spsc_queue_init(&q->spsc, q->slots, SOUND_CMD_QUEUE_CAP, sizeof(q->slots[0])));
}

TEST(test_sound_cmd_queue_push_pop) {
  SoundCmdQueue q;
  init_sound_cmd_test_queue(&q);

  SoundCmd cmd = {.type = SOUND_CMD_PLAY_TRACK, .track_id = NULL, .int_param = 1};
  TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));

  SoundCmd out;
  TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q.spsc, &out));
  TEST_ASSERT_EQUAL_INT(SOUND_CMD_PLAY_TRACK, out.type);
  TEST_ASSERT_EQUAL_INT(1, out.int_param);
}

TEST(test_sound_cmd_queue_empty) {
  SoundCmdQueue q;
  init_sound_cmd_test_queue(&q);

  TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q.spsc));

  SoundCmd cmd = {.type = SOUND_CMD_STOP_ALL};
  TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));
  TEST_ASSERT_FALSE(lockfree_spsc_queue_empty(&q.spsc));

  SoundCmd out;
  TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q.spsc, &out));
  TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q.spsc));
}

TEST(test_sound_cmd_queue_full) {
  SoundCmdQueue q;
  init_sound_cmd_test_queue(&q);

  for (int i = 0; i < SOUND_CMD_QUEUE_CAP; i++) {
    SoundCmd cmd = {.type = SOUND_CMD_STOP_ALL};
    TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));
  }

  /* Queue full: next push should fail */
  SoundCmd cmd = {.type = SOUND_CMD_STOP_ALL};
  TEST_ASSERT_FALSE(lockfree_spsc_queue_push(&q.spsc, &cmd));
}

TEST(test_sound_cmd_queue_wraparound) {
  SoundCmdQueue q;
  init_sound_cmd_test_queue(&q);

  /* Fill and drain multiple times to test wraparound */
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < SOUND_CMD_QUEUE_CAP; i++) {
      SoundCmd cmd = {.type = SOUND_CMD_PLAY_TRACK, .int_param = round * 100 + i};
      TEST_ASSERT_TRUE(lockfree_spsc_queue_push(&q.spsc, &cmd));
    }
    for (int i = 0; i < SOUND_CMD_QUEUE_CAP; i++) {
      SoundCmd out;
      TEST_ASSERT_TRUE(lockfree_spsc_queue_pop(&q.spsc, &out));
      TEST_ASSERT_EQUAL_INT(round * 100 + i, out.int_param);
    }
    TEST_ASSERT_TRUE(lockfree_spsc_queue_empty(&q.spsc));
  }
}

/* ========================================================================= */
/* Track registry + ownership tests                                          */
/* ========================================================================= */

TEST(test_sound_load_unload_contract) {
  sound_engine_init(2);

  ID track_sym = (ID)intern_symbol_global(":test-track");
  ID ba = make_test_trk1(69, 100, 2, 0);

  /* Load: retain count should increase */
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_EQUAL_INT(1, g_sound_engine.track_count);

  /* Unload: should release */
  TEST_ASSERT_TRUE(sound_engine_unload_track(track_sym));
  TEST_ASSERT_EQUAL_INT(0, g_sound_engine.track_count);

  /* Unload non-existent: false */
  TEST_ASSERT_FALSE(sound_engine_unload_track(track_sym));

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_load_invalid_data) {
  sound_engine_init(2);

  ID track_sym = (ID)intern_symbol_global(":bad-track");

  /* Non-byte-array should fail */
  TEST_ASSERT_FALSE(sound_engine_load_track(track_sym, fixnum(42)));

  /* Short data should fail */
  CljByteArray *short_ba = make_byte_array(5);
  TEST_ASSERT_FALSE(sound_engine_load_track(track_sym, (ID)short_ba));
  RELEASE(short_ba);

  /* NULL args should fail */
  TEST_ASSERT_FALSE(sound_engine_load_track(NULL, NULL));

  sound_engine_shutdown();
}

TEST(test_sound_load_replaces_existing) {
  sound_engine_init(2);

  ID track_sym = (ID)intern_symbol_global(":replace-track");
  ID ba1 = make_test_trk1(60, 100, 1, 0);
  ID ba2 = make_test_trk1(72, 200, 1, 0);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba1));
  TEST_ASSERT_EQUAL_INT(1, g_sound_engine.track_count);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba2));
  TEST_ASSERT_EQUAL_INT(1, g_sound_engine.track_count);

  RELEASE(ba1);
  RELEASE(ba2);
  sound_engine_shutdown();
}

/* ========================================================================= */
/* Streaming + tick tests                                                    */
/* ========================================================================= */

TEST(test_sound_play_and_tick_basic) {
  sound_engine_init(2);

  ID track_sym = (ID)intern_symbol_global(":tick-test");
  ID ba = make_test_trk1(69, 5, 1, 0); /* A4, gate=5 ticks */

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  /* First tick: drains command, parses NOTE + END, sets voice.
   * NOTE has no delay, so END is parsed in the same tick.
   * Stream ends (repeat=1), but voice was set. */
  sound_engine_tick();
  /* Stream may already be inactive (NOTE+END parsed in same tick),
   * but the voice should have been set by the NOTE. */
  TEST_ASSERT_EQUAL_UINT16(440, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].gate_remaining_ticks > 0);

  /* Tick through gate */
  for (int i = 0; i < 6; i++) {
    sound_engine_tick();
  }

  /* After gate expires: voice should be silent */
  TEST_ASSERT_EQUAL_UINT16(0, g_sound_engine.voices[0].freq_hz);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_play_with_delay) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":delay-test");
  ID ba = make_test_trk1_with_delay(69, 3, 5, 1); /* note=A4, gate=3, delay=5 before END */

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick(); /* drains play command, parses NOTE (delay=5 before next event) */
  TEST_ASSERT_TRUE(g_sound_engine.music_stream.active);

  /* Stream should still be active during delay period */
  for (int i = 0; i < 4; i++) {
    sound_engine_tick();
    TEST_ASSERT_TRUE(g_sound_engine.music_stream.active);
  }

  /* After delay: END is parsed -> stream ends (repeat=1 = play once) */
  sound_engine_tick(); /* tick 5: current_tick reaches next_event_tick, parses END */
  /* May need one more tick for END processing */
  sound_engine_tick();

  TEST_ASSERT_FALSE(g_sound_engine.music_stream.active);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_legato_hold_preserves_voice_until_next_note) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":legato-runtime-test");
  ID ba = make_test_trk1_two_note_ex_track(60, TRK1_NOTE_FLAG_LEGATO, 3, 3, 62, 0, 3);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick();
  sound_engine_tick();
  sound_engine_tick();

  TEST_ASSERT_EQUAL_UINT16(262, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_EQUAL_UINT16(262, g_sound_engine.voices[0].applied_freq_hz);
  TEST_ASSERT_EQUAL_UINT32(0, g_sound_engine.voices[0].gate_remaining_ticks);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].hold_until_next_note);

  sound_engine_tick();

  TEST_ASSERT_EQUAL_UINT16(294, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_EQUAL_UINT16(294, g_sound_engine.voices[0].applied_freq_hz);
  TEST_ASSERT_FALSE(g_sound_engine.voices[0].hold_until_next_note);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_same_note_without_retrigger_keeps_attack_generation) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":repeat-no-retrigger-test");
  ID ba = make_test_trk1_two_note_ex_track(69, TRK1_NOTE_FLAG_LEGATO, 3, 3, 69, 0, 3);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick();
  sound_engine_tick();
  sound_engine_tick();
  sound_engine_tick();

  TEST_ASSERT_EQUAL_UINT16(440, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_EQUAL_UINT32(0, g_sound_engine.voices[0].attack_generation);
  TEST_ASSERT_EQUAL_UINT32(0, g_sound_engine.voices[0].applied_attack_generation);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_same_note_with_retrigger_advances_attack_generation) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":repeat-retrigger-test");
  ID ba = make_test_trk1_two_note_ex_track(69, TRK1_NOTE_FLAG_LEGATO, 3, 3, 69,
                                           TRK1_NOTE_FLAG_RETRIGGER, 3);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick();
  sound_engine_tick();
  sound_engine_tick();
  sound_engine_tick();

  TEST_ASSERT_EQUAL_UINT16(440, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_EQUAL_UINT32(1, g_sound_engine.voices[0].attack_generation);
  TEST_ASSERT_EQUAL_UINT32(1, g_sound_engine.voices[0].applied_attack_generation);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_track_envelope_reduces_voice_volume_in_tail_segment) {
  sound_engine_init(1);

  static const uint8_t envelope[] = {255u, 255u, 255u, 255u, 26u};
  ID track_sym = (ID)intern_symbol_global(":envelope-tail-test");
  ID ba = make_test_trk1_with_envelope_and_two_notes(envelope, 5u, 69, 10, 20, 72, 10);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick();
  TEST_ASSERT_EQUAL_UINT8(255u, g_sound_engine.voices[0].volume);

  for (int i = 0; i < 6; i++) {
    sound_engine_tick();
  }
  TEST_ASSERT_EQUAL_UINT8(255u, g_sound_engine.voices[0].volume);

  sound_engine_tick();
  TEST_ASSERT_EQUAL_UINT8(26u, g_sound_engine.voices[0].volume);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].gate_remaining_ticks > 0u);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_track_envelope_resets_repeated_note_volume_without_retrigger_flag) {
  sound_engine_init(1);

  static const uint8_t envelope[] = {255u, 255u, 255u, 255u, 26u};
  ID track_sym = (ID)intern_symbol_global(":envelope-repeat-test");
  ID ba = make_test_trk1_with_envelope_and_two_notes(envelope, 5u, 69, 10, 10, 69, 10);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick();
  for (int i = 0; i < 7; i++) {
    sound_engine_tick();
  }
  TEST_ASSERT_EQUAL_UINT8(26u, g_sound_engine.voices[0].volume);
  TEST_ASSERT_EQUAL_UINT32(0u, g_sound_engine.voices[0].attack_generation);

  sound_engine_tick();
  sound_engine_tick();
  sound_engine_tick();
  TEST_ASSERT_EQUAL_UINT16(440u, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_EQUAL_UINT8(255u, g_sound_engine.voices[0].volume);
  TEST_ASSERT_EQUAL_UINT32(0u, g_sound_engine.voices[0].attack_generation);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_track_envelope_emits_only_two_nonzero_backend_updates_per_note) {
  sound_engine_init(1);
  reset_test_backend_set_voice_counters();

  static const uint8_t envelope[] = {255u, 255u, 255u, 255u, 26u};
  ID track_sym = (ID)intern_symbol_global(":envelope-update-count-test");
  ID ba = make_test_trk1_with_envelope_and_note(envelope, 5u, 69, 10);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  for (int i = 0; i < 12; i++) {
    sound_engine_tick();
  }

  if (g_test_backend_set_voice_nonzero_count != 2u ||
      g_test_backend_set_voice_call_count != 3u) {
    char msg[128];
    snprintf(msg,
             sizeof(msg),
             "backend updates total=%u nonzero=%u",
             (unsigned int)g_test_backend_set_voice_call_count,
             (unsigned int)g_test_backend_set_voice_nonzero_count);
    TEST_FAIL_MESSAGE(msg);
  }

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_repeat_infinite) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":repeat-test");
  ID ba = make_test_trk1(60, 2, 1, 0); /* C4, gate=2 */

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 0)); /* 0 = infinite */

  sound_engine_tick(); /* drains command, parses NOTE + END -> loop rewind */
  TEST_ASSERT_TRUE(g_sound_engine.music_stream.active);

  /* Tick many times: should stay active (infinite loop) */
  for (int i = 0; i < 50; i++) {
    sound_engine_tick();
  }
  TEST_ASSERT_TRUE(g_sound_engine.music_stream.active);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_stop_track_no_finished_callback) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":stop-test");
  ID ba = make_test_trk1(60, 100, 1, 0);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 0));

  sound_engine_tick(); /* start playing */
  TEST_ASSERT_TRUE(g_sound_engine.music_stream.active);

  TEST_ASSERT_TRUE(sound_engine_stop_track(track_sym));
  sound_engine_tick(); /* drain stop command */
  TEST_ASSERT_FALSE(g_sound_engine.music_stream.active);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_set_track_volume_during_playback) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":vol-test");
  ID ba = make_test_trk1(69, 100, 1, 0);

  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 0));

  sound_engine_tick(); /* start */
  TEST_ASSERT_EQUAL_UINT8(255, g_sound_engine.music_stream.track_volume);

  TEST_ASSERT_TRUE(sound_engine_set_track_volume(track_sym, 128));
  sound_engine_tick(); /* drain volume command */
  TEST_ASSERT_EQUAL_UINT8(128, g_sound_engine.music_stream.track_volume);

  RELEASE(ba);
  sound_engine_shutdown();
}

/* ========================================================================= */
/* SFX one-shot tests                                                        */
/* ========================================================================= */

TEST(test_sound_sfx_oneshot) {
  sound_engine_init(2);

  ID sfx_sym = (ID)intern_symbol_global(":sfx-laser");
  /* Use delay so SFX lasts multiple ticks: NOTE(gate=10) + delay=5 + END */
  ID ba = make_test_trk1_with_delay(80, 10, 5, 1);

  TEST_ASSERT_TRUE(sound_engine_load_track(sfx_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_sfx(sfx_sym));

  sound_engine_tick(); /* drains SFX command, parses NOTE (delay=5 before END) */

  bool sfx_active = false;
  for (int i = 0; i < SOUND_MAX_SFX; i++) {
    if (g_sound_engine.sfx[i].stream.active)
      sfx_active = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(sfx_active, "SFX should be active after first tick");

  /* Tick through: SFX should eventually end */
  for (int i = 0; i < 20; i++) {
    sound_engine_tick();
  }

  sfx_active = false;
  for (int i = 0; i < SOUND_MAX_SFX; i++) {
    if (g_sound_engine.sfx[i].stream.active)
      sfx_active = true;
  }
  TEST_ASSERT_FALSE_MESSAGE(sfx_active, "SFX should have ended after enough ticks");

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_sfx_last_start_wins_when_all_slots_busy) {
  sound_engine_init(2);

  ID sfx_sym = (ID)intern_symbol_global(":sfx-drop");
  ID ba = make_test_trk1_with_delay(80, 100, 30, 1);

  TEST_ASSERT_TRUE(sound_engine_load_track(sfx_sym, ba));
  g_sound_engine.telemetry.sfx_drop_count = 0;

  TEST_ASSERT_TRUE(sound_engine_play_sfx(sfx_sym));
  sound_engine_tick();
  TEST_ASSERT_TRUE(sound_engine_play_sfx(sfx_sym));
  sound_engine_tick();

  TEST_ASSERT_TRUE(sound_engine_play_sfx(sfx_sym));
  TEST_ASSERT_EQUAL_UINT32(0, g_sound_engine.telemetry.sfx_drop_count);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_finished_callback_runs_via_event_loop) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  sound_engine_init(1);
  event_loop_clear();

  ID track_sym = (ID)intern_symbol_global(":finish-test");
  ID ba = make_test_trk1(69, 2, 1, 0);
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));

  ID atom_def = eval_string("(def sound-finished-track (atom nil))", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(atom_def);
  ID cb_def = eval_string(
      "(def sound-finished-cb "
      "  (fn [event] "
      "    (reset! sound-finished-track [(:source event) (:kind event) (:track-id event)]) "
      "    nil))",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(cb_def);
  ID cb_fn = eval_string("sound-finished-cb", g_test_eval_state);
  TEST_ASSERT_NOT_NULL(cb_fn);

  sound_engine_on_finished(cb_fn);
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick();
  TEST_ASSERT_FALSE_MESSAGE(event_loop_ingress_has_pending(),
                            "sound tick should not enqueue finished callbacks directly into ingress");
  TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
  TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

  ID done = eval_string("(= @sound-finished-track [:audio :finished :finish-test])", g_test_eval_state);
  TEST_ASSERT_EQUAL(clj_true, done);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_native_on_finished_callback_receives_event_map_shape) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  sound_engine_init(1);
  event_loop_clear();

  ID track_sym = (ID)intern_symbol_global(":finish-shape-test");
  ID ba = make_test_trk1(72, 2, 1, 0);
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));

  ID setup = eval_string(
      "(do "
      "  (def sound-finished-shape (atom nil)) "
      "  (require 'tiny-fx.sound-native) "
      "  (tiny-fx.sound-native/sound-on-finished! "
      "    (fn [event] "
      "      (reset! sound-finished-shape "
      "              [(map? event) (:source event) (:kind event) (:track-id event)]) "
      "      nil)) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(setup);

  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));
  sound_engine_tick();
  TEST_ASSERT_TRUE(event_loop_has_pending_tasks());
  TEST_ASSERT_TRUE(event_loop_run_next(NULL, g_test_eval_state));

  ID done = eval_string("(= @sound-finished-shape [true :audio :finished :finish-shape-test])",
                        g_test_eval_state);
  TEST_ASSERT_EQUAL(clj_true, done);

  RELEASE(ba);
  sound_engine_shutdown();
}

/* ========================================================================= */
/* Native API wiring tests (via eval_string)                                 */
/* ========================================================================= */

TEST(test_sound_native_lookup) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  /* High-level sound namespace should stay minimal. */
  const char *names[] = {
      "tiny-fx.sound/note->midi",
      "tiny-fx.sound/track-duration-ms",
      "tiny-fx.sound/compile-track",
      "tiny-fx.sound/play-steps!",
      "tiny-fx.sound/play-sfx!",
  };

  for (int i = 0; i < 5; i++) {
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

TEST(test_sound_native_low_level_lookup) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  const char *names[] = {
      "tiny-fx.sound-native/sound-load-track!",
      "tiny-fx.sound-native/sound-unload-track!",
      "tiny-fx.sound-native/sound-play-music!",
      "tiny-fx.sound-native/sound-stop-track!",
      "tiny-fx.sound-native/sound-stop-music!",
      "tiny-fx.sound-native/sound-play-sfx!",
      "tiny-fx.sound-native/sound-stop-all!",
      "tiny-fx.sound-native/sound-set-track-volume!",
      "tiny-fx.sound-native/sound-set-music-volume!",
      "tiny-fx.sound-native/sound-on-finished!",
  };

  for (int i = 0; i < 10; i++) {
    char buf[192];
    test_snprintf(buf, sizeof(buf), "(do (require 'tiny-fx.sound-native) (fn? %s))", names[i]);
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

TEST(test_sound_debug_lookup) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  const char *names[] = {
      "tiny-fx.sound-debug/play-test-tone!",
      "tiny-fx.sound-debug/play-test-ramp!",
      "tiny-fx.sound-debug/play-test-ramp-noise!",
      "tiny-fx.sound-debug/host-status!",
      "tiny-fx.sound-debug/play-portamento-reference!",
      "tiny-fx.sound-debug/play-rocket-thruster-reference!",
      "tiny-fx.sound-debug/play-thrust-demo!",
      "tiny-fx.sound-debug/play-piu-demo!",
  };

  for (int i = 0; i < 8; i++) {
    char buf[192];
    test_snprintf(buf, sizeof(buf), "(do (require 'tiny-fx.sound-debug) (fn? %s))", names[i]);
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

TEST(test_sound_demos_lookup) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos) "
        "    (let [minuet (tiny-fx.sound-demos/load-song :minuet-in-g) "
        "          entertainer (tiny-fx.sound-demos/load-song :the-entertainer) "
        "          gymnopedie (tiny-fx.sound-demos/load-song :gymnopedie-no-1) "
        "          rondo (tiny-fx.sound-demos/load-song :rondo-alla-turca) "
        "          mountain (tiny-fx.sound-demos/load-song :hall-of-the-mountain-king) "
        "          cancan (tiny-fx.sound-demos/load-song :can-can)] "
        "      [(count (:steps minuet)) "
        "       (count (:steps entertainer)) "
        "       (count (:steps gymnopedie)) "
        "       (count (:steps rondo)) "
        "       (count (:steps mountain)) "
        "       (count (:steps cancan))]))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos demo builders should resolve and return step vectors");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
  CljPersistentVector *v = as_vector(result);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT(6, vector_count(v));
  for (uint32_t i = 0; i < 6; i++) {
    ID item = vector_nth(v, i);
    TEST_ASSERT_TRUE(is_fixnum(item));
  }
}

TEST(test_sound_demo_song_can_be_played_directly_via_play) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do "
        "  (require 'tiny-fx.sound) "
        "  (require 'tiny-fx.sound-demos) "
        "  (let [ret (tiny-fx.sound/play! (tiny-fx.sound-demos/load-song :rocket-launch-sfx))] "
        "    (and (map? ret) (contains? ret :status) (contains? ret :duration-ms))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play! should accept song descriptor from tiny-fx.sound-demos/load-song");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_high_level_namespace_excludes_debug_and_native_helpers) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = eval_string(
      "(do "
      "  (require 'tiny-fx.sound) "
      "  [(try (do tiny-fx.sound/sound-load-track! false) (catch Exception e true)) "
      "   (try (do tiny-fx.sound/sound-on-finished! false) (catch Exception e true)) "
      "   (try (do tiny-fx.sound/play-test-tone! false) (catch Exception e true)) "
      "   (try (do tiny-fx.sound/host-status! false) (catch Exception e true))])",
      g_test_eval_state);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
  CljPersistentVector *v = as_vector(result);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT(4, vector_count(v));
  for (uint32_t i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_PTR(clj_true, vector_nth(v, i));
  }
}

TEST(test_sound_native_play_music_initializes_engine_if_needed) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  sound_engine_shutdown();
  TEST_ASSERT_EQUAL_INT(0, g_sound_engine.voice_count);

  ID result = eval_string(
        "(do "
        "  (require 'tiny-fx.sound) "
        "  (let [ret (tiny-fx.sound/play-steps! :lazy-init "
        "              [{:notes [:G5 :D5] :duration :s}] "
        "              {:channel-count 2 :volumes [0 0] :tempo-bpm 120})] "
        "    (and (= :playing (:status ret)) "
        "         (= 125 (:duration-ms ret)))))",
        g_test_eval_state);

  TEST_ASSERT_TRUE(result == clj_true);
  TEST_ASSERT_TRUE(g_sound_engine.voice_count > 0);

  sound_engine_shutdown();
}

TEST(test_sound_native_play_test_tone_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string("(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-tone! 440 100 0))",
                         g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-test-tone! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_native_play_test_tone_with_volume_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-tone! 523 120 0))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-test-tone! with volume should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_native_play_test_noise_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-noise! 180 320 80 2 0))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-test-noise! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_native_play_test_ramp_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-ramp! 220 320 120 0))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-test-ramp! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_native_play_test_ramp_noise_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-ramp-noise! 180 320 80 3 0))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-test-ramp-noise! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_debug_portamento_reference_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-ramp! 220 330 1800 0))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-portamento-reference! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_debug_rocket_thruster_reference_returns_bool) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-test-ramp-noise! 180 360 2200 35 0))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-rocket-thruster-reference! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true || result == clj_false);
}

TEST(test_sound_debug_thrust_demo_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/play-thrust-demo!))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("play-thrust-demo! should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));
  ID duration = map_get(result, intern_symbol_global(":duration-ms"));
  TEST_ASSERT_TRUE(is_fixnum(duration));
  TEST_ASSERT_EQUAL_INT(2800, as_fixnum(duration));
}

TEST(test_sound_native_host_status_returns_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string("(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/host-status!))",
                         g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("host-status! should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_TRUE(map_get(result, intern_symbol_global(":backend-available")) != NOT_FOUND);
  TEST_ASSERT_TRUE(map_get(result, intern_symbol_global(":sound-running")) != NOT_FOUND);
  TEST_ASSERT_TRUE(map_get(result, intern_symbol_global(":voice-count")) != NOT_FOUND);
  TEST_ASSERT_TRUE(map_get(result, intern_symbol_global(":debug-noise-active")) != NOT_FOUND);
  TEST_ASSERT_TRUE(map_get(result, intern_symbol_global(":debug-ramp-active")) != NOT_FOUND);
  TEST_ASSERT_TRUE(map_get(result, intern_symbol_global(":debug-ramp-noise-active")) != NOT_FOUND);
}

TEST(test_sound_host_init_failure_throws_and_allows_retry) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  const char *saved = getenv("TINYCLJ_SOUND_HOST_INIT_FAIL");
  char saved_buf[64] = {0};
  if (saved && saved[0] != '\0') {
    test_snprintf(saved_buf, sizeof(saved_buf), "%s", saved);
  }

  sound_engine_shutdown();
  setenv("TINYCLJ_SOUND_HOST_INIT_FAIL", "unit-initialize", 1);

  bool threw = false;
  ID result = NULL;
  TRY {
    result = eval_string("(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/host-status!))",
                         g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
    TEST_ASSERT_NOT_NULL(ex);
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
  TEST_ASSERT_NIL(result);
  TEST_ASSERT_EQUAL_INT(0, g_sound_engine.voice_count);

  if (saved_buf[0] != '\0') {
    setenv("TINYCLJ_SOUND_HOST_INIT_FAIL", saved_buf, 1);
  } else {
    unsetenv("TINYCLJ_SOUND_HOST_INIT_FAIL");
  }

  sound_engine_shutdown();
  result = NULL;
  TRY {
    result = eval_string("(do (require 'tiny-fx.sound-debug) (tiny-fx.sound-debug/host-status!))",
                         g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("host-status! should recover after forced host init failure is removed");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
}

TEST(test_sound_tiny_fx_sound_namespace_compile_and_play) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do "
        "  (require 'tiny-fx.sound) "
        "  (let [ret (tiny-fx.sound/play-steps! :dsl-test "
        "              [{:notes [:G5 :D5] :duration :s} {:notes [:Bb5 :F5] :duration :e}] "
        "              {:channel-count 2 :volumes [0 0] :tempo-bpm 120})] "
        "    (and (= :playing (:status ret)) "
        "         (= 375 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound namespace should compile and play steps");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_play_sfx_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do "
        "  (require 'tiny-fx.sound) "
        "  (let [ret (tiny-fx.sound/play-sfx! :sfx-test "
        "              [{:notes [:G6] :duration :s}] "
        "              {:channel-count 1 :volumes [0] :tempo-bpm 120})] "
        "    (and (= :playing (:status ret)) "
        "         (= 125 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-sfx! should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_game_demo_play_starwars_title_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "    (tiny-fx.gfx/start-renderer!) "
        "    (require 'tiny-fx.game-demo) "
        "    (tiny-fx.game-demo/play-starwars-title!))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.game-demo/play-starwars-title! should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));
  ID duration = map_get(result, intern_symbol_global(":duration-ms"));
  TEST_ASSERT_TRUE(is_fixnum(duration));
  TEST_ASSERT_EQUAL_INT(8925, as_fixnum(duration));
}

TEST(test_sound_tiny_fx_sound_demos_play_minuet_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.gfx) "
        "    (tiny-fx.gfx/start-renderer!) "
        "    (require 'tiny-fx.sound-demos) "
        "    (tiny-fx.sound-demos/play-demo! :minuet-in-g))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos/play-demo! :minuet-in-g should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));
  ID duration = map_get(result, intern_symbol_global(":duration-ms"));
  TEST_ASSERT_TRUE(is_fixnum(duration));
  TEST_ASSERT_EQUAL_INT(6848, as_fixnum(duration));
}

TEST(test_sound_tiny_fx_sound_demos_minuet_activates_two_voices) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos) "
        "    (tiny-fx.sound-demos/play-demo! :minuet-in-g))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos/play-demo! :minuet-in-g two-voice activation should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));
  sound_engine_tick();
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz != g_sound_engine.voices[1].freq_hz);
  TEST_ASSERT_FALSE(g_sound_engine.voices[2].active);
  TEST_ASSERT_FALSE(g_sound_engine.voices[3].active);
}

TEST(test_sound_tiny_fx_sound_demos_play_rocket_launch_sfx_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos) "
        "    (tiny-fx.sound-demos/play-demo! :rocket-launch-sfx))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos/play-demo! :rocket-launch-sfx should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));
  ID duration = map_get(result, intern_symbol_global(":duration-ms"));
  TEST_ASSERT_TRUE(is_fixnum(duration));
  TEST_ASSERT_EQUAL_INT(2280, as_fixnum(duration));
}

TEST(test_sound_tiny_fx_sound_demos_rocket_launch_sfx_changes_frequency_over_time) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos) "
        "    (tiny-fx.sound-demos/play-demo! :rocket-launch-sfx))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos/play-demo! :rocket-launch-sfx motion test should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));

  sound_engine_tick();
  uint16_t initial_freq = g_sound_engine.voices[0].freq_hz;
  for (int i = 0; i < 60; i++) {
    sound_engine_tick();
  }
  uint16_t later_freq = g_sound_engine.voices[0].freq_hz;

  TEST_ASSERT_TRUE(initial_freq > 0);
  TEST_ASSERT_TRUE(later_freq > 0);
  TEST_ASSERT_TRUE(later_freq != initial_freq);
}

TEST(test_sound_tiny_fx_sound_demos_phase_two_batch_returns_status_maps) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos) "
        "    [(tiny-fx.sound-demos/play-demo! :the-entertainer) "
        "     (tiny-fx.sound-demos/play-demo! :gymnopedie-no-1)])",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos phase two demos should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
  CljPersistentVector *v = as_vector(result);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT(2, vector_count(v));
  ID entertainer = vector_nth(v, 0);
  ID gymnopedie = vector_nth(v, 1);
  TEST_ASSERT_TRUE(is_map(entertainer));
  TEST_ASSERT_TRUE(is_map(gymnopedie));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(entertainer, intern_symbol_global(":status")));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(gymnopedie, intern_symbol_global(":status")));
  TEST_ASSERT_EQUAL_INT(7832, as_fixnum(map_get(entertainer, intern_symbol_global(":duration-ms"))));
  TEST_ASSERT_EQUAL_INT(16569, as_fixnum(map_get(gymnopedie, intern_symbol_global(":duration-ms"))));
}

TEST(test_sound_tiny_fx_sound_demos_play_william_tell_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos-william) "
        "    (tiny-fx.sound-demos-william/play-william-tell-finale!))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos-william/play-william-tell-finale! should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(is_map(result));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(result, intern_symbol_global(":status")));
  TEST_ASSERT_EQUAL_INT(93900, as_fixnum(map_get(result, intern_symbol_global(":duration-ms"))));
}

TEST(test_sound_tiny_fx_sound_demos_phase_three_batch_returns_status_maps) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound-demos) "
        "    [(tiny-fx.sound-demos/play-demo! :rondo-alla-turca) "
        "     (tiny-fx.sound-demos/play-demo! :hall-of-the-mountain-king) "
        "     (tiny-fx.sound-demos/play-demo! :can-can)])",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound-demos phase three demos should not throw");
  }
  END_TRY

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(TAG(result) == CLJ_VECTOR_PERSISTENT);
  CljPersistentVector *v = as_vector(result);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT(3, vector_count(v));
  ID rondo = vector_nth(v, 0);
  ID mountain = vector_nth(v, 1);
  ID cancan = vector_nth(v, 2);
  TEST_ASSERT_TRUE(is_map(rondo));
  TEST_ASSERT_TRUE(is_map(mountain));
  TEST_ASSERT_TRUE(is_map(cancan));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(rondo, intern_symbol_global(":status")));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(mountain, intern_symbol_global(":status")));
  TEST_ASSERT_EQUAL_PTR(intern_symbol_global(":playing"),
                        map_get(cancan, intern_symbol_global(":status")));
  TEST_ASSERT_EQUAL_INT(4500, as_fixnum(map_get(rondo, intern_symbol_global(":duration-ms"))));
  TEST_ASSERT_EQUAL_INT(15500, as_fixnum(map_get(mountain, intern_symbol_global(":duration-ms"))));
  TEST_ASSERT_EQUAL_INT(4000, as_fixnum(map_get(cancan, intern_symbol_global(":duration-ms"))));
}

TEST(test_sound_tiny_fx_sound_play_steps_one_voice_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :one-voice "
        "                [{:notes [:G5] :duration :s} {:notes [:D5] :duration :s}] "
        "                {:channel-count 1 :volumes [0] :tempo-bpm 120})] "
        "      (and (= :playing (:status ret)) "
        "           (= 250 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! one-voice should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_play_steps_musical_durations_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :musical-dur "
        "                [{:notes [:G5 :D5] :duration :q} "
        "                 {:notes [:Bb5 :F5] :duration :dq} "
        "                 {:notes [:A5 :E5] :duration :et}] "
        "                {:channel-count 2 :volumes [0 0] :tempo-bpm 104})] "
        "      (and (= :playing (:status ret)) "
        "           (= 1632 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with musical durations should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_play_steps_rest_shorthand_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :rest-shorthand "
        "                [{:notes [:G5 :D5] :duration :q} "
        "                 {:rest :e} "
        "                 {:notes [:Bb5 :F5] :duration :q}] "
        "                {:channel-count 2 :volumes [0 0] :tempo-bpm 104})] "
        "      (and (= :playing (:status ret)) "
        "           (= 1440 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with rest shorthand should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_numeric_duration_ms_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :numeric-dur "
        "      [{:notes [:G5 :D5] :duration 120}] "
        "      {:channel-count 2 :volumes [0 0]})] "
        "      (and (= :playing (:status ret)) "
        "           (= 120 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with integer duration should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_keyword_duration_without_tempo_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :missing-tempo "
        "      [{:notes [:G5] :duration :q}] "
        "      {:channel-count 1 :volumes [0]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_integer_duration_without_tempo_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-sfx! :tempo-free-sfx "
        "      [{:notes [5200] :bend [1200] :duration 160} "
        "       {:notes [550] :duration 56}] "
        "      {:channel-count 1 :volumes [0]})] "
        "      (and (= :playing (:status ret)) "
        "           (= 216 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-sfx! with integer durations should not require tempo");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_rest_with_dur_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :bad-rest "
        "      [{:notes [:G5 :D5] :duration :q} "
        "       {:rest :e :duration :e}] "
        "      {:channel-count 2 :volumes [0 0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_track_duration_ms_accepts_integer_rest) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= 210 "
        "       (tiny-fx.sound/track-duration-ms "
        "         [{:notes [:G5] :duration 120} "
        "          {:rest 90}] "
        "         {:channel-count 1 :volumes [0]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/track-duration-ms should accept integer duration and rest");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_invalid_articulation_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [:C4] :duration :q :articulation :accent}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_non_boolean_rearticulate_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [:C4] :duration :q :rearticulate :yes}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_track_duration_ms_is_invariant_under_articulation_metadata) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= 750 "
        "       (tiny-fx.sound/track-duration-ms "
        "         [{:notes [:C4] :duration :q :articulation :legato} "
        "          {:notes [:C4] :duration :e :rearticulate true}] "
        "         {:channel-count 1 :volumes [0] :tempo-bpm 120})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/track-duration-ms should ignore articulation metadata");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_compile_track_emits_legato_flag) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [:C4] :duration :q :articulation :legato} "
        "       {:notes [:D4] :duration :q}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/compile-track should accept :articulation :legato");
  }
  END_TRY

  TestDecodedTrk1Event evt0;
  TestDecodedTrk1Event evt1;
  TEST_ASSERT_TRUE(decode_test_compiled_event(result, 1, &evt0));
  TEST_ASSERT_TRUE(decode_test_compiled_event(result, 2, &evt1));
  TEST_ASSERT_EQUAL_UINT8(TRK1_EVT_SET_VOL, evt0.event_type);
  TEST_ASSERT_EQUAL_UINT8(TRK1_EVT_NOTE_EX, evt1.event_type);
  TEST_ASSERT_BITS_HIGH(TRK1_NOTE_FLAG_LEGATO, evt1.note_flags);
}

TEST(test_sound_tiny_fx_sound_compile_track_emits_retrigger_flag_for_same_follow_tone) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [:A4] :duration :q :articulation :legato} "
        "       {:notes [:A4] :duration :q :rearticulate true}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/compile-track should accept :rearticulate true");
  }
  END_TRY

  TestDecodedTrk1Event evt2;
  TEST_ASSERT_TRUE(decode_test_compiled_event(result, 3, &evt2));
  TEST_ASSERT_EQUAL_UINT8(TRK1_EVT_NOTE_EX, evt2.event_type);
  TEST_ASSERT_BITS_HIGH(TRK1_NOTE_FLAG_RETRIGGER, evt2.note_flags);
}

TEST(test_sound_tiny_fx_sound_compile_track_melody_backing_preserves_articulation_flags) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:melody :C4 :backing :G3maj :duration :q :articulation :legato} "
        "       {:melody :D4 :backing :A3maj :duration :q}] "
        "      {:melody {:volume 0} :backing {:volume 0} :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/compile-track melody/backing articulation should not throw");
  }
  END_TRY

  TestDecodedTrk1Event evt2;
  TEST_ASSERT_TRUE(decode_test_compiled_event(result, 3, &evt2));
  TEST_ASSERT_EQUAL_UINT8(TRK1_EVT_NOTE_EX, evt2.event_type);
  TEST_ASSERT_BITS_HIGH(TRK1_NOTE_FLAG_LEGATO, evt2.note_flags);
}

TEST(test_sound_tiny_fx_sound_compile_track_emits_track_envelope_once) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [:A4] :duration :q} {:notes [:A4] :duration :q}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120 "
        "       :envelope [1.0 1.0 1.0 1.0 0.1]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/compile-track should accept a per-track :envelope");
  }
  END_TRY

  TestDecodedTrk1Event evt0;
  TEST_ASSERT_TRUE(decode_test_compiled_event(result, 0, &evt0));
  TEST_ASSERT_EQUAL_UINT8(TRK1_EVT_SET_ENV, evt0.event_type);
  TEST_ASSERT_EQUAL_UINT8(5u, evt0.envelope_point_count);
  TEST_ASSERT_EQUAL_UINT8(255u, evt0.envelope_levels[0]);
  TEST_ASSERT_EQUAL_UINT8(26u, evt0.envelope_levels[4]);
}

TEST(test_sound_tiny_fx_sound_compile_track_uses_default_track_envelope) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [:A4] :duration :q}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/compile-track should apply the default :envelope when omitted");
  }
  END_TRY

  TestDecodedTrk1Event evt0;
  TEST_ASSERT_TRUE(decode_test_compiled_event(result, 0, &evt0));
  TEST_ASSERT_EQUAL_UINT8(TRK1_EVT_SET_ENV, evt0.event_type);
  TEST_ASSERT_EQUAL_UINT8(3u, evt0.envelope_point_count);
  TEST_ASSERT_EQUAL_UINT8(255u, evt0.envelope_levels[0]);
  TEST_ASSERT_EQUAL_UINT8(255u, evt0.envelope_levels[1]);
  TEST_ASSERT_EQUAL_UINT8(51u, evt0.envelope_levels[2]);
}

TEST(test_sound_tiny_fx_sound_nonpositive_integer_duration_throws) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :bad-numeric-dur "
        "      [{:notes [:G5] :duration 0}] "
        "      {:channel-count 1 :volumes [0]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_bend_duration_ms_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :bend-basic "
        "                [{:notes [220] :bend [440] :duration 120}] "
        "                {:channel-count 1 :volumes [0]})] "
        "      (and (= :playing (:status ret)) "
        "           (= 120 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with bend should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_track_duration_ms_preserves_bend_duration) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (= 210 "
        "       (tiny-fx.sound/track-duration-ms "
        "         [{:notes [220] :bend [440] :duration 120} "
        "          {:notes [440] :duration 90}] "
        "         {:channel-count 1 :volumes [0]})))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/track-duration-ms should preserve bend duration");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_bend_changes_voice_frequency_over_time) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(1);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :bend-motion "
        "                [{:notes [220] :bend [440] :duration 120}] "
        "                {:channel-count 1 :volumes [0]})] "
        "      (= :playing (:status ret))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound bend playback should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);

  sound_engine_tick();
  uint16_t initial_freq = g_sound_engine.voices[0].freq_hz;
  for (int i = 0; i < 40; i++) {
    sound_engine_tick();
  }
  uint16_t later_freq = g_sound_engine.voices[0].freq_hz;

  TEST_ASSERT_TRUE(initial_freq > 0);
  TEST_ASSERT_TRUE(later_freq > initial_freq);
}

TEST(test_sound_tiny_fx_sound_bend_rejects_melody_backing_mode_for_now) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :bend-melody-backing "
        "      [{:melody :G5 :backing 220 :bend [440] :duration 120}] "
        "      {:melody {:volume 0} :backing {:volume 0}}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_noise_duration_ms_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(1);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-sfx! :noise-basic "
        "                [{:notes [220] :noise true :duration 120}] "
        "                {:channel-count 1 :volumes [0]})] "
        "      (and (= :playing (:status ret)) "
        "           (= 120 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-sfx! with noise should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_noise_changes_voice_frequency_over_time) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(1);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-sfx! :noise-motion "
        "                [{:notes [220] :noise true :duration 120}] "
        "                {:channel-count 1 :volumes [0]})] "
        "      (= :playing (:status ret))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound noise playback should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);

  sound_engine_tick();
  uint16_t initial_freq = g_sound_engine.voices[0].freq_hz;
  for (int i = 0; i < 25; i++) {
    sound_engine_tick();
  }
  uint16_t later_freq = g_sound_engine.voices[0].freq_hz;

  TEST_ASSERT_TRUE(initial_freq > 0);
  TEST_ASSERT_TRUE(later_freq > 0);
  TEST_ASSERT_TRUE(later_freq != initial_freq);
}

TEST(test_sound_tiny_fx_sound_noise_only_affects_backing_channel) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(2);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :noise-backing "
        "                [{:melody :A4 :backing 440 :noise true :duration 120}] "
        "                {:melody {:volume 0} :backing {:volume 0}})] "
        "      (= :playing (:status ret))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound melody/backing noise should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);

  sound_engine_tick();
  TEST_ASSERT_EQUAL_UINT16(440, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz != 440);
}

TEST(test_sound_tiny_fx_sound_noise_rejects_generic_multichannel_for_now) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(2);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-sfx! :noise-poly "
        "      [{:notes [220 330] :noise true :duration 120}] "
        "      {:channel-count 2 :volumes [0 0]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_noise_rejects_melody_without_backing_channel) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(1);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :noise-melody-only "
        "      [{:melody :A4 :noise true :duration 120}] "
        "      {:melody {:volume 0}}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_noise_compile_track_size_is_bounded) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/compile-track "
        "      [{:notes [220] :noise true :duration 5000}] "
        "      {:channel-count 1 :volumes [0]}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/compile-track with bounded noise should not throw");
  }
  END_TRY

  TEST_ASSERT_EQUAL_INT(CLJ_BYTE_ARRAY, TAG(result));
  CljByteArray *arr = as_byte_array(result);
  TEST_ASSERT_NOT_NULL(arr);
  TEST_ASSERT_TRUE(arr->length < 220);
}

TEST(test_sound_tiny_fx_sound_play_steps_four_voices_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :four-voice "
        "                [{:notes [:G5 :D5 :Bb4 :F4] :duration :e} {:notes [:A5 :E5 :C5 :G4] :duration :q}] "
        "                {:channel-count 4 :volumes [0 0 0 0] :tempo-bpm 120})] "
        "      (and (= :playing (:status ret)) "
        "           (= 750 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! four-voice should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_play_steps_two_voices_activate_two_voices) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :two-voice-activation "
        "                [{:notes [:G5 :D5] :duration :q}] "
        "                {:channel-count 2 :volumes [0 0] :tempo-bpm 120})] "
        "      (and (= :playing (:status ret)) "
        "           (= 500 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! two-voice should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
  sound_engine_tick(); /* Drain command queue and parse first step */
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz != g_sound_engine.voices[1].freq_hz);
}

TEST(test_sound_tiny_fx_sound_play_melody_backing_returns_status_map) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :mel-backing "
        "                [{:melody :G5 :backing :D5min :duration :e} "
        "                 {:melody :A5 :backing :E5min :duration :e}] "
        "                {:melody {:volume 0} :backing {:channels 2 :volumes [0 0]} :tempo-bpm 120})] "
        "      (and (= :playing (:status ret)) "
        "           (= 500 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! with melody/backing should not throw");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
}

TEST(test_sound_tiny_fx_sound_play_melody_backing_auto_uses_available_channels) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :mel-backing-auto "
        "                [{:melody :G5 :backing :D5min :duration :q}] "
        "                {:melody {:volume 0} :backing {:channels 3 :volumes [0 0 0]} :tempo-bpm 120})] "
        "      (and (= :playing (:status ret)) "
        "           (= 500 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! melody/backing should auto-fill available channels");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
  sound_engine_tick(); /* Drain command queue and parse first step */
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[2].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[3].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[2].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[3].freq_hz > 0);
}

TEST(test_sound_tiny_fx_sound_play_melody_backing_auto_generates_backing_when_missing) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  ID result = NULL;
  TRY {
    result = eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (let [ret (tiny-fx.sound/play-steps! :mel-auto-missing-backing "
        "                [{:melody :G5 :duration :q}] "
        "                {:melody {:volume 0} :backing {:channels 2 :volumes [0 0]} :tempo-bpm 120})] "
        "      (and (= :playing (:status ret)) "
        "           (= 500 (:duration-ms ret)))))",
        g_test_eval_state);
  }
  CATCH(ex) {
    TEST_FAIL_MESSAGE("tiny-fx.sound/play-steps! should auto-generate backing notes when :backing is omitted");
  }
  END_TRY

  TEST_ASSERT_TRUE(result == clj_true);
  sound_engine_tick(); /* Drain command queue and parse first step */
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[2].active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[2].freq_hz > 0);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz != g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_TRUE(g_sound_engine.voices[2].freq_hz != g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_TRUE(g_sound_engine.voices[1].freq_hz < g_sound_engine.voices[2].freq_hz);
}

TEST(test_sound_tiny_fx_sound_play_melody_backing_rejects_non_map_channel_opts) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :bad-channel-opts "
        "      [{:melody :G5 :backing :D5maj :duration :q}] "
        "      {:melody 0 :backing {:volume 0}}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_play_melody_backing_rejects_legacy_channel_count_opts) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :legacy-channel-count "
        "      [{:melody :G5 :backing :D5maj :duration :q}] "
        "      {:channel-count 2 :melody {:volume 0} :backing {:volume 0} :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_play_melody_backing_rejects_legacy_volume_levels_opts) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);
  sound_engine_shutdown();
  sound_engine_init(4);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/play-steps! :legacy-volume-levels "
        "      [{:melody :G5 :backing :D5maj :duration :q}] "
        "      {:melody {:volume-levels [0]} :backing {:volume 0} :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_tiny_fx_sound_track_duration_ms_rejects_legacy_dur_step_key) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  bool threw = false;
  TRY {
    (void)eval_string(
        "(do (require 'tiny-fx.sound) "
        "    (tiny-fx.sound/track-duration-ms "
        "      [{:notes [:G5] :dur :q}] "
        "      {:channel-count 1 :volumes [0] :tempo-bpm 120}))",
        g_test_eval_state);
  }
  CATCH(ex) {
    threw = true;
  }
  END_TRY

  TEST_ASSERT_TRUE(threw);
}

TEST(test_sound_native_load_unload_via_eval) {
  TEST_ASSERT_NOT_NULL(g_test_eval_state);

  /* Initialize sound engine for this test */
  sound_engine_init(2);

  /* Create a trk1 byte array and load it via the engine directly
   * (eval_string can't easily create binary data) */
  ID track_sym = (ID)intern_symbol_global(":eval-track");
  ID ba = make_test_trk1(69, 10, 2, 0);
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_EQUAL_INT(1, g_sound_engine.track_count);
  TEST_ASSERT_TRUE(sound_engine_unload_track(track_sym));
  TEST_ASSERT_EQUAL_INT(0, g_sound_engine.track_count);

  RELEASE(ba);
  sound_engine_shutdown();
}

/* ========================================================================= */
/* MIDI note to frequency conversion                                         */
/* ========================================================================= */

TEST(test_sound_midi_note_to_freq) {
  TEST_ASSERT_EQUAL_UINT16(0, midi_note_to_freq(0));    /* rest */
  TEST_ASSERT_EQUAL_UINT16(262, midi_note_to_freq(60)); /* C4 */
  TEST_ASSERT_EQUAL_UINT16(440, midi_note_to_freq(69)); /* A4 */
  TEST_ASSERT_EQUAL_UINT16(880, midi_note_to_freq(81)); /* A5 */
  TEST_ASSERT_EQUAL_UINT16(0, midi_note_to_freq(128));  /* out of range */
}

TEST(test_sound_tick_scheduler_waits_until_deadline) {
  SoundTickScheduler scheduler;
  uint32_t skipped = 99u;

  sound_tick_scheduler_init(&scheduler, 1000000u, 4u);
  sound_tick_scheduler_start(&scheduler, 10000000u);

  TEST_ASSERT_EQUAL_UINT32(0u, sound_tick_scheduler_ticks_due(&scheduler, 10000000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT32(0u, sound_tick_scheduler_ticks_due(&scheduler, 10999999u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT32(1u, sound_tick_scheduler_ticks_due(&scheduler, 11000000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT64(12000000u, scheduler.next_deadline_ns);
}

TEST(test_sound_tick_scheduler_caps_catchup_and_resyncs_deadline) {
  SoundTickScheduler scheduler;
  uint32_t skipped = 0u;

  sound_tick_scheduler_init(&scheduler, 1000000u, 2u);
  sound_tick_scheduler_start(&scheduler, 0u);

  TEST_ASSERT_EQUAL_UINT32(2u, sound_tick_scheduler_ticks_due(&scheduler, 5500000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(3u, skipped);
  TEST_ASSERT_EQUAL_UINT64(6000000u, scheduler.next_deadline_ns);

  TEST_ASSERT_EQUAL_UINT32(0u, sound_tick_scheduler_ticks_due(&scheduler, 5999999u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
  TEST_ASSERT_EQUAL_UINT32(1u, sound_tick_scheduler_ticks_due(&scheduler, 6000000u, &skipped));
  TEST_ASSERT_EQUAL_UINT32(0u, skipped);
}

/* ========================================================================= */
/* Tick on-demand lifecycle tests                                            */
/* ========================================================================= */

TEST(test_sound_tick_starts_on_play) {
  sound_engine_init(1);
  TEST_ASSERT_FALSE(g_sound_engine.tick_running);

  ID track_sym = (ID)intern_symbol_global(":lifecycle-test");
  ID ba = make_test_trk1(60, 2, 1, 0);
  sound_engine_load_track(track_sym, ba);

  sound_engine_play_music(track_sym, 1);
  TEST_ASSERT_TRUE(g_sound_engine.tick_running);

  /* Tick until stream ends */
  for (int i = 0; i < 20; i++) {
    sound_engine_tick();
  }
  /* After stream ends: tick should self-stop */
  TEST_ASSERT_FALSE(g_sound_engine.tick_running);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_tick_stays_running_while_voice_gate_active) {
  sound_engine_init(1);
  TEST_ASSERT_FALSE(g_sound_engine.tick_running);

  ID track_sym = (ID)intern_symbol_global(":gate-lifecycle-test");
  ID ba = make_test_trk1(60, 30, 1, 0); /* NOTE + END same tick, gate remains */
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick(); /* parses NOTE + END; stream ends, voice gate still active */
  TEST_ASSERT_FALSE(g_sound_engine.music_stream.active);
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].gate_remaining_ticks > 0);
  TEST_ASSERT_TRUE(g_sound_engine.tick_running);

  /* After gate fully decays, tick should auto-stop */
  for (int i = 0; i < 40; i++) {
    sound_engine_tick();
  }
  TEST_ASSERT_FALSE(g_sound_engine.tick_running);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_engine_ticks_until_deadline_reports_gate_tail) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":deadline-gate-test");
  ID ba = make_test_trk1(60, 30, 1, 0);
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick(); /* NOTE + END same tick; remaining gate tail stays active */
  TEST_ASSERT_FALSE(g_sound_engine.music_stream.active);
  TEST_ASSERT_EQUAL_UINT32(29u, sound_engine_ticks_until_deadline());

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_engine_ticks_until_deadline_reports_delayed_event) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":deadline-delay-test");
  ID ba = make_test_trk1_with_delay(69, 1, 5, 1);
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick(); /* parses NOTE, gate expires, END is delayed */
  TEST_ASSERT_TRUE(g_sound_engine.music_stream.active);
  TEST_ASSERT_EQUAL_UINT32(4u, sound_engine_ticks_until_deadline());

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_engine_advance_ticks_fast_forwards_gate_tail_to_idle) {
  sound_engine_init(1);

  ID track_sym = (ID)intern_symbol_global(":advance-gate-test");
  ID ba = make_test_trk1(60, 30, 1, 0);
  TEST_ASSERT_TRUE(sound_engine_load_track(track_sym, ba));
  TEST_ASSERT_TRUE(sound_engine_play_music(track_sym, 1));

  sound_engine_tick(); /* NOTE + END same tick */
  TEST_ASSERT_TRUE(g_sound_engine.tick_running);
  TEST_ASSERT_EQUAL_UINT32(29u, sound_engine_ticks_until_deadline());

  sound_engine_advance_ticks(sound_engine_ticks_until_deadline());
  TEST_ASSERT_EQUAL_UINT32(0u, g_sound_engine.voices[0].gate_remaining_ticks);
  TEST_ASSERT_EQUAL_UINT16(0u, g_sound_engine.voices[0].freq_hz);
  TEST_ASSERT_FALSE(g_sound_engine.tick_running);

  RELEASE(ba);
  sound_engine_shutdown();
}

TEST(test_sound_stop_all_silences_voices) {
  sound_engine_init(2);

  ID track_sym = (ID)intern_symbol_global(":stopall-test");
  ID ba = make_test_trk1(69, 1000, 1, 0);
  sound_engine_load_track(track_sym, ba);
  sound_engine_play_music(track_sym, 0);

  sound_engine_tick(); /* start */
  TEST_ASSERT_TRUE(g_sound_engine.voices[0].freq_hz > 0);

  sound_engine_stop_all();
  sound_engine_tick(); /* drain stop_all */

  for (int i = 0; i < g_sound_engine.voice_count; i++) {
    TEST_ASSERT_FALSE(g_sound_engine.voices[i].active);
  }

  RELEASE(ba);
  sound_engine_shutdown();
}
