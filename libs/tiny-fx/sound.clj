
(ns tiny-fx.sound
  (:require [tiny-fx.sound-native :as native]))

;; -----------------------------------------------------------------------------
;; Note helpers
;; -----------------------------------------------------------------------------

(def ^:private semitone-base-map
  {\C 0
   \D 2
   \E 4
   \F 5
   \G 7
   \A 9
   \B 11})

(defn- semitone-base [letter]
  (get semitone-base-map letter))

(defn- fail [message]
  (throw message))

(defn note->midi
  "Converts a note keyword to a MIDI note number (0..127).

   Supported note syntax:
   - :C4, :D5, :A3
   - sharps: :Cs4 or :C#4
   - flats: :Db4, :Bb3
   - rest: :REST

   For explicit Hz use an integer in :notes (see compile-track); integers are
   Hz, not MIDI.

   Pitch letters must be uppercase. Octaves are single-digit."
  [note-kw]
  (cond
    (= note-kw :REST) 0
    (not (keyword? note-kw))
    (fail "note->midi expects keyword like :G5, :Cs4/:Db4, :Bb3 or :REST")
    :else
    (let [s (name note-kw)
          n (count s)
          letter (let [c-str (subs s 0 1)]
                   (cond
                     (= c-str "C") \C
                     (= c-str "D") \D
                     (= c-str "E") \E
                     (= c-str "F") \F
                     (= c-str "G") \G
                     (= c-str "A") \A
                     (= c-str "B") \B
                     :else (fail "Invalid note letter")))
          accidental (if (= n 3) (subs s 1 2) "")
          octave-str (if (= n 3) (subs s 2 3) (subs s 1 2))
          octave (cond
                   (= octave-str "0") 0
                   (= octave-str "1") 1
                   (= octave-str "2") 2
                   (= octave-str "3") 3
                   (= octave-str "4") 4
                   (= octave-str "5") 5
                   (= octave-str "6") 6
                   (= octave-str "7") 7
                   (= octave-str "8") 8
                   (= octave-str "9") 9
                   :else (fail "Invalid octave"))
          base (semitone-base letter)
          semi (cond
                 (= accidental "#") (+ base 1)
                 (= accidental "s") (+ base 1)
                 (= accidental "b") (- base 1)
                 :else base)
          midi (+ (* (+ octave 1) 12) semi)]
      (if (or (nil? base) (< midi 0) (> midi 127))
        (fail "Invalid note keyword")
        midi))))

;; -----------------------------------------------------------------------------
;; trk1 encoding helpers
;; -----------------------------------------------------------------------------

(defn- varuint [n]
  (loop [v n out []]
    (let [b (mod v 128)
          v2 (quot v 128)
          b2 (if (> v2 0) (+ b 128) b)
          out2 (conj out b2)]
      (if (> v2 0) (recur v2 out2) out2))))

(defn- ->byte-array [xs]
  (let [a (byte-array (count xs))]
    (loop [i 0]
      (if (< i (count xs))
        (do
          (aset a i (nth xs i))
          (recur (+ i 1)))
        a))))

(defn- evt-set-vol [ch vol]
  ;; has_delay=0, event_type=1 (SET_VOL)
  [(+ (* 1 16) ch) vol])

(defn- validate-freq-hz [freq-hz]
  (when (or (not (integer? freq-hz)) (< freq-hz 0) (> freq-hz 20000))
    (fail "compile-track: frequency Hz must be integer 0..20000")))

(defn- maybe-append-delay [event-bytes has-delay delay-ms]
  (if has-delay
    (reduce conj event-bytes (varuint delay-ms))
    event-bytes))

(defn- evt-note [ch note-val gate-ms note-flags has-delay delay-ms]
  (let [hz-note? (integer? note-val)
        ex-note? (not (= note-flags 0))
        event-type (cond
                     hz-note? (if ex-note? 5 3)
                     ex-note? 4
                     :else 0)
        payload (if hz-note?
                  (do
                    (validate-freq-hz note-val)
                    [(mod note-val 256)
                     (mod (quot note-val 256) 256)])
                  [(note->midi note-val)])
        ctrl (+ (if has-delay 128 0) (* event-type 16) ch)
        base1 (reduce conj [ctrl] payload)
        base2 (reduce conj base1 (varuint gate-ms))
        base3 (if ex-note? (conj base2 note-flags) base2)]
    (maybe-append-delay base3 has-delay delay-ms)))

