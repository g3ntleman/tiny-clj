(ns tiny-breakout.audio
  (:require [tiny-fx.sound :as sound]))

;; Runtime audio keeps precompiled TRK1 payloads in namespace defs so gameplay
;; can reuse them directly without relying on backend-side preloading state.
;; The DSL compiler lives in tiny-breakout.audio-compiler and is intentionally
;; not required here.
(def paddle-hit-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 13 0 0 0 0 0 0 0 16 180 176 16 4 20 20 176 40 5 20 24 32]))

(def brick-hit-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 18 0 0 0 0 0 0 0 16 200 176 200 5 20 16 176 184 6 20 16 176 168 7 20 18 32]))

(def life-lost-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 18 0 0 0 0 0 0 0 16 220 176 112 3 24 32 176 148 2 28 36 176 184 1 42 54 32]))

(def level-clear-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 18 0 0 0 0 0 0 0 16 210 176 112 3 20 24 176 40 5 20 24 176 224 6 27 36 32]))

(def game-over-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 18 0 0 0 0 0 0 0 16 220 176 228 2 22 28 176 42 2 28 36 176 136 1 48 60 32]))

(def victory-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 23 0 0 0 0 0 0 0 16 210 176 16 4 20 20 176 38 5 20 20 176 32 6 20 24 176 45 8 35 48 32]))

(def wall-hit-track-bytes
  (byte-array [84 82 75 49 1 0 1 0 1 0 60 0 13 0 0 0 0 0 0 0 16 140 176 148 2 20 16 176 184 1 20 18 32]))

(def cue-specs
  {:sfx/paddle-hit
   {:track-id :tiny-breakout/paddle-hit
    :track-bytes paddle-hit-track-bytes}

   :sfx/brick-hit
   {:track-id :tiny-breakout/brick-hit
    :track-bytes brick-hit-track-bytes}

   :sfx/life-lost
   {:track-id :tiny-breakout/life-lost
    :track-bytes life-lost-track-bytes}

   :sfx/level-clear
   {:track-id :tiny-breakout/level-clear
    :track-bytes level-clear-track-bytes}

   :sfx/game-over
   {:track-id :tiny-breakout/game-over
    :track-bytes game-over-track-bytes}

   :sfx/victory
   {:track-id :tiny-breakout/victory
    :track-bytes victory-track-bytes}

   :sfx/wall-hit
   {:track-id :tiny-breakout/wall-hit
    :track-bytes wall-hit-track-bytes}})

(defn events->cues
  "Filters cue ids and drops unknown entries."
  [events]
  (loop [remaining (seq events)
         out []]
    (if (seq remaining)
      (let [cue-id (first remaining)
            cue (if (contains? cue-specs cue-id) cue-id nil)]
        (recur (next remaining)
               (if cue (conj out cue) out)))
      out)))

(defn- play-cue!
  "Best-effort playback of one breakout cue through tiny-fx sound backend."
  [cue-id]
  (let [spec (get tiny-breakout.audio/cue-specs cue-id)]
    (when (map? spec)
      (try
        (sound/sound-stop-track! (:track-id spec))
        (sound/sound-play-sfx! (:track-id spec) (:track-bytes spec))
        (catch RuntimeException _
          nil)
        (catch Exception _
          nil)))))

(defn prewarm-engine!
  "Starts native sound backend + tick path before first gameplay SFX.
  Avoids a one-frame hitch when the first audible cue is :brick-hit (lazy init
  otherwise happens inside sound-play-sfx!)."
  []
  (try
    (sound/sound-stop-all!)
    (catch RuntimeException _ nil)
    (catch Exception _ nil))
  nil)

(defn play-events!
  "Plays all known breakout cue ids as one-shot SFX."
  [events]
  (loop [remaining (seq events)]
    (when (seq remaining)
      (let [cue-id (first remaining)
            cue (if (contains? cue-specs cue-id) cue-id nil)]
        (when cue (play-cue! cue))
        (recur (next remaining)))))
  nil)
