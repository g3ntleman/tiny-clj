(ns tiny-breakout.core
  (:require [tiny-breakout.levels :as levels]))

(def playfield-width 320)
(def playfield-height 240)
(def paddle-width 40)
(def paddle-height 4)
(def paddle-y (- playfield-height 16))
(def paddle-speed 4)
(def ball-size 4)
(def default-lives 3)
(def launch-speed-x 2)
(def launch-speed-y -2)
(def segment-step-ms 16)

(defn- custom-levels
  [state]
  (let [xs (:levels state)]
    (if (and (vector? xs)
             (not (empty? xs)))
      xs
      nil)))

(defn- state-level-count
  [state]
  (let [xs (custom-levels state)]
    (if xs
      (count xs)
      (levels/level-count))))

(defn- current-level-bricks
  [state level-index]
  (let [xs (custom-levels state)]
    (if xs
      (levels/level-bricks (nth xs level-index))
      (levels/level-bricks-by-index level-index))))

(defn- clear-events
  [state]
  (assoc state :events []))

(defn- clear-ball-segment
  [state]
  (assoc state :ball-segment nil))

(defn- clear-paddle-motion
  [state]
  (assoc state :paddle-motion nil))

(defn- serve-ball
  [state]
  (-> state
      (clear-ball-segment)
      (assoc :ball-x (+ (:paddle-x state) (quot paddle-width 2))
             :ball-y (- paddle-y 6))))

(defn- prepare-level
  [state level-index phase]
  (let [state2 (assoc state
                      :phase phase
                      :level-index level-index
                      :bricks (current-level-bricks state level-index)
                      :events []
                      :ball-vx launch-speed-x
                      :ball-vy launch-speed-y)]
    (serve-ball state2)))

(defn- fresh-game-state
  [state]
  (prepare-level (assoc state :score 0 :lives default-lives) 0 :serve))

(defn- launch-from-serve
  [state now-ms]
  (plan-next-segment (assoc (serve-ball state)
                            :phase :play
                            :ball-vx launch-speed-x
                            :ball-vy launch-speed-y)
                     now-ms))

(defn skip-title-launch-state
  "Same domain state as after :fire on the title screen: level 0, :play, first ball segment.
  now-ms is used for segment timing (use `current-time-ms` from the host)."
  [now-ms]
  (launch-from-serve (fresh-game-state (init-state)) now-ms))

(defn init-state
  "Returns deterministic baseline game-state for one-screen breakout."
  []
  {:phase :title
   :score 0
   :lives default-lives
   :level-index 0
   :bricks []
   :events []
   :paddle-x 140
   :ball-x 160
   :ball-y 210
   :ball-vx launch-speed-x
   :ball-vy launch-speed-y
   :ball-segment nil
   :segment-id-seq 0})

(defn- paddle-bounce-vx
  [paddle-x ball-x]
  (let [ball-center (+ ball-x (quot ball-size 2))
        paddle-center (+ paddle-x (quot paddle-width 2))
        delta (- ball-center paddle-center)]
    (cond
      (< delta -12) -3
      (< delta -4) -1
      (> delta 12) 3
      (> delta 4) 1
      :else 0)))

(defn- duration-ms-for-distance
  [distance speed]
  (if (<= speed 0)
    0
    (let [scaled (* distance segment-step-ms)]
      (quot (+ scaled (- speed 1)) speed))))

(defn- candidate-segment
  [wall duration-ms]
  (if (> duration-ms 0)
    {:wall wall
     :duration-ms duration-ms}
    nil))

(defn- earlier-segment
  [best candidate]
  (if (nil? best)
    candidate
    (if (< (:duration-ms candidate) (:duration-ms best))
      candidate
      best)))

(defn- choose-segment-wall
  [ball-x ball-y vx vy]
  (let [right-limit (- playfield-width ball-size)
        bottom-limit (+ playfield-height 1)
        best-x (if (> vx 0)
                 (candidate-segment :right (duration-ms-for-distance (- right-limit ball-x) vx))
                 (if (< vx 0)
                   (candidate-segment :left (duration-ms-for-distance ball-x (- vx)))
                   nil))
        best-y (if (> vy 0)
                 (candidate-segment :bottom (duration-ms-for-distance (- bottom-limit ball-y) vy))
                 (if (< vy 0)
                   (candidate-segment :top (duration-ms-for-distance ball-y (- vy)))
                   nil))]
    (earlier-segment best-x best-y)))

(defn- project-axis
  [pos velocity duration-ms]
  (+ pos (quot (* velocity duration-ms) segment-step-ms)))

