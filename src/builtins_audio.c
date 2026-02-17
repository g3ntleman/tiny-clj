/*
 * Native audio builtins for Clojure API.
 *
 * audio-load-track!, audio-unload-track!, audio-play-music!,
 * audio-stop-track!, audio-stop-music!, audio-play-sfx!,
 * audio-stop-all!, audio-set-track-volume!, audio-set-music-volume!,
 * audio-on-finished!
 */

#include "audio_engine.h"
#include "builtins.h"
#include "validation.h"
#include "value.h"
#include "symbol.h"
#include "byte_array.h"
#include "exception.h"

/* ========================================================================= */
/* Native function implementations                                           */
/* ========================================================================= */

ID native_audio_load_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "audio-load-track!")) return NULL;

    ID track_id = args[0];
    ID bytes_obj = args[1];

    if (!track_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-load-track! track-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    bool ok = audio_engine_load_track(track_id, bytes_obj);
    return ok ? clj_true : clj_false;
}

ID native_audio_unload_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-unload-track!")) return NULL;

    ID track_id = args[0];
    if (!track_id) return clj_false;

    bool ok = audio_engine_unload_track(track_id);
    return ok ? clj_true : clj_false;
}

ID native_audio_play_music(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "audio-play-music!")) return NULL;

    ID track_id = args[0];
    ID repeat_arg = args[1];

    if (!track_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-play-music! track-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t repeat = 1;
    if (repeat_arg && is_fixnum((CljValue)repeat_arg)) {
        repeat = (int32_t)as_fixnum((CljValue)repeat_arg);
    }

    bool ok = audio_engine_play_music(track_id, repeat);
    return ok ? clj_true : clj_false;
}

ID native_audio_stop_track(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-stop-track!")) return NULL;

    ID track_id = args[0];
    if (!track_id) return clj_false;

    bool ok = audio_engine_stop_track(track_id);
    return ok ? clj_true : clj_false;
}

ID native_audio_stop_music(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "audio-stop-music!")) return NULL;
    (void)args;

    audio_engine_stop_music();
    return NULL;
}

ID native_audio_play_sfx(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-play-sfx!")) return NULL;

    ID sfx_id = args[0];
    if (!sfx_id) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-play-sfx! sfx-id must not be nil",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    bool ok = audio_engine_play_sfx(sfx_id);
    return ok ? clj_true : clj_false;
}

ID native_audio_stop_all(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 0, "audio-stop-all!")) return NULL;
    (void)args;

    audio_engine_stop_all();
    return NULL;
}

ID native_audio_set_track_volume(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 2, "audio-set-track-volume!")) return NULL;

    ID track_id = args[0];
    ID vol_arg = args[1];

    if (!track_id) return clj_false;
    if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-set-track-volume! volume must be integer 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t vol = (int32_t)as_fixnum((CljValue)vol_arg);
    bool ok = audio_engine_set_track_volume(track_id, vol);
    return ok ? clj_true : clj_false;
}

ID native_audio_set_music_volume(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-set-music-volume!")) return NULL;

    ID vol_arg = args[0];
    if (!vol_arg || !is_fixnum((CljValue)vol_arg)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT,
                        "audio-set-music-volume! volume must be integer 0..255",
                        __FILE__, __LINE__, 0);
        return NULL;
    }

    int32_t vol = (int32_t)as_fixnum((CljValue)vol_arg);
    audio_engine_set_music_volume(vol);
    return NULL;
}

ID native_audio_on_finished(ID *args, unsigned int argc) {
    if (!validate_arity(argc, 1, "audio-on-finished!")) return NULL;

    ID fn = args[0];
    audio_engine_on_finished(fn);
    return NULL;
}
