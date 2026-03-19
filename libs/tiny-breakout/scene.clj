(ns tiny-breakout.scene
  (:require [tiny-breakout.core :as core]
            [tiny-fx.gfx-scene :refer [->Group ->Rect ->VText normalize-spatial-rule]]))

(defn- paddle-prototype
  []
  (->Rect :breakout/paddle nil nil true 0 0 40 4 nil))

(defn- ball-prototype
  []
  (->Rect :breakout/ball nil nil true 0 0 4 4 nil))

(defn- brick-prototype
  []
  (->Rect :breakout/brick nil nil true 0 0 20 10 nil))

(defn- concrete-spatial-rule
  [rule-id self-id other-id]
  (record-from-map 'SpatialRule
                   (normalize-spatial-rule {:id rule-id
                                            :slot :game
                                            :kind :collision
                                            :self self-id
                                            :other other-id})))

(defn- expand-breakout-spatial-rules
  [brick-ids]
  (loop [remaining brick-ids
         rules (conj! (transient [])
                      (concrete-spatial-rule :ball-vs-paddle 1003 1002))]
    (if (empty? remaining)
      (persistent! rules)
      (recur (rest remaining)
             (conj! rules
                    (concrete-spatial-rule :ball-vs-brick 1003 (first remaining)))))))

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
  [brick prototype]
  (->Rect (:id brick)
          nil
          nil
          true
          (:x brick)
          (:y brick)
          (:w brick)
          (:h brick)
          prototype))

(defn- maybe-field-timeline
  [state from-value axis-key]
  (let [segment (:ball-segment state)]
    (if (and (map? segment)
             (number? (:start-ms segment))
             (number? (:end-ms segment))
             (> (:end-ms segment) (:start-ms segment)))
      (record-create 'Timeline
                     [[[(let [v (:start-ms segment)] (if (number? v) v 0))
                        from-value]
                       [(let [v (:end-ms segment)] (if (number? v) v 0))
                        (get segment axis-key)]]
                      false])
      from-value)))

(defn- visible-bricks
  [state]
  (let [active-bricks (get state :bricks)
        levels (get state :levels)
        level-index (get state :level-index)
        phase (get state :phase)]
    (cond
      (and (vector? active-bricks)
           (or (not (empty? active-bricks))
               (not= phase :title)))
      active-bricks
      (and (vector? levels)
           (number? level-index)
           (<= 0 level-index)
           (< level-index (count levels)))
      (let [level (nth levels level-index)
            bricks (get level :bricks)]
        (if (vector? bricks) bricks []))
      :else [])))

(defn build-scene
  "Builds one deterministic frame-scene shaped map from breakout state map."
  [state]
  (let [paddle-x (let [v (get state :paddle-x)] (if (number? v) v 0))
        ball-x (let [v (get state :ball-x)] (if (number? v) v 0))
        ball-y (let [v (get state :ball-y)] (if (number? v) v 0))
        ball-x-field (maybe-field-timeline state ball-x :to-x)
        ball-y-field (maybe-field-timeline state ball-y :to-y)
        score (let [v (get state :score)] (if (number? v) v 0))
        lives (let [v (get state :lives)] (if (number? v) v 0))
        phase (or (get state :phase) :title)
        paddle-prototype (paddle-prototype)
        ball-prototype (ball-prototype)
        brick-prototype (brick-prototype)
        hud (str "Score: " score "  Lives: " lives)
        overlay (overlay-text phase)
        bricks (visible-bricks state)
        base-entities {1001 (->Rect 1001 nil nil true 0 0 core/playfield-width core/playfield-height nil)
                       1002 (->Rect 1002 nil nil true paddle-x core/paddle-y core/paddle-width core/paddle-height paddle-prototype)
                       1003 (->Rect 1003 nil nil true ball-x-field ball-y-field core/ball-size core/ball-size ball-prototype)
                       1004 (->VText 1004 nil nil true 8 12 1 0 hud nil)
                       1005 (->VText 1005 nil nil true 100 120 1 0 overlay nil)}]
    (loop [remaining bricks
           entities base-entities
           child-ids [1001 1002 1003 1004 1005]]
      (if (empty? remaining)
        {:type :FrameScene
         :root (->Group 1000 nil nil true child-ids nil)
         :index entities
         :clip-rect [0 0 320 240]
         :z 0
         :visible true
         :opaque true
         :erase-color 0
         :guard-px 1
         :collision-rules (expand-breakout-spatial-rules (drop 5 child-ids))}
        (let [brick (first remaining)
              brick-id (:id brick)]
          (recur (rest remaining)
                 (assoc entities brick-id (brick->entity brick brick-prototype))
                 (conj child-ids brick-id)))))))
