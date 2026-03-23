(ns tiny-breakout.scene
  (:require [tiny-breakout.core :as core]
            [tiny-fx.gfx-scene :refer [->Group ->Rect ->VText normalize-spatial-rule]]))

(defn paddle-prototype
  []
  (->Rect :breakout/paddle nil nil true 0 0 40 4 nil))

(defn ball-prototype
  []
  (->Rect :breakout/ball nil nil true 0 0 4 4 nil))

(defn brick-prototype
  []
  (->Rect :breakout/brick nil nil true 0 0 20 10 nil))

(defn concrete-spatial-rule
  [rule-id self-id other-id]
  (record-from-map 'SpatialRule
                   (normalize-spatial-rule {:id rule-id
                                            :slot :game
                                            :kind :collision
                                            :self self-id
                                            :other other-id})))

(defn paddle-rule
  []
  (concrete-spatial-rule :ball-vs-paddle 1003 1002))

(defn paddle-rule?
  [rule]
  (and (= :ball-vs-paddle (:id rule))
       (= 1003 (:self rule))
       (= 1002 (:other rule))))

(defn brick-rule-target-id
  [rule]
  (if (and (= :ball-vs-brick (:id rule))
           (= 1003 (:self rule))
           (number? (:other rule)))
    (:other rule)
    nil))

(defn rule-targets
  [rules]
  (loop [i 0
         known {}]
    (if (>= i (count rules))
      known
      (let [rule (nth rules i)
            brick-id (brick-rule-target-id rule)]
        (recur (inc i)
               (if brick-id
                 (assoc known brick-id true)
                 known))))))

(defn- rule-target-cache-valid?
  [state rules]
  (and (map? (:collision-rule-targets state))
       (vector? (:collision-rule-targets-for state))
       (identical? (:collision-rule-targets-for state) rules)))

(defn with-expanded-collision-rules
  "Ensures breakout state carries an append-only concrete rule vector.

  Existing brick rules stay in place so removed bricks become inert via the
  scene index instead of forcing eager recompaction.
  After one cache warm-up, returns state unchanged when all rules already
  present (zero allocation on the steady-state path)."
  [state]
  (let [bricks (visible-bricks state)
        existing-rules (let [rules (:collision-rules state)]
                         (if (vector? rules) rules []))
        base-rules (if (some (fn [rule]
                               (and (= :ball-vs-paddle (:id rule))
                                    (= 1003 (:self rule))
                                    (= 1002 (:other rule))))
                             existing-rules)
                     existing-rules
                     (into [(paddle-rule)] existing-rules))
        has-target-cache? (rule-target-cache-valid? state base-rules)
        known-targets (if has-target-cache?
                        (:collision-rule-targets state)
                        (rule-targets base-rules))]
    (loop [remaining bricks
           known known-targets
           rules (transient base-rules)
           added? (not (identical? base-rules existing-rules))]
      (if (empty? remaining)
        (let [rules-out (if added?
                          (persistent! rules)
                          base-rules)]
          (if (or added? (not has-target-cache?))
            (assoc state
                   :collision-rules rules-out
                   :collision-rule-targets known
                   :collision-rule-targets-for rules-out)
            state))
        (let [brick (first remaining)
              brick-id (:id brick)]
          (if (or (not (number? brick-id))
                  (get known brick-id))
            (recur (rest remaining) known rules added?)
            (recur (rest remaining)
                   (assoc known brick-id true)
                   (conj! rules
                          (concrete-spatial-rule :ball-vs-brick 1003 brick-id))
                   true)))))))

(defn overlay-text
  [phase]
  (cond
    (= phase :game-over) "Game Over"
    (= phase :victory) "You win!"
    (= phase :pause) "Paused"
    (= phase :title) "Breakout"
    (= phase :level-clear) "Level Clear"
    :else ""))