(defn- evt-end []
  ;; has_delay=0, event_type=2 (END), channel=0
  [(* 2 16)])

(defn- trk1-header [stream-len channel-count]
  [84 82 75 49      ;; "TRK1"
   1                ;; version
   0                ;; flags
   channel-count
   0                ;; reserved
   1 0              ;; tpq=1
   60 0             ;; bpm=60 => 1 tick = 1 ms
   (mod stream-len 256)
   (mod (quot stream-len 256) 256)
   (mod (quot stream-len 65536) 256)
   (mod (quot stream-len 16777216) 256)
   0 0 0 0])        ;; crc32 unused

(defn- normalize-notes [notes channel-count]
  (loop [i 0 out []]
    (if (< i channel-count)
      (recur (+ i 1) (conj out (or (nth notes i nil) :REST)))
      out)))

(defn- infer-channel-count [steps]
  (loop [s steps best 1]
    (if (empty? s)
      best
      (let [step (first s)
            notes (get step :notes)
            backing (get step :backing)
            has-melody (not (nil? (get step :melody)))
            n (if (nil? notes)
                (if (or has-melody (not (nil? backing)))
                  (+ 1 (count (or backing [])))
                  1)
                (count notes))
            best2 (if (> n best) n best)]
        (recur (rest s) best2)))))

(defn- build-initial-vol-events [channel-count volumes]
  (loop [ch 0 ev []]
    (if (< ch channel-count)
      (let [vol (or (nth volumes ch nil) 180)
            ev2 (reduce conj ev (evt-set-vol ch vol))]
        (recur (+ ch 1) ev2))
      ev)))

(def ^:private duration-fraction-map
  {:w  [4 1]
   :h  [2 1]
   :q  [1 1]
   :e  [1 2]
   :s  [1 4]
   :t  [1 8]
   :dh [3 1]
   :dq [3 2]
   :de [3 4]
   :ds [3 8]
   :qt [2 3]
   :et [1 3]
   :st [1 6]})

(def ^:private bend-default-segment-ms 15)
(def ^:private bend-max-segments 16)
(def ^:private noise-default-segment-ms 12)
(def ^:private noise-max-segments 24)

(defn- duration->ms [dur tempo-bpm]
  (cond
    (integer? dur)
    (if (>= dur 1)
      dur
      (fail "Step duration integer must be >= 1 ms"))
    (keyword? dur)
    (let [frac (get duration-fraction-map dur)]
      (if (nil? frac)
        (fail "Unknown musical duration keyword")
        (let [quarter-ms (max 1 (quot 60000 tempo-bpm))
              num (nth frac 0)
              den (nth frac 1)]
          ;; Round to nearest integer ms: (x + den/2) / den
          (max 1 (quot (+ (* quarter-ms num) (quot den 2)) den)))))
    :else
    (fail "Step duration must be a musical keyword (:q, :e, :dq, ...) or integer milliseconds")))

(defn- step-duration-spec [step ex-prefix]
  (let [rest-dur (get step :rest)
        has-rest (not (nil? rest-dur))
        duration (get step :duration)]
    (when (contains? step :dur)
      (fail (str ex-prefix " step must use :duration instead of legacy :dur")))
    (when (and has-rest (not (nil? duration)))
      (fail (str ex-prefix " step must not use both :duration and :rest")))
    (if has-rest rest-dur (or duration :q))))

