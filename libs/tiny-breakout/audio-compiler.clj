(ns tiny-breakout.audio-compiler
  (:require [tiny-fx.trk1 :as trk1]))

(def ^:private cue-dsl
  {:sfx/paddle-hit
   {:track-id :tiny-breakout/paddle-hit
    :steps [{:notes [1040] :duration 20}
            {:notes [1320] :duration 24}]
    :opts {:channel-count 1 :gate-percent 72 :volumes [180]}}

   :sfx/brick-hit
   {:track-id :tiny-breakout/brick-hit
    :steps [{:notes [1480] :duration 16}
            {:notes [1720] :duration 16}
            {:notes [1960] :duration 18}]
    :opts {:channel-count 1 :gate-percent 70 :volumes [200]}}

   :sfx/life-lost
   {:track-id :tiny-breakout/life-lost
    :steps [{:notes [880] :duration 32}
            {:notes [660] :duration 36}
            {:notes [440] :duration 54}]
    :opts {:channel-count 1 :gate-percent 78 :volumes [220]}}

   :sfx/level-clear
   {:track-id :tiny-breakout/level-clear
    :steps [{:notes [880] :duration 24}
            {:notes [1320] :duration 24}
            {:notes [1760] :duration 36}]
    :opts {:channel-count 1 :gate-percent 76 :volumes [210]}}

   :sfx/game-over
   {:track-id :tiny-breakout/game-over
    :steps [{:notes [740] :duration 28}
            {:notes [554] :duration 36}
            {:notes [392] :duration 60}]
    :opts {:channel-count 1 :gate-percent 80 :volumes [220]}}

   :sfx/victory
   {:track-id :tiny-breakout/victory
    :steps [{:notes [1040] :duration 20}
            {:notes [1318] :duration 20}
            {:notes [1568] :duration 24}
            {:notes [2093] :duration 48}]
    :opts {:channel-count 1 :gate-percent 74 :volumes [210]}}

   :sfx/wall-hit
   {:track-id :tiny-breakout/wall-hit
    :steps [{:notes [660] :duration 16}
            {:notes [440] :duration 18}]
    :opts {:channel-count 1 :gate-percent 68 :volumes [140]}}})

(defn compile-track-bytes
  "Compiles tiny-breakout's constrained SFX DSL into TRK1 bytes.
   Supported input:
   - single-channel NOTE_HZ tracks
   - integer :duration in ms
   - :gate-percent and one :volumes entry in opts"
  [steps opts]
  (trk1/compile-simple-note-hz-track steps opts))

