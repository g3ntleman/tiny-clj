(ns tiny-fx.sound)

^#^{:doc "Starts music playback directly from compiled TRK1 bytes. track-id is used for stop/finished notifications; repeat-count follows engine semantics."}
(def sound-play-music! (fn sound-play-music! [track-id track-bytes repeat-count] :native))

^#^{:doc "Stops playback for one specific track id."}
(def sound-stop-track! (fn sound-stop-track! [track-id] :native))

^#^{:doc "Stops the active music stream."}
(def sound-stop-music! (fn sound-stop-music! [] :native))

^#^{:doc "Starts a one-shot sound effect directly from compiled TRK1 bytes. sfx-id is used for stop/finished notifications."}
(def sound-play-sfx! (fn sound-play-sfx! [sfx-id track-bytes] :native))

^#^{:doc "Stops all active sound channels."}
(def sound-stop-all! (fn sound-stop-all! [] :native))

^#^{:doc "Sets per-track playback volume (0..255)."}
(def sound-set-track-volume! (fn sound-set-track-volume! [track-id volume] :native))

^#^{:doc "Sets global music stream volume (0..255)."}
(def sound-set-music-volume! (fn sound-set-music-volume! [volume] :native))

^#^{:doc "Registers callback fn(event-map) for finished track notifications."}
(def sound-on-finished! (fn sound-on-finished! [callback-fn] :native))