(defn- resolve-tempo-bpm [steps opts ex-prefix]
  (let [opts* (or opts {})
        tempo-provided (contains? opts* :tempo-bpm)
        tempo-bpm (get opts* :tempo-bpm)
        tempo-required (loop [s steps]
                         (if (empty? s)
                           false
                           (if (keyword? (step-duration-spec (first s) ex-prefix))
                             true
                             (recur (rest s)))))]
    (when (and (not tempo-provided) tempo-required)
      (fail (str ex-prefix ": :tempo-bpm is required when using musical duration keywords")))
    (when tempo-provided
      (when (or (not (integer? tempo-bpm)) (< tempo-bpm 20) (> tempo-bpm 400))
        (fail (str ex-prefix " :tempo-bpm must be in 20..400"))))
    tempo-bpm))

(defn- note-value->hz [note-val ex-prefix]
  (if (integer? note-val)
    (do
      (when (or (< note-val 0) (> note-val 20000))
        (fail (str ex-prefix " modulated channel frequency must be integer 0..20000")))
      note-val)
    (fail (str ex-prefix " modulated channels currently require integer Hz values"))))

(defn- bounded-segment-count [duration-ms default-segment-ms max-segments]
  (let [approx (quot (+ duration-ms (- default-segment-ms 1)) default-segment-ms)
        capped (min max-segments (max 1 approx))]
    (min duration-ms capped)))

(defn- split-duration-evenly [duration-ms segment-count]
  (let [base (quot duration-ms segment-count)
        extra (mod duration-ms segment-count)]
    (loop [i 0 out []]
      (if (< i segment-count)
        (recur (+ i 1)
               (conj out (+ base (if (< i extra) 1 0))))
        out))))

(defn- bend-interpolate-hz [start-hz end-hz segment-index segment-count]
  (if (<= segment-count 1)
    end-hz
    (let [den (- segment-count 1)
          num (+ (* start-hz (- den segment-index))
                 (* end-hz segment-index))]
      (quot (+ num (quot den 2)) den))))

(defn- bend-targets-for-step [step channel-count ex-prefix]
  (let [bend (get step :bend)
        has-rest (not (nil? (get step :rest)))]
    (if (nil? bend)
      nil
      (do
        (when has-rest
          (fail (str ex-prefix " step must not use :bend with :rest")))
        (when (not (vector? bend))
          (fail (str ex-prefix " :bend must be a vector matching :notes channels")))
        (when (> (count bend) channel-count)
          (fail (str ex-prefix " :bend must not be wider than the resolved channel count")))
        (loop [i 0 out []]
          (if (< i channel-count)
            (recur (+ i 1) (conj out (nth bend i nil)))
            out))))))

(defn- any-non-nil? [xs]
  (loop [i 0]
    (if (< i (count xs))
      (if (nil? (nth xs i))
        (recur (+ i 1))
        true)
      false)))

(defn- any-true? [xs]
  (if (nil? xs)
    false
    (loop [i 0]
      (if (< i (count xs))
        (if (= true (nth xs i))
          true
          (recur (+ i 1)))
        false))))

(defn- validate-noise-flag [step ex-prefix]
  (let [noise (get step :noise)]
    (when (and (not (nil? noise))
               (not (= noise true))
               (not (= noise false)))
      (fail (str ex-prefix " :noise must be boolean")))
    noise))

(defn- validate-articulation [step ex-prefix]
  (let [articulation (get step :articulation)]
    (when (and (not (nil? articulation))
               (not (= articulation :normal))
               (not (= articulation :legato))
               (not (= articulation :staccato)))
      (fail (str ex-prefix " :articulation must be :normal, :legato or :staccato")))
    (or articulation :normal)))

(defn- validate-rearticulate [step ex-prefix]
  (let [rearticulate (get step :rearticulate)]
    (when (and (not (nil? rearticulate))
               (not (= rearticulate true))
               (not (= rearticulate false)))
      (fail (str ex-prefix " :rearticulate must be boolean")))
    (= rearticulate true)))

(defn- generic-noise-mask-for-step [step channel-count ex-prefix]
  (let [noise (validate-noise-flag step ex-prefix)]
    (if (= noise true)
      (do
        (when (not (= channel-count 1))
          (fail (str ex-prefix " :noise in generic :notes mode currently requires exactly 1 channel")))
        [true])
      nil)))

