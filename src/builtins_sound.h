#ifndef TINY_CLJ_BUILTINS_SOUND_H
#define TINY_CLJ_BUILTINS_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include "builtins.h"

typedef void (*BuiltinsSoundRegisterFn)(const char *cname, BuiltinFn fn);

bool builtins_sound_namespace_allowed(const char *cname, size_t ns_len);
void builtins_sound_register(BuiltinsSoundRegisterFn registrar);

ID native_sound_load_track(ID *args, unsigned int argc);
ID native_sound_unload_track(ID *args, unsigned int argc);
ID native_sound_play_music(ID *args, unsigned int argc);
ID native_sound_stop_track(ID *args, unsigned int argc);
ID native_sound_stop_music(ID *args, unsigned int argc);
ID native_sound_play_sfx(ID *args, unsigned int argc);
ID native_sound_stop_all(ID *args, unsigned int argc);
ID native_sound_set_track_volume(ID *args, unsigned int argc);
ID native_sound_set_music_volume(ID *args, unsigned int argc);
ID native_sound_on_finished(ID *args, unsigned int argc);
#ifdef DEBUG
ID native_sound_play_test_tone(ID *args, unsigned int argc);
ID native_sound_host_status(ID *args, unsigned int argc);
#endif

#endif
