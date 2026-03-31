(ns tiny-fx.sound)

^#^{:doc "Starts music playback directly from compiled TRK1 bytes. track-id is used for stop/finished notifications; repeat-count follows engine semantics."}
(defn sound-play-music! [track-id track-bytes repeat-count] :native)

^#^{:doc "Stops playback for one specific track id."}
(defn sound-stop-track! [track-id] :native)

^#^{:doc "Stops the active music stream."}
(defn sound-stop-music! [] :native)

^#^{:doc "Starts a one-shot sound effect directly from compiled TRK1 bytes. sfx-id is used for stop/finished notifications."}
(defn sound-play-sfx! [sfx-id track-bytes] :native)

^#^{:doc "Stops all active sound channels."}
(defn sound-stop-all! [] :native)

^#^{:doc "Sets per-track playback volume (0..255)."}
(defn sound-set-track-volume! [track-id volume] :native)

^#^{:doc "Sets global music stream volume (0..255)."}
(defn sound-set-music-volume! [volume] :native)

^#^{:doc "Registers callback fn(event-map) for finished track notifications."}
(defn sound-on-finished! [callback-fn] :native)

