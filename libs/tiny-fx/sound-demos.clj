(ns tiny-fx.sound-demos
  (:require [tiny-fx.assets :as assets]))

(def ^:private sfx-keys
  #{:rocket-launch-sfx :laser-sfx})

(def ^:private trk1-cache-root
  "/data/tiny-fx/sound-demos")

(defn bytes-asset-under-prefix
  "Loads raw bytes from `/assets/<ns-path>/<file-name>` via the embedded FS.
  Returns nil if the asset is missing or unreadable."
  [ns-path file-name]
  (try
    (slurp-bytes (str "/assets/" ns-path "/" file-name))
    (catch Exception _ nil)))

(defn play-startup-entertainer!
  "Plays precompiled TRK1 `the-entertainer.trk1` once at host startup.
  Only the compiled payload is loaded from the asset store; the Clojure
  reference to the byte-array is not retained after this call returns (the
  sound engine keeps its own retain until playback ends)."
  []
  (let [b (bytes-asset-under-prefix "tiny-fx/sound-demos" "the-entertainer.trk1")]
    (if b
      (try
        (require 'tiny-fx.sound)
        ((var tiny-fx.sound/sound-play-music!) :startup/the-entertainer b 1)
        (catch Exception _ nil))
      nil))
  nil)

(defn- trk1-cache-path
  [which]
  (str trk1-cache-root "/" (name which) ".trk1"))

(defn- trk1-cache-meta-path
  [which]
  (str trk1-cache-root "/" (name which) ".meta.edn"))

(declare write-trk1-cache!)

(defn- recover-track-duration-ms
  [song]
  (try
    (require 'tiny-fx.trk1)
    ((var tiny-fx.trk1/track-duration-ms) (:steps song) (:opts song))
    (catch Exception _ nil)
    (finally
      (try
        (ns-unload 'tiny-fx.trk1)
        (catch Exception _ false)))))

(defn- read-trk1-cache
  [which song]
  (let [track-bytes (try
                      (slurp-bytes (trk1-cache-path which))
                      (catch Exception _ nil))
        meta-map (try
                   (let [s (slurp (trk1-cache-meta-path which))]
                     (if s (read-string s) nil))
                   (catch Exception _ nil))
        cached-duration-ms (:duration-ms meta-map)]
    (if track-bytes
      (let [duration-ms (if (integer? cached-duration-ms)
                          cached-duration-ms
                          (recover-track-duration-ms song))
            safe-duration-ms (if (integer? duration-ms) duration-ms 0)]
        (if (and (not (integer? cached-duration-ms))
                 (integer? duration-ms))
          (write-trk1-cache! which track-bytes duration-ms)
          nil)
        {:track-bytes track-bytes
         :duration-ms safe-duration-ms})
      nil)))

(defn- write-trk1-cache!
  [which track-bytes duration-ms]
  (if (and track-bytes (integer? duration-ms))
    (try
      (require 'tiny-clj.fs)
      ((var tiny-clj.fs/spit-bytes) (trk1-cache-path which) track-bytes)
      (spit (trk1-cache-meta-path which) (str {:duration-ms duration-ms}))
      true
      (catch Exception _ false))
    false))

(defn load-song
  "Loads one demo song descriptor with :track-id, :steps, :opts and inferred :kind.
  This function returns the source descriptor only (no compilation)."
  [which]
  (assoc (assets/edn-asset-under-prefix "tiny-fx/sound-demos"
                                        (str (name which) ".edn")
                                        [:track-id :steps :opts])
         :kind
         (if (contains? sfx-keys which) :sfx :music)))

(defn- compile-song-track
  [song]
  (require 'tiny-fx.trk1)
  (try
    ((var tiny-fx.trk1/prepare-track) (:steps song) (:opts song))
    (finally
      (try
        (ns-unload 'tiny-fx.trk1)
        (catch Exception _ false)))))

(defn- load-song-track
  [which song]
  (or (read-trk1-cache which song)
      (let [prepared (compile-song-track song)
            track-bytes (:track-bytes prepared)
            duration-ms (:duration-ms prepared)]
        (write-trk1-cache! which track-bytes duration-ms)
        {:track-bytes track-bytes
         :duration-ms duration-ms})))

(defn play-demo!
  "Plays one bundled demo.
  The track is loaded from Flash cache when available; otherwise it is compiled
  once, cached as TRK1 bytes, and the compiler namespace is unloaded."
  [which]
  (require 'tiny-fx.sound)
  (let [d (load-song which)
        track (load-song-track which d)
        track-bytes (:track-bytes track)
        duration-ms (:duration-ms track)
        _ (try
            (ns-unload 'tiny-fx.trk1)
            (catch Exception _ false))
        status (if (= (:kind d) :sfx)
                 (if ((var tiny-fx.sound/sound-play-sfx!) (:track-id d) track-bytes) :playing :dropped)
                 (if ((var tiny-fx.sound/sound-play-music!) (:track-id d) track-bytes 1) :playing :stopped))]
    {:status status
     :duration-ms duration-ms}))
