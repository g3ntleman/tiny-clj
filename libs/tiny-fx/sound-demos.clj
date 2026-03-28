(ns tiny-fx.sound-demos
  (:require [tiny-fx.assets :as assets]
            [tiny-clj.fs :as fs]
            [tiny-fx.sound :as sound]))

(def ^:private sfx-keys
  #{:rocket-launch-sfx :laser-sfx})

(defn bytes-asset-under-prefix
  "Loads raw bytes from `/assets/<ns-path>/<file-name>` via the embedded FS.
  Returns nil if the asset is missing or unreadable."
  [ns-path file-name]
  (try
    (fs/slurp-bytes (str "/assets/" ns-path "/" file-name))
    (catch Exception _ nil)))

(defn play-startup-entertainer!
  "Plays precompiled TRK1 `the-entertainer.trk1` once at host startup.
  Only the compiled payload is loaded from the asset store; the Clojure
  reference to the byte-array is not retained after this call returns (the
  sound engine keeps its own retain until playback ends)."
  []
  (when-let [b (bytes-asset-under-prefix "tiny-fx/sound-demos" "the-entertainer.trk1")]
    (try
      (sound/sound-play-music! :startup/the-entertainer b 1)
      (catch Exception _ nil)))
  nil)

(defn load-song
  "Loads one demo song descriptor with :track-id, :steps, :opts and inferred :kind."
  [which]
  (assoc (assets/edn-asset-under-prefix "tiny-fx/sound-demos"
                                                       (str (name which) ".edn")
                                                       [:track-id :steps :opts])
         :kind
         (if (contains? sfx-keys which) :sfx :music)))

(defn play-demo!
  "Compiles and plays one bundled demo via explicit trk1/runtime composition."
  [which]
  (require 'tiny-fx.sound)
  (require 'tiny-fx.trk1)
  (let [d (load-song which)
        prepared ((var tiny-fx.trk1/prepare-track) (:steps d) (:opts d))
        track-bytes (:track-bytes prepared)
        duration-ms (:duration-ms prepared)
        status (if (= (:kind d) :sfx)
                 (if ((var tiny-fx.sound/sound-play-sfx!) (:track-id d) track-bytes) :playing :dropped)
                 (if ((var tiny-fx.sound/sound-play-music!) (:track-id d) track-bytes 1) :playing :stopped))]
    {:status status
     :duration-ms duration-ms}))
