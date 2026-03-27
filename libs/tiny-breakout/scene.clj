(ns tiny-breakout.scene
  (:require [tiny-breakout.core :as core]
            [tiny-breakout.levels :as levels]
            [tiny-fx.gfx :as gfx]
            [tiny-fx.gfx-scene :refer [->Group ->Rect ->Style ->VText normalize-spatial-rule]]))

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
  (concrete-spatial-rule :ball-vs-paddle :ball :paddle))

(defn- visible-brick-ids
  [bricks]
  (keys bricks))

;; Cached paddle-only rule vector. Brick collisions are handled predictively
;; by fx/sweep-aabb; no per-brick SpatialRule is needed at runtime.
(def ^:private paddle-only-rules [(paddle-rule)])

(defn with-expanded-collision-rules
  "Attaches the paddle collision rule to state. Brick rules are not needed
  because brick collisions are resolved predictively by fx/sweep-aabb."
  [state]
  (let [state (dissoc state :collision-rule-targets :collision-rule-targets-for)]
    (if (identical? (:collision-rules state) paddle-only-rules)
      state
      (assoc state :collision-rules paddle-only-rules))))

(defn overlay-text
  "Returns the shared centered overlay label for one breakout phase."
  [phase]
  (cond
    (= phase :game-over) "GAME OVER"
    (= phase :victory) "YOU WIN!"
    (= phase :pause) "PAUSED"
    (= phase :title) "BREAKOUT"
    (= phase :level-clear) "LEVEL CLEAR"
    :else ""))

(def ^:private overlay-char-advance
  {\space 5
   \! 4})

(defn- overlay-text-width
  [overlay]
  (reduce (fn [width ch]
            (+ width (get overlay-char-advance ch 10)))
          0
          overlay))

(defn overlay-x
  "Returns the centered x position for one overlay label in the breakout font."
  [overlay]
  (if (= overlay "")
    100
    (quot (+ (- core/playfield-width (overlay-text-width overlay)) 1) 2)))

(def ^:private ease-in-duration-ms 880)
(def ^:private ease-in-dark-gray 24)
(def ^:private ease-in-light-gray 255)
(def ^:private ease-in-stop-count 11)

(defn- ease-in-stop-times
  []
  (mapv (fn [step]
          (quot (* ease-in-duration-ms step) ease-in-stop-count))
        (range 0 (+ ease-in-stop-count 1))))

(defn- ease-in-gray-rgb565
  [offset-ms]
  (let [gray (+ ease-in-dark-gray
                (quot (* (- ease-in-light-gray ease-in-dark-gray) offset-ms)
                      ease-in-duration-ms))]
    (gfx/color gray gray gray)))

(defn ease-in-keyframes
  "Compiles explicit grayscale stops into duplicated-timestamp keyframes so the
  shared timeline path stays in Clojure and never interpolates packed RGB565 values."
  [start-ms]
  (let [stops (mapv (fn [offset-ms]
                      [(+ start-ms offset-ms)
                       (ease-in-gray-rgb565 offset-ms)])
                    (ease-in-stop-times))]
    (if (empty? stops)
      []
      (reduce (fn [keyframes [next-ms next-value]]
                (let [[_ prev-value] (peek keyframes)]
                  (conj keyframes
                        [next-ms prev-value]
                        [next-ms next-value])))
              [(first stops)]
              (rest stops)))))

(defn overlay-style
  [state overlay]
  (if (= overlay "")
    nil
    (let [start-ms (get state :overlay-start-ms)
          stroke-color (if (number? start-ms)
                         (record-create 'Timeline
                                        [(ease-in-keyframes start-ms)
                                         false
                                         false])
                         (gfx/color 0xffffff))]
      (->Style stroke-color nil true false nil false nil))))

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
  (let [active-bricks (levels/normalize-bricks (get state :bricks))
        levels (get state :levels)
        level-index (get state :level-index)
        phase (get state :phase)]
    (cond
      (not (empty? active-bricks))
      active-bricks
      (and (vector? levels)
           (number? level-index)
           (<= 0 level-index)
           (< level-index (count levels))
           (not= phase :title))
      (let [level (nth levels level-index)
            bricks (get level :bricks)]
        (levels/level-bricks level))
      :else {})))

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
          overlay-label (overlay-text phase)
          overlay (if (and (= phase :title)
                           (not (number? (get state :overlay-start-ms))))
                    ""
                    overlay-label)
          overlay-x (overlay-x overlay)
          overlay-style (overlay-style state overlay)
          base-child-ids [:background
                          :paddle
                          :ball
                          :score-text
                          :overlay-text
                          :lives-label
                          :lives-value]
          base-entities {:background (->Rect :background nil nil true 0 0 core/playfield-width core/playfield-height nil)
                         :paddle (->Rect :paddle nil nil true paddle-x-field core/paddle-y core/paddle-width core/paddle-height paddle-shape)
                         :ball (->Rect :ball nil nil true ball-x-field ball-y-field core/ball-size core/ball-size ball-shape)
                         :score-text (->VText :score-text nil nil true 8 12 1 0 score-text nil)
                         :overlay-text (->VText :overlay-text nil overlay-style true overlay-x 120 1 0 overlay nil)
                         :lives-label (->VText :lives-label nil nil true 226 12 1 0 "Lives:" nil)
                         :lives-value (->VText :lives-value nil nil true 286 12 1 0 lives-text nil)}]
      (let [[entities child-ids-t]
            (reduce (fn [[ents cids] [_ brick]]
                      (let [brick-id (:id brick)]
                        [(assoc ents brick-id (brick->entity brick brick-shape))
                         (conj! cids brick-id)]))
                    [base-entities (transient base-child-ids)]
                    bricks)
            child-ids (persistent! child-ids-t)
            root-node (->Group :tiny-fx.scene/root nil nil true child-ids nil)]
        {:type :FrameScene
         :root :tiny-fx.scene/root
         :index (assoc entities :tiny-fx.scene/root root-node)
         :clip-rect [0 0 core/playfield-width core/playfield-height]
         :z 0
         :visible true
         :opaque true
         :erase-color 0
         :guard-px 1
         :collision-rules (:collision-rules state)}))))
