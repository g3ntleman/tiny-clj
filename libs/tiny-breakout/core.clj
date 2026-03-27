(ns tiny-breakout.core
  (:require [tiny-breakout.levels :as levels]
            [fx]))

(def playfield-width 320)
(def playfield-height 240)
(def paddle-width 40)
(def paddle-height 4)
(def paddle-y (- playfield-height 16))
(def paddle-speed 4)
(def ball-size 4)
(def default-lives 3)
(def launch-speed-x 0)
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

(defn- normalize-bricks
  [bricks]
  (levels/normalize-bricks bricks))

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

(defn init-state
  "Returns deterministic baseline game-state for one-screen breakout."
  []
  {:phase :title
   :score 0
   :lives default-lives
   :level-index 0
   :bricks {}
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

(defn- project-axis
  [pos velocity duration-ms]
  (+ pos (quot (* velocity duration-ms) segment-step-ms)))

;; Wall obstacles for fx/sweep-aabb.
;; IDs are negative to distinguish from bricks (positive IDs).
;; Avoid -1 because fx/sweep-aabb uses obstacle_id=-1 as its no-hit sentinel.
;; Coordinates match the gap formula in duration-ms-for-distance:
;;   right:  obs_min_x = playfield-width         (gap_x = pw - (bx+bs))
;;   left:   obs_max_x = 0 (x=-1, w=1)           (gap_x = bx)
;;   top:    obs_max_y = 0 (y=-1, h=1)           (gap_y = by)
;;   bottom: obs_min_y = ph+1+ball-size           (gap_y = ph+1 - by)
(def wall-obstacle-seq
  [{:id -101 :x playfield-width :y -1 :w 1 :h (+ playfield-height 2)}
   {:id -102 :x -1              :y -1 :w 1 :h (+ playfield-height 2)}
   {:id -103 :x -1              :y -1 :w (+ playfield-width 2) :h 1}
   {:id -104 :x -1 :y (+ playfield-height 1 ball-size) :w (+ playfield-width 2) :h 1}])

(defn- bounce-velocity
  "Returns {:vx :vy} with the velocity component mirrored by the hit face normal.
  :left/:right normals flip vx; :top/:bottom normals flip vy."
  [state normal]
  (if (or (= normal :left) (= normal :right))
    {:vx (- (:ball-vx state)) :vy (:ball-vy state)}
    {:vx (:ball-vx state) :vy (- (:ball-vy state))}))

(defn- wall-id->wall
  [hit-id]
  (cond
    (= hit-id -101) :right
    (= hit-id -102) :left
    (= hit-id -103) :top
    (= hit-id -104) :bottom
    :else nil))

(defn- create-wall-chosen
  [ball-x ball-y vx vy wall]
  (let [right-limit  (- playfield-width ball-size)
        bottom-limit (+ playfield-height 1)]
    (cond
      (= wall :right)
      (let [d (duration-ms-for-distance (- right-limit ball-x) vx)]
        {:wall :right :duration-ms d :to-x right-limit :to-y (project-axis ball-y vy d)})

      (= wall :left)
      (let [d (duration-ms-for-distance ball-x (- vx))]
        {:wall :left :duration-ms d :to-x 0 :to-y (project-axis ball-y vy d)})

      (= wall :top)
      (let [d (duration-ms-for-distance ball-y (- vy))]
        {:wall :top :duration-ms d :to-x (project-axis ball-x vx d) :to-y 0})

      (= wall :bottom)
      (let [d (duration-ms-for-distance (- bottom-limit ball-y) vy)]
        {:wall :bottom :duration-ms d :to-x (project-axis ball-x vx d) :to-y bottom-limit})

      :else nil)))

(defn- create-brick-chosen
  [ball-x ball-y vx vy brick normal]
  (let [bx (:x brick) by (:y brick) bw (:w brick) bh (:h brick)
        duration
        (cond
          (= normal :left)   (duration-ms-for-distance (max 0 (- bx (+ ball-x ball-size))) vx)
          (= normal :right)  (duration-ms-for-distance (max 0 (- ball-x (+ bx bw))) (- vx))
          (= normal :top)    (duration-ms-for-distance (max 0 (- by (+ ball-y ball-size))) vy)
          (= normal :bottom) (duration-ms-for-distance (max 0 (- ball-y (+ by bh))) (- vy))
          :else 0)
        to-x
        (cond
          (= normal :left)  (- bx ball-size)
          (= normal :right) (+ bx bw)
          :else (project-axis ball-x vx duration))
        to-y
        (cond
          (= normal :top)    (- by ball-size)
          (= normal :bottom) (+ by bh)
          :else (project-axis ball-y vy duration))]
    {:collision {:hit-id (:id brick) :normal normal}
     :duration-ms duration
     :to-x to-x
     :to-y to-y}))

(defn- hit->chosen
  [ball-x ball-y vx vy bricks hit]
  (let [hit-id (:hit-id hit)
        normal (:normal hit)]
    (if (< hit-id 0)
      (create-wall-chosen ball-x ball-y vx vy (wall-id->wall hit-id))
      (let [brick (get bricks hit-id)]
        (when brick
          (create-brick-chosen ball-x ball-y vx vy brick normal))))))

(defn- choose-segment-target
  "Calls fx/sweep-aabb against walls + bricks and returns a chosen-segment map:
    wall hit  → {:wall kw :duration-ms N :to-x N :to-y N}
    brick hit → {:collision {:hit-id N :normal kw} :duration-ms N :to-x N :to-y N}
    no hit    → nil"
  [ball-x ball-y vx vy bricks]
  (let [bricks (normalize-bricks bricks)
        all (concat wall-obstacle-seq (vals bricks))
        hit (fx/sweep-aabb {:x ball-x :y ball-y :w ball-size :h ball-size}
                           {:vx vx :vy vy}
                           all
                           5000)]
    (when hit
      (hit->chosen ball-x ball-y vx vy bricks hit))))

(defn- reflect-velocity-away-from-boundaries
  "Avoids zero-duration wall segments when the ball is already anchored on a wall
  and a freshly applied response would send it straight back into that wall."
  [state]
  (let [ball-x (let [v (:ball-x state)] (if (number? v) v 0))
        ball-y (let [v (:ball-y state)] (if (number? v) v 0))
        vx (let [v (:ball-vx state)] (if (number? v) v 0))
        vy (let [v (:ball-vy state)] (if (number? v) v 0))
        right-limit (- playfield-width ball-size)
        vx2 (if (or (and (<= ball-x 0) (< vx 0))
                    (and (>= ball-x right-limit) (> vx 0)))
              (- vx)
              vx)
        vy2 (if (and (<= ball-y 0) (< vy 0))
              (- vy)
              vy)]
    (assoc state
           :ball-vx vx2
           :ball-vy vy2)))

(defn- plan-next-segment
  [state now-ms]
  (if (not= (:phase state) :play)
    (clear-ball-segment state)
    (let [state (-> state
                    (assoc :bricks (normalize-bricks (:bricks state)))
                    reflect-velocity-away-from-boundaries)
          ball-x (let [v (:ball-x state)] (if (number? v) v 0))
          ball-y (let [v (:ball-y state)] (if (number? v) v 0))
          vx (let [v (:ball-vx state)] (if (number? v) v 0))
          vy (let [v (:ball-vy state)] (if (number? v) v 0))
          chosen (choose-segment-target ball-x ball-y vx vy (:bricks state))]
      (if (nil? chosen)
        (clear-ball-segment state)
        (let [duration-ms (:duration-ms chosen)
              segment-id (+ (let [v (:segment-id-seq state)] (if (number? v) v 0)) 1)]
          (assoc state
                 :segment-id-seq segment-id
                 :ball-segment {:id segment-id
                                :start-ms now-ms
                                :end-ms (+ now-ms duration-ms)
                                :from-x ball-x
                                :from-y ball-y
                                :to-x (:to-x chosen)
                                :to-y (:to-y chosen)
                                :wall (:wall chosen)
                                :collision (:collision chosen)}))))))

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

(defn- ball-anchor-from-event
  [event]
  (let [self-aabb (:self-aabb event)]
    {:x (:min-x self-aabb)
     :y (:min-y self-aabb)}))

(defn- remove-brick-by-id
  [bricks brick-id]
  (let [bricks (normalize-bricks bricks)
        hit (get bricks brick-id)]
    {:hit hit :bricks (dissoc bricks brick-id)}))

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
  "Pure domain transition for one host-provided spatial event.
  Only handles :ball-vs-paddle; brick collisions are handled predictively
  by fx/sweep-aabb inside choose-segment-target."
  [state event now-ms]
  (let [phase (:phase state)
        event-phase (:phase event)
        rule-id (event-rule-id event)]
    (if (or (not= phase :play) (not= event-phase :enter))
      (clear-events state)
      (if (= rule-id :ball-vs-paddle)
        (let [anchor (ball-anchor-from-event event)
              ball-x (:x anchor)
              ball-y (:y anchor)
              other-aabb (:other-aabb event)
              paddle-x (:min-x other-aabb)
              next-state (-> state
                             (clear-events)
                             (anchor-ball ball-x (- (:min-y other-aabb) ball-size))
                             (assoc :ball-vx (paddle-bounce-vx paddle-x ball-x)
                                    :ball-vy (- (max 2 (abs (:ball-vy state))))
                                    :events [:paddle-hit]))]
          (plan-next-segment next-state now-ms))
        (clear-events state)))))

(defn- bounce-off-wall
  [state wall]
  (let [bounced (if (= wall :top)
                  (assoc state :ball-vy (- (:ball-vy state)))
                  (assoc state :ball-vx (- (:ball-vx state))))]
    (assoc bounced :events (conj (:events bounced) :wall-hit))))

(defn apply-segment-end-at-ms
  "Pure domain transition for one expected segment-end notification.
now-ms may be nil; in that case the segment end timestamp is used."
  [state segment-id now-ms]
  (let [segment (:ball-segment state)
        result
        (if (or (nil? segment) (not= (:id segment) segment-id))
          (clear-events state)
          (let [end-ms    (:end-ms segment)
                resume-ms (if (number? now-ms) now-ms end-ms)
                collision (:collision segment)]
            (if collision
              ;; Brick hit: Validate-at-End
              (let [hit-id (:hit-id collision)
                    bricks (normalize-bricks (:bricks state))
                    brick  (get bricks hit-id)]
                (if brick
                  ;; Brick still present → bounce + replan
                  (let [normal  (:normal collision)
                        vel     (bounce-velocity state normal)
                        removal (remove-brick-by-id bricks hit-id)
                        state2  (-> state
                                    (clear-events)
                                    (anchor-ball (:to-x segment) (:to-y segment))
                                    (assoc :bricks   (:bricks removal)
                                           :ball-vx  (:vx vel)
                                           :ball-vy  (:vy vel)
                                           :score    (+ (:score state) (:points (:hit removal)))
                                           :events   [:brick-hit]))
                        advanced (finish-brick-hit state2 (:bricks removal))]
                    (if (= (:phase advanced) :play)
                      (plan-next-segment advanced resume-ms)
                      advanced))
                  ;; Phantom: brick gone → replan without response
                  (plan-next-segment
                    (anchor-ball state (:to-x segment) (:to-y segment))
                    resume-ms)))
              ;; Wall hit
              (let [wall    (:wall segment)
                    anchored (-> state
                                 (clear-events)
                                 (anchor-ball (:to-x segment) (:to-y segment)))]
                (cond
                  (or (= wall :left) (= wall :right) (= wall :top))
                  (plan-next-segment (bounce-off-wall anchored wall) resume-ms)

                  (= wall :bottom)
                  (apply-bottom-out anchored)

                  :else
                  anchored)))))]
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
