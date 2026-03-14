
(ns tiny-fx.sound-native)
;; Internal native sound namespace.
;; Public callers should prefer tiny-fx.sound.
;; Low-level runtime control stays here and is opt-in.
;; Ramp/bend was removed; use tiny-fx.sound DSL NOTE_Hz steps or host debug helpers only.

^#^{:doc "Loads a compiled track byte-array into the runtime under track-id."}
(def sound-load-track! (fn sound-load-track! [track-id track-bytes] :native))

^#^{:doc "Removes a previously loaded track by id."}
(def sound-unload-track! (fn sound-unload-track! [track-id] :native))

^#^{:doc "Plays a loaded track as music. repeat-count follows engine semantics."}
(def sound-play-music! (fn sound-play-music! [track-id repeat-count] :native))

^#^{:doc "Stops playback for one specific track id."}
(def sound-stop-track! (fn sound-stop-track! [track-id] :native))

^#^{:doc "Stops the active music stream."}
(def sound-stop-music! (fn sound-stop-music! [] :native))

^#^{:doc "Plays a loaded track id as one-shot sound effect."}
(def sound-play-sfx! (fn sound-play-sfx! [sfx-id] :native))

^#^{:doc "Stops all active sound channels."}
(def sound-stop-all! (fn sound-stop-all! [] :native))

^#^{:doc "Sets per-track playback volume (0..255)."}
(def sound-set-track-volume! (fn sound-set-track-volume! [track-id volume] :native))

^#^{:doc "Sets global music stream volume (0..255)."}
(def sound-set-music-volume! (fn sound-set-music-volume! [volume] :native))

^#^{:doc "Registers callback fn(event-map) for finished track notifications."}
(def sound-on-finished! (fn sound-on-finished! [callback-fn] :native))