(defn- noise-mask-for-step [step channel-count ex-prefix]
  (let [mask (get step :noise-channels)]
    (if (nil? mask)
      (generic-noise-mask-for-step step channel-count ex-prefix)
      (do
        (when (not (vector? mask))
          (fail (str ex-prefix " internal :noise-channels marker must be a vector")))
        (when (not (= (count mask) channel-count))
          (fail (str ex-prefix " internal :noise-channels marker must match channel count")))
        mask))))

(defn- modulation-segment-count [duration-ms bend-targets noise-mask]
  (if (any-true? noise-mask)
    (bounded-segment-count duration-ms noise-default-segment-ms noise-max-segments)
    (if (any-non-nil? bend-targets)
      (bounded-segment-count duration-ms bend-default-segment-ms bend-max-segments)
      1)))

(defn- noise-radius-hz [center-hz]
  (if (<= center-hz 0)
    0
    (max 6 (min 48 (quot center-hz 10)))))

(defn- perturb-hz-for-noise [center-hz segment-index channel-index]
  (if (<= center-hz 0)
    0
    (let [radius (noise-radius-hz center-hz)
          raw (mod (+ (* (+ segment-index 1) 73)
                      (* (+ channel-index 1) 19)
                      (* center-hz 7))
                   9)
          centered (- raw 4)
          offset (quot (* centered radius) 4)
          hz (+ center-hz offset)]
      (max 1 (min 20000 hz)))))

(defn- prepare-modulation-frequencies [notes bend-targets noise-mask channel-count]
  (loop [ch 0 start-hzs [] end-hzs []]
    (if (< ch channel-count)
      (let [note-val (nth notes ch)
            target-val (if (nil? bend-targets) nil (nth bend-targets ch nil))
            noisy? (if (nil? noise-mask) false (= true (nth noise-mask ch false)))]
        (if (or noisy? (not (nil? target-val)))
          (let [start-hz (note-value->hz note-val "compile-track")
                end-hz (if (nil? target-val)
                         nil
                         (note-value->hz target-val "compile-track"))]
            (recur (+ ch 1)
                   (conj start-hzs start-hz)
                   (conj end-hzs end-hz)))
          (recur (+ ch 1)
                 (conj start-hzs nil)
                 (conj end-hzs nil))))
      {:start-hzs start-hzs
       :end-hzs end-hzs})))

(defn- expand-modulated-step [step channel-count tempo-bpm]
  (let [bend-targets (bend-targets-for-step step channel-count "compile-track")
        noise-mask (noise-mask-for-step step channel-count "compile-track")
        has-modulation (or (any-non-nil? bend-targets) (any-true? noise-mask))]
    (if (not has-modulation)
      [step]
      (let [duration-ms (duration->ms (step-duration-spec step "compile-track") tempo-bpm)
            notes (normalize-notes (or (get step :notes) []) channel-count)
            segment-count (modulation-segment-count duration-ms bend-targets noise-mask)
            segment-durations (split-duration-evenly duration-ms segment-count)
            prepared (prepare-modulation-frequencies notes bend-targets noise-mask channel-count)
            start-hzs (get prepared :start-hzs)
            end-hzs (get prepared :end-hzs)]
        (loop [seg 0 out []]
          (if (< seg segment-count)
            (let [segment-notes (loop [ch 0 acc []]
                                  (if (< ch channel-count)
                                    (let [note-val (nth notes ch)
                                          start-hz (nth start-hzs ch nil)
                                          end-hz (nth end-hzs ch nil)
                                          noisy? (if (nil? noise-mask) false (= true (nth noise-mask ch false)))]
                                      (if (nil? start-hz)
                                        (recur (+ ch 1) (conj acc note-val))
                                        (let [center-hz (if (nil? end-hz)
                                                          start-hz
                                                          (bend-interpolate-hz start-hz end-hz seg segment-count))
                                              out-hz (if noisy?
                                                       (perturb-hz-for-noise center-hz seg ch)
                                                       center-hz)]
                                          (recur (+ ch 1) (conj acc out-hz)))))
                                    acc))
                  segment-step (assoc step
                                      :notes segment-notes
                                      :duration (nth segment-durations seg))]
              (recur (+ seg 1) (conj out segment-step)))
            out))))))

