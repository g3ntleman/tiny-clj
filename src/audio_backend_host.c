/*
 * Audio backend stub for host builds (macOS/Linux).
 * No actual sound output; voices are tracked in g_audio_engine.
 */

#ifndef ESP32_BUILD

#include "audio_engine.h"

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

#endif /* !ESP32_BUILD */
