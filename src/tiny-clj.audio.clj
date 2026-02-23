R"TINY_SND_CMP(
(ns tiny-snd.composer
  (:require [tiny-snd.runtime :refer :all]))

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

(defn note->midi [note-kw]
  (cond
    (= note-kw :REST) 0
    (not (keyword? note-kw))
    (throw (ex-info "note->midi expects keyword like :G5, :Cs4/:Db4, :Bb3 or :REST"
                    {:note note-kw}))
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
        (throw (ex-info "Invalid note keyword"
                        {:note note-kw :parsed-midi midi}))
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
        (throw (ex-info "Unknown musical duration keyword"
                        {:dur dur
                         :supported [:w :h :q :e :s :t :dh :dq :de :ds :qt :et :st]}))
        (let [quarter-ms (max 1 (quot 60000 tempo-bpm))
              num (nth frac 0)
              den (nth frac 1)]
          ;; Round to nearest integer ms: (x + den/2) / den
          (max 1 (quot (+ (* quarter-ms num) (quot den 2)) den)))))
    (throw (ex-info "Step duration must be a musical keyword (:q, :e, :dq, ...)"
                    {:dur dur}))))

(defn compile-track
  "Generic N-voice compiler (1..16 channels).
   Step format:
   {:notes [:G5 :D5 ...] :dur :q}
   {:melody :G5 :backing [:D5 :Bb4 ...] :dur :q} ;; backing auto-fills channels
   {:rest :e}                     ;; full rest for all channels
   Options:
   {:channel-count 1..16
    :volumes [220 160 140 ...]
    :gate-percent 82
    :tempo-bpm 120}
   Supported duration keywords:
   :w :h :q :e :s :t :dh :dq :de :ds :qt :et :st"
  [steps opts]
  (let [has-melody-backing (loop [s steps]
                             (if (empty? s)
                               false
                               (let [step (first s)]
                                 (if (or (not (nil? (get step :melody)))
                                         (not (nil? (get step :backing))))
                                   true
                                   (recur (rest s))))))
        opts* (if has-melody-backing
                (compile-opts-melody-backing steps opts)
                opts)
        steps* (if has-melody-backing
                 (melody-backing->steps steps opts*)
                 steps)
        inferred (infer-channel-count steps*)
        channel-count (or (get opts* :channel-count) inferred)
        _ (when (or (< channel-count 1) (> channel-count 16))
            (throw (ex-info "compile-track: :channel-count must be in 1..16"
                            {:channel-count channel-count})))
        tempo-bpm (or (get opts* :tempo-bpm) 120)
        _ (when (or (< tempo-bpm 20) (> tempo-bpm 400))
            (throw (ex-info "compile-track: :tempo-bpm must be in 20..400"
                            {:tempo-bpm tempo-bpm})))
        gate-percent (or (get opts* :gate-percent) 82)
        volumes (or (get opts* :volumes) [200 180 160 140])
        events0 (build-initial-vol-events channel-count volumes)
        eventsN (loop [s steps* ev events0]
                  (if (empty? s)
                    (reduce conj ev (evt-end))
                    (let [step (first s)
                          rest-dur (get step :rest)
                          has-rest (not (nil? rest-dur))
                          _ (when (and has-rest (not (nil? (get step :dur))))
                              (throw (ex-info "compile-track step must not use both :dur and :rest"
                                              {:step step})))
                          dur-spec (if has-rest rest-dur (or (get step :dur) :q))
                          dur (duration->ms dur-spec tempo-bpm)
                          gate (max 20 (quot (* dur gate-percent) 100))
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

(defn- play-result->status [ok]
  (if ok :playing :stopped))

(defn play-steps!
  "Compiles, loads and plays steps once.
   Options map is passed to compile-track."
  [track-id steps opts]
  (audio-load-track! track-id (compile-track steps opts))
  (play-result->status (audio-play-music! track-id 1)))

;; Melody + backing transformation helper.
;; Input step format:
;; {:melody :G5 :backing [:D5 :Bb4 ...] :dur :q}
;; {:rest :e}  ;; full rest for melody and backing channels

(defn- infer-channel-count-melody-backing [steps]
  (loop [s steps best 1]
    (if (empty? s)
      best
      (let [step (first s)
            backing (or (get step :backing) [])
            n (+ 1 (count backing))
            best2 (if (> n best) n best)]
        (recur (rest s) best2)))))

(defn- build-melody-backing-volumes [channel-count melody-vol backing-volumes]
  (let [target-backing (max 0 (- channel-count 1))]
    (loop [i 0 out [melody-vol]]
      (if (< i target-backing)
        (recur (+ i 1) (conj out (or (nth backing-volumes i nil) 160)))
        out))))

(defn- compile-opts-melody-backing [steps opts]
  (let [base (or opts {})
        inferred (infer-channel-count-melody-backing steps)
        channel-count (or (get base :channel-count) inferred)
        _ (when (or (< channel-count 1) (> channel-count 16))
            (throw (ex-info "compile-track-melody-backing: :channel-count must be in 1..16"
                            {:channel-count channel-count})))
        melody-vol (or (get base :melody-vol) 220)
        backing-volumes (or (get base :backing-volumes) [160 140 120 110 100])
        gate-percent (or (get base :gate-percent) 82)
        volumes (or (get base :volumes)
                    (build-melody-backing-volumes channel-count melody-vol backing-volumes))
        opts1 (dissoc base :melody-vol :backing-volumes)
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
  "Normalizes melody+backing steps into generic {:notes [...] :dur ...} steps.
   Backing is automatically expanded to all available backing channels.
   If :channel-count is N, melody uses 1 channel and backing is expanded to N-1 notes.
   Supports pause shorthand per step: {:rest :e}."
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
                         _ (when (and has-rest (not (nil? (get step :dur))))
                             (throw (ex-info "compile-track-melody-backing step must not use both :dur and :rest"
                                             {:step step})))
                         melody (if has-rest :REST (or (get step :melody) :REST))
                         dur (if has-rest rest-dur (or (get step :dur) :q))
                         backing-src (if has-rest [] (or (get step :backing) []))
                         backing (expand-backing-auto backing-src backing-count)
                         notes (into [melody] backing)]
                     (recur (rest s) (conj out {:notes notes :dur dur})))))]
    steps2))

;; -----------------------------------------------------------------------------
;; Demo: piezo-friendly Star Wars title phrase
;; -----------------------------------------------------------------------------

(def starwars-title-steps
  [{:rest :t}
   {:melody :G5 :backing [:D4] :dur :q}
   {:melody :G5 :backing [:D4] :dur :q}
   {:melody :G5 :backing [:D4] :dur :q}
   {:melody :Eb5 :backing [:C4] :dur :de}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :G5 :backing [:D4] :dur :q}
   {:rest :s}

   {:melody :Eb5 :backing [:C4] :dur :de}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :G5 :backing [:D4] :dur :q}
   {:rest :s}])

(defn play-starwars-title!
  "Convenience demo for host/ESP playback."
  []
  (play-steps! :starwars-title-2v starwars-title-steps
               {:channel-count 2
                :melody-vol 220
                :backing-volumes [195]
                :tempo-bpm 104
                :gate-percent 78}))

(defn wait-until-audio-stopped!
  "Blocks cooperatively until audio playback stops or timeout elapses.
   Uses yield so host/event-loop can continue progressing.
   Returns:
   :stopped  -> tick is no longer running
   :timeout  -> timeout reached while still running
   :unknown  -> status API unsupported"
  [timeout-ms]
  (let [limit (if (or (nil? timeout-ms) (< timeout-ms 1)) 2000 timeout-ms)]
    (loop [elapsed 0]
      (let [st (audio-host-status!)
            supported (get st :supported)
            running (get st :tick-running)]
        (cond
          (not supported) :unknown
          (not running) :stopped
          (>= elapsed limit) :timeout
          :else
          (do
            (yield 10)
            (recur (+ elapsed 10))))))))

(defn play-starwars-title-blocking!
  "Starts the Star Wars demo and waits until playback ends (or timeout).
   Returns a map:
   {:start :playing|:stopped
    :wait :stopped|:timeout|:unknown}"
  []
  (let [start (play-starwars-title!)
        wait (wait-until-audio-stopped! 6000)]
    {:start start :wait wait}))

)TINY_SND_CMP"