(defn- plan-next-segment
  [state now-ms]
  (if (not= (:phase state) :play)
    (clear-ball-segment state)
    (let [ball-x (let [v (:ball-x state)] (if (number? v) v 0))
          ball-y (let [v (:ball-y state)] (if (number? v) v 0))
          vx (let [v (:ball-vx state)] (if (number? v) v 0))
          vy (let [v (:ball-vy state)] (if (number? v) v 0))
          chosen (choose-segment-wall ball-x ball-y vx vy)]
      (if (nil? chosen)
        (clear-ball-segment state)
        (let [duration-ms (:duration-ms chosen)
              segment-id (+ (let [v (:segment-id-seq state)] (if (number? v) v 0)) 1)
              wall (:wall chosen)
              target-x (if (or (= wall :left) (= wall :right))
                         (if (= wall :left) 0 (- playfield-width ball-size))
                         (project-axis ball-x vx duration-ms))
              target-y (if (or (= wall :top) (= wall :bottom))
                         (if (= wall :top) 0 (+ playfield-height 1))
                         (project-axis ball-y vy duration-ms))]
          (assoc state
                 :segment-id-seq segment-id
                 :ball-segment {:id segment-id
                                :start-ms now-ms
                                :end-ms (+ now-ms duration-ms)
                                :to-x target-x
                                :to-y target-y
                                :wall wall}))))))

(defn- apply-bottom-out
  [state]
  (if (> (:ball-y state) playfield-height)
    (let [lives-left (- (:lives state) 1)
          events (conj (:events state) :life-lost)]
      (if (<= lives-left 0)
        (assoc state
               :phase :game-over
               :lives 0
               :events (conj events :game-over)
               :ball-segment nil)
        (serve-ball (assoc state
                           :phase :serve
                           :lives lives-left
                           :ball-vx launch-speed-x
                           :ball-vy launch-speed-y
                           :events events))))
    state))

(defn- anchor-ball
  [state ball-x ball-y]
  (assoc state
         :ball-x ball-x
         :ball-y ball-y
         :ball-segment nil))

(defn- anchor-ball-from-render
  [state rendered-ball]
  (if (and (map? rendered-ball)
           (number? (:x rendered-ball))
           (number? (:y rendered-ball)))
    (anchor-ball state (:x rendered-ball) (:y rendered-ball))
    state))

(defn- event-rule-id
  [event]
  (let [id (:id event)
        rule (:rule event)
        rule-id (if rule (:id rule) nil)]
    (if (nil? id) rule-id id)))

(defn- overlap-width
  [self-aabb other-aabb]
  (let [x0 (max (:min-x self-aabb) (:min-x other-aabb))
        x1 (min (:max-x self-aabb) (:max-x other-aabb))]
    (- x1 x0)))

(defn- overlap-height
  [self-aabb other-aabb]
  (let [y0 (max (:min-y self-aabb) (:min-y other-aabb))
        y1 (min (:max-y self-aabb) (:max-y other-aabb))]
    (- y1 y0)))

(defn- ball-anchor-from-event
  [event]
  (let [self-aabb (:self-aabb event)]
    {:x (:min-x self-aabb)
     :y (:min-y self-aabb)}))

(defn- remove-brick-by-id
  [bricks brick-id]
  (loop [remaining bricks
         kept []
         hit nil]
    (if (empty? remaining)
      {:hit hit :bricks kept}
      (let [brick (first remaining)]
        (if (and (nil? hit) (= (:id brick) brick-id))
          (recur (rest remaining) kept brick)
          (recur (rest remaining) (conj kept brick) hit))))))

(defn- finish-brick-hit
  [state remaining]
  (if (empty? remaining)
    (if (= (+ (:level-index state) 1) (state-level-count state))
      (-> state
          (clear-paddle-motion)
          (assoc :phase :victory
                 :ball-segment nil
                 :events (conj (:events state) :victory)))
      (-> state
          (clear-paddle-motion)
          (assoc :phase :level-clear
                 :ball-segment nil
                 :events (conj (:events state) :level-clear))))
    state))

