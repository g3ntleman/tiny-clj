(ns tiny-breakout.audio
  (:require [tiny-fx.sound :as sound]
            [tiny-fx.sound-native :as sound-native]))

(def sfx-library
  {:sfx/paddle-hit
   {:track-id :tiny-breakout/paddle-hit
    :kind :sfx
    :steps [{:notes [1040] :duration 20}
            {:notes [1320] :duration 24}]
    :opts {:channel-count 1
           :gate-percent 72
           :volumes [180]}}

   :sfx/brick-hit
   {:track-id :tiny-breakout/brick-hit
    :kind :sfx
    :steps [{:notes [1480] :duration 16}
            {:notes [1720] :duration 16}
            {:notes [1960] :duration 18}]
    :opts {:channel-count 1
           :gate-percent 70
           :volumes [200]}}

   :sfx/life-lost
   {:track-id :tiny-breakout/life-lost
    :kind :sfx
    :steps [{:notes [880] :duration 32}
            {:notes [660] :duration 36}
            {:notes [440] :duration 54}]
    :opts {:channel-count 1
           :gate-percent 78
           :volumes [220]}}

   :sfx/level-clear
   {:track-id :tiny-breakout/level-clear
    :kind :sfx
    :steps [{:notes [880] :duration 24}
            {:notes [1320] :duration 24}
            {:notes [1760] :duration 36}]
    :opts {:channel-count 1
           :gate-percent 76
           :volumes [210]}}

   :sfx/game-over
   {:track-id :tiny-breakout/game-over
    :kind :sfx
    :steps [{:notes [740] :duration 28}
            {:notes [554] :duration 36}
            {:notes [392] :duration 60}]
    :opts {:channel-count 1
           :gate-percent 80
           :volumes [220]}}

   :sfx/victory
   {:track-id :tiny-breakout/victory
    :kind :sfx
    :steps [{:notes [1040] :duration 20}
            {:notes [1318] :duration 20}
            {:notes [1568] :duration 24}
            {:notes [2093] :duration 48}]
    :opts {:channel-count 1
           :gate-percent 74
           :volumes [210]}}})

(defn event->cue
  "Maps one normalized gameplay event keyword to a symbolic audio cue."
  [event-id]
  (cond
    (= event-id :paddle-hit) :sfx/paddle-hit
    (= event-id :brick-hit) :sfx/brick-hit
    (= event-id :life-lost) :sfx/life-lost
    (= event-id :level-clear) :sfx/level-clear
    (= event-id :game-over) :sfx/game-over
    (= event-id :victory) :sfx/victory
    :else nil))

(defn sfx-spec
  "Returns one deterministic mini-SFX descriptor map or nil."
  [cue-id]
  (get sfx-library cue-id))

(defn events->cues
  "Maps a vector/list of event keywords to cue keywords, dropping unknown events."
  [events]
  (let [xs (if (vector? events) events (if (list? events) (vec events) []))]
    (reduce (fn [out e]
              (let [cue (event->cue e)]
                (if cue (conj out cue) out)))
            []
            xs)))

(def ^:private preloaded-durations* (atom {}))

(defn- ensure-cue-loaded!
  [cue-id]
  (if (contains? @preloaded-durations* cue-id)
    (get @preloaded-durations* cue-id)
    (let [spec (sfx-spec cue-id)
          track-id (:track-id spec)
          steps (:steps spec)
          opts (:opts spec)]
      (if (and track-id steps)
        (let [track-bytes (sound/compile-track steps opts)
              duration-ms (sound/track-duration-ms steps opts)]
          (sound-native/sound-load-track! track-id track-bytes)
          (swap! preloaded-durations* assoc cue-id duration-ms)
          duration-ms)
        0))))

(defn preload!
  "Pre-compiles and loads all SFX tracks into the sound engine."
  []
  (loop [entries (seq sfx-library)
         durations {}]
    (if (empty? entries)
      (reset! preloaded-durations* durations)
      (let [entry (first entries)
            cue-id (first entry)
            spec (second entry)
            track-id (:track-id spec)
            steps (:steps spec)
            opts (:opts spec)]
        (if (and track-id steps)
          (let [track-bytes (sound/compile-track steps opts)
                duration-ms (sound/track-duration-ms steps opts)]
            (sound-native/sound-load-track! track-id track-bytes)
            (recur (rest entries) (assoc durations cue-id duration-ms)))
          (recur (rest entries) durations))))))

(defn play-cue!
  "Triggers a pre-loaded SFX by cue id.
   Returns a status map compatible with sound/play-sfx! or nil."
  [cue-id]
  (let [spec (sfx-spec cue-id)]
    (when (map? spec)
      (let [duration-ms (ensure-cue-loaded! cue-id)
            ok (sound-native/sound-play-sfx! (:track-id spec))]
        {:status (if ok :playing :dropped)
         :duration-ms duration-ms}))))

(defn play-events!
  "Plays all known audio cues for the given gameplay events."
  [events]
  (loop [remaining (events->cues events)]
    (if (empty? remaining)
      nil
      (do
        (play-cue! (first remaining))
        (recur (rest remaining))))))
