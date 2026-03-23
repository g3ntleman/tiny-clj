(ns tiny-breakout.audio-compiler)

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

(defn- clamp-int
  [value min-value max-value fallback]
  (if (integer? value)
    (max min-value (min max-value value))
    fallback))

(defn- encode-varuint
  [n]
  (loop [v (if (integer? n) (max 0 n) 0)
         out []]
    (let [b (mod v 128)
          v2 (quot v 128)
          out2 (conj out (if (> v2 0) (+ b 128) b))]
      (if (> v2 0)
        (recur v2 out2)
        out2))))

(defn- step-frequency-hz
  [step]
  (let [notes (:notes step)
        hz (if (and (vector? notes)
                    (not (empty? notes)))
             (first notes)
             0)]
    (clamp-int hz 0 20000 0)))

(defn- step-duration-ms
  [step]
  (let [duration (:duration step)]
    (clamp-int duration 1 60000 1)))

(defn- step-gate-ms
  [duration-ms gate-percent]
  (max 20 (quot (* duration-ms gate-percent) 100)))

(defn compile-track-bytes
  "Compiles tiny-breakout's constrained SFX DSL into TRK1 bytes.
   Supported input:
   - single-channel NOTE_HZ tracks
   - integer :duration in ms
   - :gate-percent and one :volumes entry in opts"
  [steps opts]
  (let [volumes (:volumes opts)
        volume (if (and (vector? volumes) (integer? (first volumes)))
                 (clamp-int (first volumes) 0 255 180)
                 180)
        gate-percent (clamp-int (:gate-percent opts) 1 100 82)
        set-vol-ctrl (+ (* 1 16) 0)
        note-hz-ctrl (+ 128 (* 3 16) 0)
        end-ctrl (+ (* 2 16) 0)
        stream (loop [remaining (seq steps)
                      out [set-vol-ctrl volume]]
                 (if (seq remaining)
                   (let [step (first remaining)
                         hz (step-frequency-hz step)
                         duration-ms (step-duration-ms step)
                         gate-ms (step-gate-ms duration-ms gate-percent)
                         hz-lo (mod hz 256)
                         hz-hi (mod (quot hz 256) 256)]
                     (recur (next remaining)
                            (-> out
                                (conj note-hz-ctrl hz-lo hz-hi)
                                (into (encode-varuint gate-ms))
                                (into (encode-varuint duration-ms)))))
                   (conj out end-ctrl)))
        stream-len (count stream)
        header [84 82 75 49
                1
                0
                1
                0
                1 0
                60 0
                (mod stream-len 256)
                (mod (quot stream-len 256) 256)
                (mod (quot stream-len 65536) 256)
                (mod (quot stream-len 16777216) 256)
                0 0 0 0]
        all (into header stream)
        a (byte-array (count all))]
    (loop [i 0]
      (if (< i (count all))
        (do
          (aset a i (nth all i))
          (recur (+ i 1)))
        a))))

(defn compiled-cue-specs
  "Returns cue-id -> {:track-id kw :track-bytes byte-array}."
  []
  (reduce-kv
   (fn [out cue-id descriptor]
     (assoc out cue-id
            {:track-id (:track-id descriptor)
             :track-bytes (compile-track-bytes (:steps descriptor)
                                               (:opts descriptor))}))
   {}
   cue-dsl))
