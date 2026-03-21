(ns tiny-fx.sound-demos-william
  (:require [tiny-fx.sound :as sound]
            [tiny-fx.sound-native :as native]))

;; William Tell kept in a dedicated namespace so the large literal data is
;; only loaded when explicitly requested.

(defn william-tell-finale-track-id
  []
  :sound-demos-william-tell-finale)

;; Full two-buzzer reduction of the fast William Tell finale.
;; Rebuilt from ABC/MusicXML notation (no slow overture intro) so the full
;; galloping tune is present instead of the truncated demo MIDI.
;; Split into small quoted chunks to keep require-time parser peak lower on ESP32.

(defn william-tell-finale-steps-chunk-1
  []
  '[
    {:melody :G4 :duration :e}
    {:melody :G4 :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
   ])

(defn william-tell-finale-steps-chunk-2
  []
  '[
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:C3] :duration :e}
    {:melody :G5 :backing [:G2] :duration :h}
    {:melody :G5 :backing [:G2] :duration :e}
    {:melody :F5 :backing [:G2] :duration :e}
    {:melody :E5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-3
  []
  '[
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:C3] :duration :e}
   ])

(defn william-tell-finale-steps-chunk-4
  []
  '[
    {:melody :G5 :backing [:G2] :duration :h}
    {:melody :G5 :backing [:G2] :duration :e}
    {:melody :F5 :backing [:G2] :duration :e}
    {:melody :E5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :e}
    {:melody :E5 :backing [:A2] :duration :e}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :A5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :A5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :A5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :D5 :backing [:A2] :duration :q}
    {:melody :C5 :backing [:A2] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-5
  []
  '[
    {:melody :B4 :backing [:A2] :duration :q}
    {:melody :A4 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :e}
    {:melody :E5 :backing [:A2] :duration :e}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :e}
    {:melody :E5 :backing [:A2] :duration :e}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :A5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :A5 :backing [:A2] :duration :q}
    {:melody :E5 :backing [:A2] :duration :q}
    {:melody :A5 :backing [:A2] :duration :q}
    {:melody :G5 :backing [:D3] :duration :q}
    {:melody :Fs5 :backing [:D3] :duration :q}
    {:melody :G5 :backing [:G2] :duration :dh}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :E5 :backing [:G2] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-6
  []
  '[
    {:melody :F5 :backing [:G2] :duration :q}
    {:melody :D5 :backing [:G2] :duration :h}
    {:melody :F5 :backing [:G2] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :h}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :h}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :E5 :backing [:G2] :duration :q}
    {:melody :F5 :backing [:G2] :duration :q}
    {:melody :D5 :backing [:G2] :duration :h}
    {:melody :F5 :backing [:G2] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :h}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-7
  []
  '[
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-8
  []
  '[
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:C3] :duration :e}
    {:melody :G5 :backing [:G2] :duration :h}
    {:melody :G5 :backing [:G2] :duration :e}
    {:melody :F5 :backing [:G2] :duration :e}
    {:melody :E5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-9
  []
  '[
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:G2] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:C3] :duration :e}
    {:melody :G5 :backing [:G2] :duration :h}
    {:melody :G5 :backing [:G2] :duration :e}
   ])

(defn william-tell-finale-steps-chunk-10
  []
  '[
    {:melody :F5 :backing [:G2] :duration :e}
    {:melody :E5 :backing [:G2] :duration :e}
    {:melody :D5 :backing [:G2] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :h}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:E3] :duration :dh}
    {:melody :D5 :backing [:E3] :duration :q}
    {:melody :C5 :backing [:F3] :duration :q}
    {:melody :B4 :backing [:F3] :duration :q}
    {:melody :C5 :backing [:F3] :duration :q}
    {:melody :A4 :backing [:F3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :B4 :backing [:C3] :duration :e}
   ])

(defn william-tell-finale-steps-chunk-11
  []
  '[
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:D3] :duration :e}
    {:melody :G4 :backing [:D3] :duration :e}
    {:melody :F4 :backing [:D3] :duration :e}
    {:melody :G4 :backing [:D3] :duration :e}
    {:melody :F4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :A4 :backing [:G2] :duration :e}
    {:melody :B4 :backing [:G2] :duration :e}
    {:melody :E4 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:C3] :duration :e}
    {:melody :E4 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:C3] :duration :e}
    {:melody :E4 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :D4 :backing [:D3] :duration :e}
    {:melody :E4 :backing [:D3] :duration :e}
    {:melody :D4 :backing [:D3] :duration :e}
    {:melody :E4 :backing [:D3] :duration :e}
    {:melody :D4 :backing [:D3] :duration :e}
    {:melody :E4 :backing [:D3] :duration :e}
    {:melody :D4 :backing [:D3] :duration :e}
   ])

(defn william-tell-finale-steps-chunk-12
  []
  '[
    {:melody :E4 :backing [:D3] :duration :e}
    {:melody :D4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:E3] :duration :dh}
    {:melody :D5 :backing [:E3] :duration :q}
    {:melody :C5 :backing [:F3] :duration :q}
    {:melody :B4 :backing [:F3] :duration :q}
    {:melody :C5 :backing [:F3] :duration :q}
    {:melody :A4 :backing [:F3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :B4 :backing [:C3] :duration :e}
   ])

