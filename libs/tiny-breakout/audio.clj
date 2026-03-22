(ns tiny-breakout.audio)

(def ^:private sound-loaded?* (atom false))

(defn- ensure-sound!
  []
  (when (not @sound-loaded?*)
    (require 'tiny-fx.sound)
    (require 'tiny-fx.sound-native)
    (reset! sound-loaded?* true))
  nil)

(defn- sfx-spec
  [cue-id]
  (cond
    (= cue-id :sfx/paddle-hit)
    {:track-id :tiny-breakout/paddle-hit
     :steps [{:notes [1040] :duration 20}
             {:notes [1320] :duration 24}]
     :opts {:channel-count 1 :gate-percent 72 :volumes [180]}}

    (= cue-id :sfx/brick-hit)
    {:track-id :tiny-breakout/brick-hit
     :steps [{:notes [1480] :duration 16}
             {:notes [1720] :duration 16}
             {:notes [1960] :duration 18}]
     :opts {:channel-count 1 :gate-percent 70 :volumes [200]}}

    (= cue-id :sfx/life-lost)
    {:track-id :tiny-breakout/life-lost
     :steps [{:notes [880] :duration 32}
             {:notes [660] :duration 36}
             {:notes [440] :duration 54}]
     :opts {:channel-count 1 :gate-percent 78 :volumes [220]}}

    (= cue-id :sfx/level-clear)
    {:track-id :tiny-breakout/level-clear
     :steps [{:notes [880] :duration 24}
             {:notes [1320] :duration 24}
             {:notes [1760] :duration 36}]
     :opts {:channel-count 1 :gate-percent 76 :volumes [210]}}

    (= cue-id :sfx/game-over)
    {:track-id :tiny-breakout/game-over
     :steps [{:notes [740] :duration 28}
             {:notes [554] :duration 36}
             {:notes [392] :duration 60}]
     :opts {:channel-count 1 :gate-percent 80 :volumes [220]}}

    (= cue-id :sfx/victory)
    {:track-id :tiny-breakout/victory
     :steps [{:notes [1040] :duration 20}
             {:notes [1318] :duration 20}
             {:notes [1568] :duration 24}
             {:notes [2093] :duration 48}]
     :opts {:channel-count 1 :gate-percent 74 :volumes [210]}}

    :else nil))

(defn- event->cue
  [event-id]
  (cond
    (= event-id :paddle-hit) :sfx/paddle-hit
    (= event-id :brick-hit) :sfx/brick-hit
    (= event-id :life-lost) :sfx/life-lost
    (= event-id :level-clear) :sfx/level-clear
    (= event-id :game-over) :sfx/game-over
    (= event-id :victory) :sfx/victory
    :else nil))

(defn events->cues
  [events]
  (loop [remaining (seq events)
         out []]
    (if (seq remaining)
      (let [cue (event->cue (first remaining))]
        (recur (next remaining)
               (if cue (conj out cue) out)))
      out)))

(def ^:private loaded-tracks* (atom #{}))

(defn- ensure-cue-loaded!
  [cue-id]
  (when (not (contains? @loaded-tracks* cue-id))
    (ensure-sound!)
    (let [spec (sfx-spec cue-id)
          track-id (:track-id spec)
          steps (:steps spec)
          opts (:opts spec)]
      (when (and track-id steps)
        (let [track-bytes (tiny-fx.sound/compile-track steps opts)]
          (tiny-fx.sound-native/sound-load-track! track-id track-bytes)
          (swap! loaded-tracks* conj cue-id)))))
  nil)

(defn- play-cue!
  [cue-id]
  (let [spec (sfx-spec cue-id)]
    (when (map? spec)
      (ensure-cue-loaded! cue-id)
      (tiny-fx.sound-native/sound-play-sfx! (:track-id spec)))))

(defn play-events!
  [events]
  (loop [remaining (seq events)]
    (when (seq remaining)
      (let [cue (event->cue (first remaining))]
        (when cue (play-cue! cue))
        (recur (next remaining))))))