(defn- expand-modulated-steps [steps opts tempo-bpm]
  (let [channel-count (or (get opts :channel-count) (infer-channel-count steps))]
    (loop [s steps out []]
      (if (empty? s)
        out
        (recur (rest s) (into out (expand-modulated-step (first s) channel-count tempo-bpm)))))))

(defn- ensure-no-melody-backing-bend [steps]
  (loop [s steps]
    (when (not (empty? s))
      (let [step (first s)]
        (when (and (or (not (nil? (get step :melody)))
                       (not (nil? (get step :backing))))
                   (contains? step :bend))
          (fail "compile-track: :bend is not yet supported in melody/backing mode"))
        (recur (rest s))))))

(defn- normalize-playback-config [steps opts ex-prefix]
  (let [has-melody-backing (loop [s steps]
                             (if (empty? s)
                               false
                               (let [step (first s)]
                                 (if (or (not (nil? (get step :melody)))
                                         (not (nil? (get step :backing))))
                                   true
                                   (recur (rest s))))))
        _ (when has-melody-backing
            (ensure-no-melody-backing-bend steps))
        opts* (if has-melody-backing
                (compile-opts-melody-backing steps opts)
                opts)
        steps* (if has-melody-backing
                 (melody-backing->steps steps opts)
                 steps)
        tempo-bpm (resolve-tempo-bpm steps* opts* ex-prefix)
        steps** (expand-modulated-steps steps* opts* tempo-bpm)]
    {:steps steps**
     :opts opts*
     :tempo-bpm tempo-bpm}))

(defn- track-duration-ms* [steps tempo-bpm]
  (loop [s steps total 0]
    (if (empty? s)
      total
      (let [step (first s)
            dur (duration->ms (step-duration-spec step "compile-track") tempo-bpm)]
        (recur (rest s) (+ total dur))))))

(defn- normal-gate-ms [dur gate-percent]
  (max 20 (quot (* dur gate-percent) 100)))

(defn- step-gate-ms [dur gate-percent articulation]
  (cond
    (= articulation :legato) dur
    (= articulation :staccato) (max 20 (quot (normal-gate-ms dur gate-percent) 2))
    :else (normal-gate-ms dur gate-percent)))

(def ^:private default-envelope
  [1.0 1.0 0.2])

(defn- compute-note-flags [articulation rearticulate note-val next-note-val]
  (let [legato? (and (= articulation :legato)
                     (not (= note-val :REST))
                     (not (nil? next-note-val))
                     (not (= next-note-val :REST)))
        retrigger? (and rearticulate (not (= note-val :REST)))]
    (+ (if legato? 1 0)
       (if retrigger? 2 0))))

(defn track-duration-ms
  "Returns the total playback time in milliseconds for the given step sequence.

   Accepts the same DSL and options as compile-track, including melody/backing
   shorthand. A missing :duration defaults to :q. :tempo-bpm is required when
   any step uses a musical duration keyword."
  [steps opts]
  (let [cfg (normalize-playback-config steps opts "track-duration-ms")
        steps* (get cfg :steps)
        tempo-bpm (get cfg :tempo-bpm)]
    (track-duration-ms* steps* tempo-bpm)))

(defn- compile-track-envelope-levels [opts]
  (let [envelope (or (get opts :envelope) default-envelope)]
    (when (not (vector? envelope))
      (fail "compile-track :envelope must be a vector"))
    (when (or (< (count envelope) 1) (> (count envelope) 8))
      (fail "compile-track :envelope must contain 1..8 levels"))
    (loop [i 0 out []]
      (if (< i (count envelope))
        (let [level (nth envelope i)]
          (when (or (not (number? level)) (< level 0) (> level 1.0))
            (fail "compile-track :envelope levels must be numbers in 0.0..1.0"))
          (let [scaled-level (quot (+ (* level 256) 0.5) 1)
                byte-level (if (> scaled-level 255) 255 scaled-level)]
            (recur (+ i 1) (conj out byte-level))))
        out))))

