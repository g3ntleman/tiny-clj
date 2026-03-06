#ifndef TINY_CLJ_BUILTINS_TINY_FX_SOUND_H
#define TINY_CLJ_BUILTINS_TINY_FX_SOUND_H

#include "builtins.h"

ID native_audio_load_track(ID *args, unsigned int argc);
ID native_audio_unload_track(ID *args, unsigned int argc);
ID native_audio_play_music(ID *args, unsigned int argc);
ID native_audio_stop_track(ID *args, unsigned int argc);
ID native_audio_stop_music(ID *args, unsigned int argc);
ID native_audio_play_sfx(ID *args, unsigned int argc);
ID native_audio_stop_all(ID *args, unsigned int argc);
ID native_audio_set_track_volume(ID *args, unsigned int argc);
ID native_audio_set_music_volume(ID *args, unsigned int argc);
ID native_audio_on_finished(ID *args, unsigned int argc);
ID native_audio_play_test_tone(ID *args, unsigned int argc);
ID native_audio_host_status(ID *args, unsigned int argc);

#endif