(defn apply-spatial-event
  "Pure domain transition for one host-provided spatial event."
  [state event now-ms]
  (let [phase (:phase state)
        event-phase (:phase event)
        rule-id (event-rule-id event)]
    (if (or (not= phase :play) (not= event-phase :enter))
      (clear-events state)
      (let [anchor (ball-anchor-from-event event)
            ball-x (:x anchor)
            ball-y (:y anchor)
            result
            (cond
              (= rule-id :ball-vs-paddle)
              (let [other-aabb (:other-aabb event)
                    paddle-x (:min-x other-aabb)
                    next-state (-> state
                                   (clear-events)
                                   (anchor-ball ball-x (- (:min-y other-aabb) ball-size))
                                   (assoc :ball-vx (paddle-bounce-vx paddle-x ball-x)
                                          :ball-vy (- (max 2 (abs (:ball-vy state))))
                                          :events (conj (:events (clear-events state)) :paddle-hit)))]
                (plan-next-segment next-state now-ms))

              (= rule-id :ball-vs-brick)
              (let [other-id (:other event)
                    removal (remove-brick-by-id (:bricks state) other-id)
                    hit (:hit removal)
                    remaining (:bricks removal)]
                (if (nil? hit)
                  (clear-events state)
                  (let [self-aabb (:self-aabb event)
                        other-aabb (:other-aabb event)
                        overlap-x (overlap-width self-aabb other-aabb)
                        overlap-y (overlap-height self-aabb other-aabb)
                        horizontal? (< overlap-x overlap-y)
                        bounced-vx (if horizontal? (- (:ball-vx state)) (:ball-vx state))
                        bounced-vy (if horizontal? (:ball-vy state) (- (:ball-vy state)))
                        snapped-x (if horizontal?
                                    (if (> (:ball-vx state) 0)
                                      (- (:min-x other-aabb) ball-size)
                                      (+ (:max-x other-aabb) 1))
                                    ball-x)
                        snapped-y (if horizontal?
                                    ball-y
                                    (if (> (:ball-vy state) 0)
                                      (- (:min-y other-aabb) ball-size)
                                      (+ (:max-y other-aabb) 1)))
                        state2 (-> state
                                   (clear-events)
                                   (anchor-ball snapped-x snapped-y)
                                   (assoc :bricks remaining
                                          :score (+ (:score state) (:points hit))
                                          :ball-vx bounced-vx
                                          :ball-vy bounced-vy
                                          :events (conj (:events (clear-events state)) :brick-hit)))
                        advanced (finish-brick-hit state2 remaining)]
                    (if (= (:phase advanced) :play)
                      (plan-next-segment advanced now-ms)
                      advanced))))

              :else
              (clear-events state))]
        result))))

(defn apply-segment-end-at-ms
  "Pure domain transition for one expected segment-end notification.
now-ms may be nil; in that case the segment end timestamp is used."
  [state segment-id now-ms]
  (let [segment (:ball-segment state)
        result
        (if (or (nil? segment) (not= (:id segment) segment-id))
          (clear-events state)
          (let [wall (:wall segment)
                end-ms (:end-ms segment)
                resume-ms (if (number? now-ms) now-ms end-ms)
                anchored (-> state
                             (clear-events)
                             (anchor-ball (:to-x segment) (:to-y segment)))]
            (cond
              (= wall :left)
              (plan-next-segment (assoc anchored :ball-vx (- (:ball-vx anchored))) resume-ms)

              (= wall :right)
              (plan-next-segment (assoc anchored :ball-vx (- (:ball-vx anchored))) resume-ms)

              (= wall :top)
              (plan-next-segment (assoc anchored :ball-vy (- (:ball-vy anchored))) resume-ms)

              (= wall :bottom)
              (apply-bottom-out anchored)

              :else
              anchored)))]
    result))

(defn apply-segment-end
  "Pure domain transition for one expected segment-end notification."
  [state segment-id]
  (apply-segment-end-at-ms state segment-id nil))

(defn apply-input
  "Pure domain transition for one normalized input map.
Optional `rendered-ball` should be {:x n :y n} when the host can sample the
current animated ball position before interrupting motion."
  [state input now-ms rendered-ball]
  (let [dx (let [v (get input :dx)] (if (number? v) v 0))
        launch? (or (= true (get input :launch))
                    (= true (get input :launch?)))
        pause? (or (= true (get input :pause))
                   (= true (get input :pause?)))
        next-paddle (max 0 (min (- playfield-width paddle-width)
                                (+ (:paddle-x state) (* dx paddle-speed))))
        moved (assoc (clear-events state) :paddle-x next-paddle)]
    (cond
      (= (:phase moved) :title)
      (if launch?
        (launch-from-serve (fresh-game-state moved) now-ms)
        moved)

      (= (:phase moved) :serve)
      (if launch?
        (launch-from-serve moved now-ms)
        (serve-ball moved))

      (= (:phase moved) :play)
      (if pause?
        (-> moved
            (anchor-ball-from-render rendered-ball)
            (assoc :phase :pause))
        moved)

      (= (:phase moved) :pause)
      (if pause?
        (plan-next-segment (assoc moved :phase :play) now-ms)
        moved)

      (= (:phase moved) :level-clear)
      (if launch?
        (prepare-level moved (+ (:level-index moved) 1) :serve)
        moved)

      (= (:phase moved) :game-over)
      (if launch?
        (fresh-game-state moved)
        moved)

      (= (:phase moved) :victory)
      (if launch?
        (fresh-game-state moved)
        moved)

      :else moved)))