(defn- compile-track-init-events [channel-count volumes envelope-levels]
  (let [base-events (build-initial-vol-events channel-count volumes)
        envelope-event (reduce conj [(+ (* 6 16) 0) (count envelope-levels)]
                               envelope-levels)]
    (into envelope-event base-events)))

(defn- step-next-notes [next-step channel-count]
  (if (nil? next-step)
    nil
    (if (not (nil? (get next-step :rest)))
      (normalize-notes [] channel-count)
      (normalize-notes (or (get next-step :notes) []) channel-count))))

(defn- compile-track-step-events [ev step next-step channel-count tempo-bpm gate-percent]
  (let [dur (duration->ms (step-duration-spec step "compile-track") tempo-bpm)
        articulation (validate-articulation step "compile-track")
        rearticulate (validate-rearticulate step "compile-track")
        gate (step-gate-ms dur gate-percent articulation)
        has-rest (not (nil? (get step :rest)))
        notes (if has-rest
                (normalize-notes [] channel-count)
                (normalize-notes (or (get step :notes) []) channel-count))
        next-notes (step-next-notes next-step channel-count)]
    (loop [ch 0 acc ev]
      (if (< ch channel-count)
        (let [n (nth notes ch)
              next-n (if (nil? next-notes) nil (nth next-notes ch))
              note-flags (compute-note-flags articulation rearticulate n next-n)
              has-delay (= ch (- channel-count 1))
              delay (if has-delay dur 0)
              acc2 (reduce conj acc (evt-note ch n gate note-flags has-delay delay))]
          (recur (+ ch 1) acc2))
        acc))))

(defn- compile-track*
  [steps opts tempo-bpm]
  (let [inferred (infer-channel-count steps)
        channel-count (or (get opts :channel-count) inferred)
        _ (when (or (< channel-count 1) (> channel-count 16))
            (fail "compile-track: :channel-count must be in 1..16"))
        gate-percent (or (get opts :gate-percent) 82)
        envelope-levels (compile-track-envelope-levels opts)
        volumes (or (get opts :volumes) [200 180 160 140])
        events0 (compile-track-init-events channel-count volumes envelope-levels)
        eventsN (loop [s steps ev events0]
                  (if (empty? s)
                    (reduce conj ev (evt-end))
                    (let [step (first s)
                          next-step (first (rest s))
                          ev2 (compile-track-step-events ev step next-step channel-count tempo-bpm gate-percent)]
                      (recur (rest s) ev2))))
        stream-len (count eventsN)
        all (reduce conj (trk1-header stream-len channel-count) eventsN)]
    (->byte-array all)))

(defn compile-track
  "Compiles generic or melody/backing steps into TRK1 bytes.
   Supports duration keywords, articulation, rearticulation and generic bend/noise options."
  [steps opts]
  (let [cfg (normalize-playback-config steps opts "compile-track")
        steps* (get cfg :steps)
        opts* (get cfg :opts)
        tempo-bpm (get cfg :tempo-bpm)]
    (compile-track* steps* opts* tempo-bpm)))

(defn- prepare-track-playback [steps opts]
  (let [cfg (normalize-playback-config steps opts "compile-track")
        steps* (get cfg :steps)
        opts* (get cfg :opts)
        tempo-bpm (get cfg :tempo-bpm)]
    {:track-bytes (compile-track* steps* opts* tempo-bpm)
     :duration-ms (track-duration-ms* steps* tempo-bpm)}))

(defn play-steps!
  "Compiles, loads and plays steps once.
   Options map is passed to compile-track.
   track-id is an opaque runtime id; keywords are the usual choice.
   Returns {:status :playing|:stopped :duration-ms <int>}."
  [track-id steps opts]
  (let [prepared (prepare-track-playback steps opts)
        track-bytes (get prepared :track-bytes)
        duration-ms (get prepared :duration-ms)]
    (native/sound-load-track! track-id track-bytes)
    {:status (if (native/sound-play-music! track-id 1) :playing :stopped)
     :duration-ms duration-ms}))

