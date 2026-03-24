(ns tiny-fx.sound-demos-william)

;; William Tell kept in a dedicated namespace so the large literal data is
;; only loaded when explicitly requested.

(defn william-tell-finale-track-id
  []
  :sound-demos-william-tell-finale)
;; Full two-buzzer reduction of the fast William Tell finale.
;; Rebuilt from ABC/MusicXML notation (no slow overture intro) so the full
;; galloping tune is present instead of the truncated demo MIDI.
(defn build-william-tell-finale-steps
  []
  '[{:melody :G4 :duration :e}
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
   {:rest :q}
   {:rest :e}
   {:melody :E5 :backing [:C3] :duration :e}
   {:melody :E5 :backing [:C3] :duration :h}
   {:rest :q}
   {:rest :e}
   {:melody :C5 :backing [:C3] :duration :e}
   {:melody :C5 :backing [:C3] :duration :w}])

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

(defn play-william-tell-finale!
  "Plays the full fast William Tell finale once, based on notation instead of the truncated demo MIDI."
  []
  (require 'tiny-fx.sound)
  (require 'tiny-fx.trk1)
  (let [demo (build-william-tell-finale-demo)
        prepared ((var tiny-fx.trk1/prepare-track) (:steps demo) (:opts demo))]
    {:status (if ((var tiny-fx.sound/sound-play-music!) (:track-id demo) (:track-bytes prepared) 1)
               :playing
               :stopped)
     :duration-ms (:duration-ms prepared)}))
