R"TINY_SND_CMP(
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
  "Converts a note keyword to a MIDI note number.

   Supported note syntax:
   - :C4, :D5, :A3
   - sharps: :Cs4 or :C#4
   - flats: :Db4, :Bb3
   - rest: :REST

   Pitch letters must be uppercase. Octaves are single-digit."
  [note-kw]
  (cond
    (= note-kw :REST) 0
    (not (keyword? note-kw))
    (fail "note->midi expects keyword like :G5, :Cs4/:Db4, :Bb3 or :REST")
    :else
    (let [s (name note-kw)
          n (count s)
          letter (read-string (str "\\" (subs s 0 1)))
          accidental (if (= n 3) (subs s 1 2) "")
          octave-str (if (= n 3) (subs s 2 3) (subs s 1 2))
          octave (read-string octave-str)
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

(defn- evt-note [ch note-kw gate-ms has-delay delay-ms]
  ;; has_delay bit in bit7, event_type=0 (NOTE)
  (let [ctrl (+ (if has-delay 128 0) (* 0 16) ch)
        base [ctrl (note->midi note-kw)]
        base2 (reduce conj base (varuint gate-ms))]
    (if has-delay
      (reduce conj base2 (varuint delay-ms))
      base2)))

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

(defn- duration->ms [dur tempo-bpm]
  (if (keyword? dur)
    (let [frac (get duration-fraction-map dur)]
      (if (nil? frac)
        (fail "Unknown musical duration keyword")
        (let [quarter-ms (max 1 (quot 60000 tempo-bpm))
              num (nth frac 0)
              den (nth frac 1)]
          ;; Round to nearest integer ms: (x + den/2) / den
          (max 1 (quot (+ (* quarter-ms num) (quot den 2)) den)))))
    (fail "Step duration must be a musical keyword (:q, :e, :dq, ...)")))

(defn- step-has-melody-backing? [step]
  (or (not (nil? (get step :melody)))
      (not (nil? (get step :backing)))))

(defn- step-duration-spec [step ex-prefix]
  (let [rest-dur (get step :rest)
        has-rest (not (nil? rest-dur))
        duration (get step :duration)]
    (when (contains? step :dur)
      (fail (str ex-prefix " step must use :duration instead of legacy :dur")))
    (when (and has-rest (not (nil? duration)))
      (fail (str ex-prefix " step must not use both :duration and :rest")))
    (if has-rest rest-dur (or duration :q))))

(defn- normalize-playback-config [steps opts]
  (let [has-melody-backing (loop [s steps]
                             (if (empty? s)
                               false
                               (let [step (first s)]
                                 (if (step-has-melody-backing? step)
                                   true
                                   (recur (rest s))))))
        opts* (if has-melody-backing
                (compile-opts-melody-backing steps opts)
                opts)
        steps* (if has-melody-backing
                 (melody-backing->steps steps opts)
                 steps)]
    {:steps steps*
     :opts opts*}))

(defn- track-duration-ms* [steps opts]
  (let [tempo-bpm (or (get opts :tempo-bpm) 120)]
    (loop [s steps total 0]
      (if (empty? s)
        total
        (let [step (first s)
              dur (duration->ms (step-duration-spec step "compile-track") tempo-bpm)]
          (recur (rest s) (+ total dur)))))))

(defn track-duration-ms
  "Returns the total playback time in milliseconds for the given step sequence.

   Accepts the same DSL and options as compile-track, including melody/backing
   shorthand. A missing :duration defaults to :q."
  [steps opts]
  (let [cfg (normalize-playback-config steps opts)
        steps* (get cfg :steps)
        opts* (get cfg :opts)]
    (track-duration-ms* steps* opts*)))

(defn- compile-track*
  [steps opts]
  (let [inferred (infer-channel-count steps)
        channel-count (or (get opts :channel-count) inferred)
        _ (when (or (< channel-count 1) (> channel-count 16))
            (fail "compile-track: :channel-count must be in 1..16"))
        tempo-bpm (or (get opts :tempo-bpm) 120)
        _ (when (or (< tempo-bpm 20) (> tempo-bpm 400))
            (fail "compile-track: :tempo-bpm must be in 20..400"))
        gate-percent (or (get opts :gate-percent) 82)
        volumes (or (get opts :volumes) [200 180 160 140])
        events0 (build-initial-vol-events channel-count volumes)
        eventsN (loop [s steps ev events0]
                  (if (empty? s)
                    (reduce conj ev (evt-end))
                    (let [step (first s)
                          dur (duration->ms (step-duration-spec step "compile-track") tempo-bpm)
                          gate (max 20 (quot (* dur gate-percent) 100))
                          has-rest (not (nil? (get step :rest)))
                          notes (if has-rest
                                  (normalize-notes [] channel-count)
                                  (normalize-notes (or (get step :notes) []) channel-count))
                          ev2 (loop [ch 0 acc ev]
                                (if (< ch channel-count)
                                  (let [n (nth notes ch)
                                        has-delay (= ch (- channel-count 1))
                                        delay (if has-delay dur 0)
                                        acc2 (reduce conj acc (evt-note ch n gate has-delay delay))]
                                    (recur (+ ch 1) acc2))
                                  acc))]
                      (recur (rest s) ev2))))
        stream-len (count eventsN)
        all (reduce conj (trk1-header stream-len channel-count) eventsN)]
    (->byte-array all)))

(defn compile-track
  "Generic N-voice compiler (1..16 channels).
   Step format:
   {:notes [:G5 :D5 ...] :duration :q}
   {:melody :G5 :backing [:D5 :Bb4 ...] :duration :q} ;; backing auto-fills channels
   {:rest :e}                     ;; full rest for all channels
   {:notes [:G5]}                 ;; :duration defaults to :q
   Options:
   Generic {:notes ...} options:
   {:channel-count 1..16
    :volumes [220 160 140 ...]
    :gate-percent 82
    :tempo-bpm 120}
   Melody/backing options:
   {:melody {:volume 220}
    :backing {:channels 2 :volumes [160 140]}
    :gate-percent 82
    :tempo-bpm 120}
   Rules:
   - :duration and :rest are mutually exclusive on the same step
   - missing notes are padded with :REST up to :channel-count
   - if :channel-count is omitted it is inferred from the widest step
   - if any step uses :melody or :backing, the sequence is normalized through
     melody-backing->steps
   - in melody/backing mode, role options own the channel layout; do not pass
     top-level :channel-count or :volumes there
   - melody defaults to 1 channel with volume 220
   - backing channel count is inferred from the widest backing step, defaulting
     to 1 when backing role options are present without wider backing data
   Supported duration keywords:
   :w :h :q :e :s :t :dh :dq :de :ds :qt :et :st"
  [steps opts]
  (let [cfg (normalize-playback-config steps opts)
        steps* (get cfg :steps)
        opts* (get cfg :opts)]
    (compile-track* steps* opts*)))

(defn- play-result->status [ok]
  (if ok :playing :stopped))

(defn- play-sfx-result->status [ok]
  (if ok :playing :dropped))

(defn- prepare-track-playback [steps opts]
  (let [cfg (normalize-playback-config steps opts)
        steps* (get cfg :steps)
        opts* (get cfg :opts)]
    {:track-bytes (compile-track* steps* opts*)
     :duration-ms (track-duration-ms* steps* opts*)}))

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
    {:status (play-result->status (native/sound-play-music! track-id 1))
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
    {:status (play-sfx-result->status (native/sound-play-sfx! track-id))
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

(defn- step-has-backing? [step]
  (not (nil? (get step :backing))))

(defn- any-step-has-backing? [steps]
  (loop [s steps]
    (if (empty? s)
      false
      (if (step-has-backing? (first s))
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

(defn melody-backing->steps
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
                         melody (if has-rest :REST (or (get step :melody) :REST))
                         dur (step-duration-spec step "compile-track-melody-backing")
                         backing-src (if has-rest [] (or (get step :backing) []))
                         backing (expand-backing-auto backing-src backing-count)
                         notes (into [melody] backing)]
                    (recur (rest s) (conj out {:notes notes :duration dur})))))]
    steps2))

)TINY_SND_CMP"
