(ns tiny-fx.sound-demos
  (:require [tiny-fx.sound :as sound]))

;; Public-domain demo phrases for two piezo buzzers.
;; Each demo exposes its data and a direct REPL entry point.

(def minuet-in-g-track-id :sound-demos-minuet-in-g)
(def minuet-in-g-steps
  [{:melody :D5 :backing [:G3] :duration :q}
   {:melody :G5 :backing [:D4] :duration :e}
   {:melody :A5 :backing [:D4] :duration :e}
   {:melody :B5 :backing [:G3] :duration :q}
   {:melody :G5 :backing [:D4] :duration :q}
   {:melody :D6 :backing [:G3] :duration :q}
   {:melody :B5 :backing [:D4] :duration :q}
   {:melody :G5 :backing [:G3] :duration :dq}
   {:melody :A5 :backing [:D4] :duration :e}
   {:melody :B5 :backing [:G3] :duration :q}
   {:melody :C6 :backing [:E4] :duration :q}
   {:melody :D6 :backing [:A3] :duration :q}
   {:rest :e}
   {:melody :B5 :backing [:D4] :duration :e}
   {:melody :A5 :backing [:G3] :duration :q}
   {:melody :G5 :backing [:D4] :duration :dh}])
(def minuet-in-g-opts
  {:melody {:volume 220}
   :backing {:volume 150}
   :tempo-bpm 140
   :gate-percent 78})
(defn play-minuet-in-g!
  "Plays the two-buzzer Minuet in G demo phrase once."
  []
  (sound/play-steps! minuet-in-g-track-id minuet-in-g-steps minuet-in-g-opts))

(def the-entertainer-track-id :sound-demos-the-entertainer)
(def the-entertainer-steps
  [{:melody :D5 :backing [:G3] :duration :s}
   {:melody :E5 :backing [:D4] :duration :s}
   {:melody :C5 :backing [:G3] :duration :s}
   {:melody :A4 :backing [:D4] :duration :e}
   {:melody :B4 :backing [:G3] :duration :s}
   {:melody :G4 :backing [:G3] :duration :e}
   {:melody :D5 :backing [:G3] :duration :s}
   {:melody :E5 :backing [:D4] :duration :s}
   {:melody :C5 :backing [:G3] :duration :s}
   {:melody :A4 :backing [:D4] :duration :e}
   {:melody :B4 :backing [:G3] :duration :s}
   {:melody :G4 :backing [:G3] :duration :e}
   {:melody :E4 :backing [:C3] :duration :s}
   {:melody :C5 :backing [:G3] :duration :e}
   {:melody :E4 :backing [:E3] :duration :s}
   {:melody :C5 :backing [:G2] :duration :e}
   {:melody :E4 :backing [:C3] :duration :s}
   {:melody :C5 :backing [:C3] :duration :dq}
   {:melody :C6 :backing [:C4] :duration :s}
   {:melody :D6 :backing [:A3] :duration :s}
   {:melody :Ds6 :backing [:B3] :duration :s}
   {:melody :E6 :backing [:G2] :duration :s}
   {:melody :C6 :backing [:A3] :duration :s}
   {:melody :D6 :backing [:B3] :duration :s}
   {:melody :E6 :backing [:C3] :duration :e}
   {:melody :B5 :backing [:G2] :duration :s}
   {:melody :D6 :backing [:G3] :duration :e}
   {:melody :C6 :backing [:C3] :duration :dq}])
(def the-entertainer-opts
  {:melody {:volume 220}
   :backing {:volume 128}
   :tempo-bpm 88
   :gate-percent 78})
(defn play-the-entertainer!
  "Plays a longer Mutopia-derived two-buzzer Entertainer phrase once."
  []
  (sound/play-steps! the-entertainer-track-id the-entertainer-steps the-entertainer-opts))

(def gymnopedie-no-1-track-id :sound-demos-gymnopedie-no-1)
(def gymnopedie-no-1-steps
  [{:melody :D5 :backing [:G2] :duration :dh}
   {:melody :E5 :backing [:D3] :duration :dh}
   {:melody :D5 :backing [:G2] :duration :dh}
   {:melody :B4 :backing [:D3] :duration :dh}
   {:melody :A4 :backing [:G2] :duration :h}
   {:rest :q}
   {:melody :B4 :backing [:D3] :duration :dh}
   {:melody :D5 :backing [:G2] :duration :dh}])
(def gymnopedie-no-1-opts
  {:melody {:volume 210}
   :backing {:volume 120}
   :tempo-bpm 76
   :gate-percent 88})
(defn play-gymnopedie-no-1!
  "Plays a slow two-buzzer Gymnopedie No. 1 phrase once."
  []
  (sound/play-steps! gymnopedie-no-1-track-id gymnopedie-no-1-steps gymnopedie-no-1-opts))