(defn brick->entity
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

(defn maybe-field-timeline
  [motion from-value axis-key end-event?]
  (if (and (map? motion)
           (number? (:start-ms motion))
           (number? (:end-ms motion))
           (> (:end-ms motion) (:start-ms motion)))
    (record-create 'Timeline
                   [[[(let [v (:start-ms motion)] (if (number? v) v 0))
                      from-value]
                     [(let [v (:end-ms motion)] (if (number? v) v 0))
                      (get motion axis-key)]]
                    false
                    end-event?])
    from-value))

(defn attached-ball-motion
  [state]
  (let [phase (get state :phase)
        motion (get state :paddle-motion)
        paddle-x (let [v (get state :paddle-x)] (if (number? v) v 0))
        ball-x (let [v (get state :ball-x)] (if (number? v) v 0))]
    (if (and (map? motion)
             (or (= phase :title) (= phase :serve)))
      (record-create 'Timeline
                     [[[(let [v (:start-ms motion)] (if (number? v) v 0))
                        ball-x]
                       [(let [v (:end-ms motion)] (if (number? v) v 0))
                        (+ ball-x (- (get motion :to-x) paddle-x))]]
                      false
                      false])
      nil)))

(defn visible-bricks
  [state]
  (let [active-bricks (get state :bricks)
        levels (get state :levels)
        level-index (get state :level-index)
        phase (get state :phase)]
    (cond
      (and (vector? active-bricks)
           (not (empty? active-bricks)))
      active-bricks
      (and (vector? levels)
           (number? level-index)
           (<= 0 level-index)
           (< level-index (count levels))
           (not= phase :title))
      (let [level (nth levels level-index)
            bricks (get level :bricks)]
        (if (vector? bricks) bricks []))
      :else [])))

(defn build-scene
  "Builds one deterministic frame-scene shaped map from breakout state map.
  State must already carry expanded collision rules (via with-expanded-collision-rules)."
  [state]
  (let [paddle-x (let [v (get state :paddle-x)] (if (number? v) v 0))
        ball-x (let [v (get state :ball-x)] (if (number? v) v 0))
        ball-y (let [v (get state :ball-y)] (if (number? v) v 0))
        paddle-motion (get state :paddle-motion)
        ball-segment (get state :ball-segment)
        paddle-x-field (maybe-field-timeline paddle-motion paddle-x :to-x false)
        attached-ball-x-field (attached-ball-motion state)
        ball-x-field (if (nil? attached-ball-x-field)
                       (maybe-field-timeline ball-segment ball-x :to-x true)
                       attached-ball-x-field)
        ball-y-field (maybe-field-timeline ball-segment ball-y :to-y true)
        score (let [v (get state :score)] (if (number? v) v 0))
        lives (let [v (get state :lives)] (if (number? v) v 0))
        phase (or (get state :phase) :title)
        bricks (visible-bricks state)]
    (let [paddle-shape (paddle-prototype)
          ball-shape (ball-prototype)
          brick-shape (brick-prototype)
          score-text (str "Score: " score)
          lives-text (str lives)
          overlay (overlay-text phase)
          base-entities {1001 (->Rect 1001 nil nil true 0 0 core/playfield-width core/playfield-height nil)
                         1002 (->Rect 1002 nil nil true paddle-x-field core/paddle-y core/paddle-width core/paddle-height paddle-shape)
                         1003 (->Rect 1003 nil nil true ball-x-field ball-y-field core/ball-size core/ball-size ball-shape)
                         1004 (->VText 1004 nil nil true 8 12 1 0 score-text nil)
                         1005 (->VText 1005 nil nil true 100 120 1 0 overlay nil)
                         1006 (->VText 1006 nil nil true 226 12 1 0 "Lives:" nil)
                         1007 (->VText 1007 nil nil true 286 12 1 0 lives-text nil)}]
      (loop [i 0
             entities base-entities
             child-ids (transient [1001 1002 1003 1004 1005 1006 1007])]
        (if (>= i (count bricks))
          (let [child-ids (persistent! child-ids)
                root-node (->Group 'root nil nil true child-ids nil)]
            {:type :FrameScene
             :root 'root
             :index (assoc entities 'root root-node)
             :clip-rect [0 0 320 240]
             :z 0
             :visible true
             :opaque true
             :erase-color 0
             :guard-px 1
             :collision-rules (:collision-rules state)})
          (let [brick (nth bricks i)
                brick-id (:id brick)]
            (recur (inc i)
                   (assoc entities brick-id (brick->entity brick brick-shape))
                   (conj! child-ids brick-id))))))))