(defn play-sfx!
  "Compiles, loads and triggers steps as a one-shot sound effect.
   Options map is passed to compile-track.
   Returns {:status :playing|:dropped :duration-ms <int>}."
  [track-id steps opts]
  (let [prepared (prepare-track-playback steps opts)
        track-bytes (get prepared :track-bytes)
        duration-ms (get prepared :duration-ms)]
    (native/sound-load-track! track-id track-bytes)
    {:status (if (native/sound-play-sfx! track-id) :playing :dropped)
     :duration-ms duration-ms}))

;; Melody + backing transformation helper.
;; Input step format:
;; {:melody :G5 :backing [:D5 :Bb4 ...] :duration :q}
;; {:rest :e}  ;; full rest for melody and backing channels

(def ^:private default-melody-volume 220)
(def ^:private default-backing-volumes [160 140 120 110 100])

(defn- ensure-role-opts-map [role-name role-opts]
  (when (and (not (nil? role-opts))
             (not (map? role-opts)))
    (fail "Melody/backing option groups must be maps"))
  role-opts)

(defn- ensure-role-volumes-vector [role-name volumes]
  (when (and (not (nil? volumes))
             (not (vector? volumes)))
    (fail "Channel volumes must be a vector"))
  volumes)

(defn- ensure-role-volume-number [role-name volume]
  (when (and (not (nil? volume))
             (not (number? volume)))
    (fail "Channel volume must be a number"))
  volume)

(defn- ensure-role-channels [role-name channels]
  (when (and (not (nil? channels))
             (or (not (integer? channels)) (< channels 1) (> channels 16)))
    (fail "Role :channels must be an integer in 1..16"))
  channels)

(defn- any-step-has-backing? [steps]
  (loop [s steps]
    (if (empty? s)
      false
      (if (not (nil? (get (first s) :backing)))
        true
        (recur (rest s))))))

(defn- widest-backing-width [steps]
  (loop [s steps best 0]
    (if (empty? s)
      best
      (let [step (first s)
            backing (or (get step :backing) [])
            best2 (if (> (count backing) best) (count backing) best)]
        (recur (rest s) best2)))))

(defn- reject-legacy-melody-backing-opts [base melody-opts backing-opts]
  (when (contains? base :channel-count)
    (fail "Melody/backing options must not use top-level legacy :channel-count; use role :channels instead"))
  (when (contains? base :volumes)
    (fail "Melody/backing options must not use top-level :volumes; use :melody/:backing role volumes instead"))
  (when (contains? base :melody-vol)
    (fail "Melody/backing options must not use legacy :melody-vol; use :melody {:volume ...}"))
  (when (contains? base :backing-volumes)
    (fail "Melody/backing options must not use legacy :backing-volumes; use :backing {:volumes ...}"))
  (when (contains? melody-opts :volume-levels)
    (fail "Melody options must not use legacy :volume-levels; use :volume or :volumes"))
  (when (contains? backing-opts :volume-levels)
    (fail "Backing options must not use legacy :volume-levels; use :volume or :volumes")))

(defn- resolve-melody-channels [melody-opts]
  (let [channels (or (ensure-role-channels :melody (get melody-opts :channels)) 1)]
    (when (not (= channels 1))
      (fail "Melody currently supports exactly 1 channel"))
    channels))

(defn- resolve-backing-channels [steps backing-opts]
  (let [explicit (ensure-role-channels :backing (get backing-opts :channels))
        inferred (widest-backing-width steps)
        has-backing-role (or (any-step-has-backing? steps)
                             (not (empty? backing-opts)))]
    (or explicit
        (if has-backing-role
          (max 1 inferred)
          0))))

(defn- expand-default-backing-volumes [channels]
  (loop [i 0 out []]
    (if (< i channels)
      (recur (+ i 1) (conj out (or (nth default-backing-volumes i nil) 160)))
      out)))

