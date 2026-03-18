(ns tiny-breakout.scene
  (:require [tiny-fx.gfx-scene :refer [->Group ->Rect ->VText]]))

(defn- overlay-text
  [phase]
  (cond
    (= phase :game-over) "Game Over"
    (= phase :victory) "You win!"
    (= phase :pause) "Paused"
    (= phase :title) "Breakout"
    (= phase :level-clear) "Level Clear"
    :else ""))

(defn- brick->entity
  [brick]
  (->Rect (:id brick)
          nil
          nil
          true
          (:x brick)
          (:y brick)
          (:w brick)
          (:h brick)
          nil))

(defn build-scene
  "Builds one deterministic frame-scene shaped map from breakout state map."
  [state]
  (let [paddle-x (let [v (get state :paddle-x)] (if (number? v) v 0))
        ball-x (let [v (get state :ball-x)] (if (number? v) v 0))
        ball-y (let [v (get state :ball-y)] (if (number? v) v 0))
        score (let [v (get state :score)] (if (number? v) v 0))
        lives (let [v (get state :lives)] (if (number? v) v 0))
        phase (or (get state :phase) :title)
        hud (str "Score: " score "  Lives: " lives)
        overlay (overlay-text phase)
        bricks (let [xs (get state :bricks)]
                 (if (vector? xs) xs []))
        children (vec (concat
                       [(->Rect 1001 nil nil true 0 0 320 240 nil)
                        (->Rect 1002 nil nil true paddle-x 224 40 4 nil)
                        (->Rect 1003 nil nil true ball-x ball-y 4 4 nil)
                        (->VText 1004 nil nil true 8 12 1 0 hud nil)
                        (->VText 1005 nil nil true 100 120 1 0 overlay nil)]
                       (map brick->entity bricks)))
        root (->Group 1000 nil nil true children nil)]
    {:type :FrameScene
     :root root
     :clip-rect [0 0 320 240]
     :z 0
     :visible true
     :opaque true
     :erase-color 0
     :guard-px 1
     :collision-rules nil}))
