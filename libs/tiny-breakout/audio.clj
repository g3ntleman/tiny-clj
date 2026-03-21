(ns tiny-breakout.audio
  (:require [tiny-fx.sound :as sound]))

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

(defn play-cue!
  "Plays one symbolic cue when it resolves to a known SFX descriptor."
  [cue-id]
  (let [spec (sfx-spec cue-id)]
    (if (map? spec)
      (sound/play! spec)
      nil)))

(defn play-events!
  "Plays all known audio cues for the given gameplay events."
  [events]
  (loop [remaining (events->cues events)
         out []]
    (if (empty? remaining)
      out
      (recur (rest remaining)
             (conj out (play-cue! (first remaining)))))))