(defn- resolve-role-volumes [role-name channels role-opts]
  (let [volume (ensure-role-volume-number role-name (get role-opts :volume))
        volumes (ensure-role-volumes-vector role-name (get role-opts :volumes))]
    (when (and (not (nil? volume)) (not (nil? volumes)))
      (fail "Role options must not use both :volume and :volumes"))
    (when (and (not (nil? volumes)) (not (= (count volumes) channels)))
      (fail "Role :volumes must match the resolved channel count exactly"))
    (cond
      (= channels 0) []
      (not (nil? volumes)) volumes
      (not (nil? volume))
      (loop [i 0 out []]
        (if (< i channels)
          (recur (+ i 1) (conj out volume))
          out))
      (= role-name :melody) [default-melody-volume]
      :else (expand-default-backing-volumes channels))))

(defn- compile-opts-melody-backing [steps opts]
  (let [base (or opts {})
        melody-opts (or (ensure-role-opts-map :melody (get base :melody)) {})
        backing-opts (or (ensure-role-opts-map :backing (get base :backing)) {})
        _ (reject-legacy-melody-backing-opts base melody-opts backing-opts)
        melody-channels (resolve-melody-channels melody-opts)
        backing-channels (resolve-backing-channels steps backing-opts)
        channel-count (+ melody-channels backing-channels)
        _ (when (or (< channel-count 1) (> channel-count 16))
            (fail "compile-track-melody-backing: :channel-count must be in 1..16"))
        melody-volumes (resolve-role-volumes :melody melody-channels melody-opts)
        backing-volumes (resolve-role-volumes :backing backing-channels backing-opts)
        gate-percent (or (get base :gate-percent) 82)
        volumes (into melody-volumes backing-volumes)
        opts1 (dissoc base :melody :backing)
        opts2 (assoc opts1 :channel-count channel-count)
        opts3 (assoc opts2 :volumes volumes)]
    (assoc opts3 :gate-percent gate-percent)))

(defn- expand-backing-auto [backing target-count]
  (if (<= target-count 0)
    []
    (if (empty? backing)
      (loop [i 0 out []]
        (if (< i target-count)
          (recur (+ i 1) (conj out :REST))
          out))
      (loop [i 0 out []]
        (if (< i target-count)
          (recur (+ i 1) (conj out (nth backing (mod i (count backing)))))
          out)))))

(defn- melody-backing->steps
  "Normalizes melody+backing steps into generic {:notes [...] :duration ...} steps.
   Backing is automatically expanded to all available backing channels.
   Melody always uses 1 channel.
   Backing channel count comes from :backing {:channels N} or is inferred from the
   widest backing step.
   Supports pause shorthand per step: {:rest :e}.
   Empty backing expands to rests; short backing patterns repeat to fill the
   available backing channels."
  [steps opts]
  (let [opts2 (compile-opts-melody-backing steps opts)
        channel-count (or (get opts2 :channel-count) 1)
        backing-count (max 0 (- channel-count 1))
        steps2 (loop [s steps out []]
                 (if (empty? s)
                   out
                   (let [step (first s)
                         rest-dur (get step :rest)
                         has-rest (not (nil? rest-dur))
                         noise (validate-noise-flag step "compile-track-melody-backing")
                         melody (if has-rest :REST (or (get step :melody) :REST))
                         dur (step-duration-spec step "compile-track-melody-backing")
                         backing-src (if has-rest [] (or (get step :backing) []))
                         backing (expand-backing-auto backing-src backing-count)
                         notes (into [melody] backing)
                         noise-channels (if (= noise true)
                                          (do
                                            (when (<= backing-count 0)
                                              (fail "compile-track-melody-backing: :noise requires at least one backing channel"))
                                            (loop [i 0 mask [false]]
                                              (if (< i backing-count)
                                                (recur (+ i 1) (conj mask true))
                                                mask)))
                                          nil)
                         step2-base {:notes notes
                                     :duration dur}
                         step2-base (if (nil? noise-channels)
                                      step2-base
                                      (assoc step2-base :noise-channels noise-channels))
                         step2 (cond-> step2-base
                                 (contains? step :articulation)
                                 (assoc :articulation (get step :articulation))
                                 (contains? step :rearticulate)
                                 (assoc :rearticulate (get step :rearticulate)))]
                    (recur (rest s) (conj out step2)))))]
    steps2))