(defn william-tell-finale-steps-chunk-13
  []
  '[
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:D3] :duration :e}
    {:melody :G4 :backing [:D3] :duration :e}
    {:melody :F4 :backing [:D3] :duration :e}
    {:melody :G4 :backing [:D3] :duration :e}
    {:melody :F4 :backing [:G2] :duration :e}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :A4 :backing [:G2] :duration :e}
    {:melody :B4 :backing [:G2] :duration :e}
    {:melody :E4 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:C3] :duration :e}
    {:melody :E4 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:C3] :duration :e}
    {:melody :E4 :backing [:C3] :duration :e}
    {:melody :F4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :A4 :backing [:C3] :duration :e}
    {:melody :D4 :backing [:D3] :duration :q}
    {:melody :E4 :backing [:D3] :duration :q}
    {:melody :G4 :backing [:G2] :duration :e}
    {:melody :F4 :backing [:G2] :duration :e}
    {:melody :E4 :backing [:G2] :duration :e}
    {:melody :D4 :backing [:G2] :duration :e}
    {:melody :C4 :backing [:C3] :duration :dh}
   ])

(defn william-tell-finale-steps-chunk-14
  []
  '[
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :e}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :G4 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
    {:melody :F5 :backing [:C3] :duration :q}
    {:melody :G5 :backing [:C3] :duration :q}
    {:rest :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :D5 :backing [:C3] :duration :q}
    {:melody :E5 :backing [:C3] :duration :q}
   ])

(defn william-tell-finale-steps-chunk-15
  []
  '[
    {:rest :q}
    {:melody :E4 :backing [:G2] :duration :q}
    {:melody :F4 :backing [:G2] :duration :q}
    {:melody :G4 :backing [:G2] :duration :q}
    {:rest :q}
    {:rest :h}
    {:melody :B4 :backing [:G2] :duration :h}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:melody :B4 :backing [:G2] :duration :q}
    {:melody :C5 :backing [:C3] :duration :h}
    {:rest :q}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :q}
    {:rest :q}
    {:melody :C5 :backing [:C3] :duration :q}
    {:rest :q}
    {:melody :C5 :backing [:C3] :duration :h}
   ])

(defn william-tell-finale-steps-chunk-16
  []
  '[
    {:rest :q}
    {:rest :e}
    {:melody :E5 :backing [:C3] :duration :e}
    {:melody :E5 :backing [:C3] :duration :h}
    {:rest :q}
    {:rest :e}
    {:melody :C5 :backing [:C3] :duration :e}
    {:melody :C5 :backing [:C3] :duration :w}
   ])

(defn william-tell-finale-step-chunks
  []
  [(william-tell-finale-steps-chunk-1)
   (william-tell-finale-steps-chunk-2)
   (william-tell-finale-steps-chunk-3)
   (william-tell-finale-steps-chunk-4)
   (william-tell-finale-steps-chunk-5)
   (william-tell-finale-steps-chunk-6)
   (william-tell-finale-steps-chunk-7)
   (william-tell-finale-steps-chunk-8)
   (william-tell-finale-steps-chunk-9)
   (william-tell-finale-steps-chunk-10)
   (william-tell-finale-steps-chunk-11)
   (william-tell-finale-steps-chunk-12)
   (william-tell-finale-steps-chunk-13)
   (william-tell-finale-steps-chunk-14)
   (william-tell-finale-steps-chunk-15)
   (william-tell-finale-steps-chunk-16)])

(defn build-william-tell-finale-steps
  []
  ;; Avoid repeated persistent vector copies (native_into/vector_conj) on ESP32.
  (persistent!
    (reduce (fn [acc chunk]
              (reduce conj! acc chunk))
            (transient [])
            (william-tell-finale-step-chunks))))

(defn build-william-tell-finale-opts
  []
  {:melody {:volume 220}
   :backing {:volume 138}
   :tempo-bpm 200
   :gate-percent 100})

(defn build-william-tell-finale-demo
  "Creates and returns the William Tell demo descriptor on demand."
  []
  {:track-id (william-tell-finale-track-id)
   :steps (build-william-tell-finale-steps)
   :opts (build-william-tell-finale-opts)})

(defn william-tell-finale-demo
  "Backwards-compatible alias for the William Tell demo descriptor."
  []
  (build-william-tell-finale-demo))

(defn- compile-and-load-william-tell-finale!
  []
  (let [track-id (william-tell-finale-track-id)
        duration-ms* (atom 0)
        compile-and-load! (fn []
                            (let [compiled (sound/compile-steps
                                             (build-william-tell-finale-steps)
                                             (build-william-tell-finale-opts))
                                  duration-ms (get compiled :duration-ms)
                                  safe-duration-ms (if (= duration-ms 0) 93900 duration-ms)]
                              (native/sound-load-track! track-id (:track-bytes compiled))
                              (reset! duration-ms* safe-duration-ms)
                              nil))]
    (try
      ;; Evaluate compile+load in a child autorelease pool when available.
      (heap (compile-and-load!))
      (catch Exception _
        (compile-and-load!)))
    {:track-id track-id
     :duration-ms @duration-ms*}))

(defn play-william-tell-finale!
  "Compiles and loads the DSL first, then starts playback.
   In DEBUG builds this runs translation in (heap ...) so DSL data is released
   before sound-play-music! starts."
  []
  (let [prepared (compile-and-load-william-tell-finale!)
        track-id (:track-id prepared)]
    {:status (if (native/sound-play-music! track-id 1) :playing :stopped)
     :duration-ms (:duration-ms prepared)}))
