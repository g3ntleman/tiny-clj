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
(def minuet-in-g-duration-ms
  (sound/track-duration-ms minuet-in-g-steps minuet-in-g-opts))
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
   :tempo-bpm 80
   :gate-percent 78})
(def the-entertainer-duration-ms
  (sound/track-duration-ms the-entertainer-steps the-entertainer-opts))
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
   :tempo-bpm 60
   :gate-percent 88})
(def gymnopedie-no-1-duration-ms
  (sound/track-duration-ms gymnopedie-no-1-steps gymnopedie-no-1-opts))
(defn play-gymnopedie-no-1!
  "Plays a slow two-buzzer Gymnopedie No. 1 phrase once."
  []
  (sound/play-steps! gymnopedie-no-1-track-id gymnopedie-no-1-steps gymnopedie-no-1-opts))

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
(def rondo-alla-turca-duration-ms
  (sound/track-duration-ms rondo-alla-turca-steps rondo-alla-turca-opts))
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
(def hall-of-the-mountain-king-duration-ms
  (sound/track-duration-ms hall-of-the-mountain-king-steps hall-of-the-mountain-king-opts))
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
(def can-can-duration-ms
  (sound/track-duration-ms can-can-steps can-can-opts))
(defn play-can-can!
  "Plays a compact two-buzzer Can Can phrase once."
  []
  (sound/play-steps! can-can-track-id can-can-steps can-can-opts))