(def william-tell-finale-track-id :sound-demos-william-tell-finale)
;; Full two-buzzer reduction of the fast William Tell finale.
;; Rebuilt from ABC/MusicXML notation (no slow overture intro) so the full
;; galloping tune is present instead of the truncated demo MIDI.
(def william-tell-finale-steps
  [{:melody :G4 :duration :e}
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
(def william-tell-finale-opts
  {:melody {:volume 220}
   :backing {:volume 138}
   :tempo-bpm 200
   :gate-percent 100})
(defn play-william-tell-finale!
  "Plays the full fast William Tell finale once, based on notation instead of the truncated demo MIDI."
  []
  (sound/play-steps! william-tell-finale-track-id william-tell-finale-steps william-tell-finale-opts))

(def rondo-alla-turca-track-id :sound-demos-rondo-alla-turca)
(def rondo-alla-turca-steps
  [{:melody :A5 :backing [:A3] :duration :s}
   {:melody :Gs5 :backing [:E4] :duration :s}
   {:melody :A5 :backing [:A3] :duration :s}
   {:melody :C6 :backing [:E4] :duration :s}
   {:melody :E6 :backing [:A3] :duration :e}
   {:melody :A5 :backing [:A3] :duration :s}
   {:melody :Gs5 :backing [:E4] :duration :s}
   {:melody :A5 :backing [:A3] :duration :s}
   {:melody :B5 :backing [:E4] :duration :s}
   {:melody :C6 :backing [:A3] :duration :e}
   {:melody :A5 :backing [:A3] :duration :s}
   {:melody :Gs5 :backing [:E4] :duration :s}
   {:melody :A5 :backing [:A3] :duration :s}
   {:melody :E5 :backing [:E4] :duration :s}
   {:melody :A5 :backing [:A3] :duration :e}
   {:rest :e}
   {:melody :G5 :backing [:G3] :duration :e}
   {:melody :A5 :backing [:E4] :duration :e}
   {:melody :B5 :backing [:G3] :duration :e}
   {:melody :C6 :backing [:A3] :duration :e}
   {:melody :E6 :backing [:A3] :duration :h}])
(def rondo-alla-turca-opts
  {:melody {:volume 220}
   :backing {:volume 142}
   :tempo-bpm 120
   :gate-percent 72})
(defn play-rondo-alla-turca!
  "Plays a compact two-buzzer Rondo alla Turca phrase once."
  []
  (sound/play-steps! rondo-alla-turca-track-id rondo-alla-turca-steps rondo-alla-turca-opts))

(def hall-of-the-mountain-king-track-id :sound-demos-hall-of-the-mountain-king)
(def hall-of-the-mountain-king-steps
  [{:melody :E4 :backing [:D3] :duration :q}
   {:melody :F4 :backing [:D3] :duration :q}
   {:melody :G4 :backing [:D3] :duration :q}
   {:melody :E4 :backing [:D3] :duration :q}
   {:melody :E4 :backing [:D3] :duration :q}
   {:melody :F4 :backing [:D3] :duration :q}
   {:melody :G4 :backing [:D3] :duration :q}
   {:melody :A4 :backing [:D3] :duration :q}
   {:melody :B4 :backing [:D3] :duration :q}
   {:rest :e}
   {:melody :A4 :backing [:D3] :duration :h}
   {:melody :E4 :backing [:D3] :duration :w}])
(def hall-of-the-mountain-king-opts
  {:melody {:volume 220}
   :backing {:volume 138}
   :tempo-bpm 60
   :gate-percent 80})
(defn play-hall-of-the-mountain-king!
  "Plays a compact two-buzzer Hall of the Mountain King phrase once."
  []
  (sound/play-steps! hall-of-the-mountain-king-track-id
                     hall-of-the-mountain-king-steps
                     hall-of-the-mountain-king-opts))

(def can-can-track-id :sound-demos-can-can)
(def can-can-steps
  [{:melody :A5 :backing [:A3] :duration :e}
   {:melody :C6 :backing [:E4] :duration :e}
   {:melody :A5 :backing [:A3] :duration :e}
   {:melody :G5 :backing [:E4] :duration :e}
   {:melody :E5 :backing [:A3] :duration :e}
   {:melody :G5 :backing [:E4] :duration :e}
   {:melody :E5 :backing [:A3] :duration :e}
   {:melody :D5 :backing [:E4] :duration :e}
   {:melody :C5 :backing [:A3] :duration :e}
   {:melody :E5 :backing [:E4] :duration :e}
   {:melody :C5 :backing [:A3] :duration :e}
   {:melody :B4 :backing [:E4] :duration :e}
   {:melody :A4 :backing [:A3] :duration :q}
   {:rest :q}])
(def can-can-opts
  {:melody {:volume 220}
   :backing {:volume 152}
   :tempo-bpm 120
   :gate-percent 76})
(defn play-can-can!
  "Plays a compact two-buzzer Can Can phrase once."
  []
  (sound/play-steps! can-can-track-id can-can-steps can-can-opts))

(def rocket-launch-sfx-track-id :sound-demos-rocket-launch-sfx)
;; Rocket/thruster SFX tuned closer to the host reference:
;; two long noisy rise phases plus a short bright exhaust tail.
(def rocket-launch-sfx-steps
  [{:notes [180] :bend [285] :noise true :duration 1100}
   {:notes [220] :bend [345] :noise true :duration 1100}
   {:notes [560] :duration 80}])
(def rocket-launch-sfx-opts
  {:channel-count 1
   :volumes [240]
   :gate-percent 100})
(defn play-rocket-launch-sfx!
  "Plays a one-shot rocket launch SFX using tuned compile-time bend+noise expansion."
  []
  (sound/play-sfx! rocket-launch-sfx-track-id rocket-launch-sfx-steps rocket-launch-sfx-opts))

;; -----------------------------------------------------------------------------
;; Laser SFX: DSL-only downward chirp via bounded compile-time bend expansion.
;; -----------------------------------------------------------------------------

(def laser-sfx-track-id :sound-demos-laser)

;; One bounded bend step plus a short tail for a compact downward chirp.
(def laser-sfx-steps
  [{:notes [5200] :bend [1200] :duration 160}
   {:notes [550] :duration 56}])

(def laser-sfx-opts
  {:channel-count 1
   :volumes [228]
   :gate-percent 88})
(defn play-laser-sfx!
  "One-shot laser pew via compile-time bend expansion."
  []
  (sound/play-sfx! laser-sfx-track-id laser-sfx-steps laser-sfx-opts))
