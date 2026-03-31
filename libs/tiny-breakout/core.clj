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
  "Returns the state-provided levels vector when it is non-empty, else nil."
  [state]
  (let [xs (:levels state)]
    (if (and (vector? xs)
             (not (empty? xs)))
      xs
      nil)))

(defn- state-level-count
  "Returns the active level count from state overrides or built-in levels."
  [state]
  (let [xs (custom-levels state)]
    (if xs
      (count xs)
      (levels/level-count))))

(defn- current-level-bricks
  "Returns bricks for one level index from overrides or built-in levels."
  [state level-no]
  (let [xs (custom-levels state)]
    (if xs
      (levels/level-bricks (nth xs level-no))
      (levels/level-bricks-by-index level-no))))

(defn- normalize-bricks
  "Normalizes brick containers to an id->brick map."
  [bricks]
  (levels/normalize-bricks bricks))

(defn- clear-effects
  "Clears transient gameplay effect cues from state."
  [state]
  (if (contains? state :effects)
    (dissoc state :effects)
    state))

(defn- state-effects
  "Returns transient effect cues carried in state."
  [state]
  (let [events (:effects state)]
    (if (vector? events) events [])))

(defn- clear-ball-segment
  "Removes the active ball segment from state."
  [state]
  (assoc state :ball-segment nil))

(defn- clear-paddle-motion
  "Removes active paddle motion from state."
  [state]
  (assoc state :paddle-motion nil))

(defn- serve-ball
  "Places the ball on the paddle and clears active segment motion."
  [state]
  (-> state
      (clear-ball-segment)
      (assoc :ball-x (+ (:paddle-x state) (quot paddle-width 2))
             :ball-y (- paddle-y 5))))

(defn- prepare-level
  "Prepares one level with fresh bricks and serve-phase defaults."
  [state level-no phase]
  (let [state2 (assoc state
                      :phase phase
                      :level-no level-no
                      :bricks (current-level-bricks state level-no)
                      :ball-vx launch-speed-x
                      :ball-vy launch-speed-y)]
    (serve-ball state2)))

(defn- fresh-game-state
  "Builds a fresh game state with reset score/lives on level 0."
  [state]
  (prepare-level (assoc state :score 0 :lives default-lives) 0 :serve))

(defn- launch-from-serve
  "Transitions from serve to play and plans the next segment."
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
   :level-no 0
   :bricks {}
   :paddle-x 140
   :ball-x 160
   :ball-y 210
   :ball-vx launch-speed-x
   :ball-vy launch-speed-y
   :ball-segment nil
   :segment-id-seq 0})

(defn- paddle-bounce-vx
  "Computes horizontal bounce velocity from paddle contact offset."
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
  "Converts distance and speed to segment duration in milliseconds."
  [distance speed]
  (if (<= speed 0)
    0
    (let [scaled (* distance segment-step-ms)]
      (quot (+ scaled (- speed 1)) speed))))

(defn- project-axis
  "Projects one axis using velocity and duration in segment time units."
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
  "Maps sweep wall obstacle ids to logical wall keywords."
  [hit-id]
  (case hit-id
    -101 :right
    -102 :left
    -103 :top
    -104 :bottom
    nil))

(defn- create-wall-chosen
  "Builds chosen segment data for a wall collision target."
  [ball-x ball-y vx vy wall]
  (let [right-limit  (- playfield-width ball-size)
        bottom-limit (+ playfield-height 1)]
    (case wall
      :right
      (let [d (duration-ms-for-distance (- right-limit ball-x) vx)]
        {:wall :right :duration-ms d :to-x right-limit :to-y (project-axis ball-y vy d)})

      :left
      (let [d (duration-ms-for-distance ball-x (- vx))]
        {:wall :left :duration-ms d :to-x 0 :to-y (project-axis ball-y vy d)})

      :top
      (let [d (duration-ms-for-distance ball-y (- vy))]
        {:wall :top :duration-ms d :to-x (project-axis ball-x vx d) :to-y 0})

      :bottom
      (let [d (duration-ms-for-distance (- bottom-limit ball-y) vy)]
        {:wall :bottom :duration-ms d :to-x (project-axis ball-x vx d) :to-y bottom-limit})

      nil)))

(defn- create-brick-chosen
  "Builds chosen segment data for a brick collision target."
  [ball-x ball-y vx vy brick normal]
  (let [bx (:x brick) by (:y brick) bw (:w brick) bh (:h brick)
        duration
        (case normal
          :left   (duration-ms-for-distance (max 0 (- bx (+ ball-x ball-size))) vx)
          :right  (duration-ms-for-distance (max 0 (- ball-x (+ bx bw))) (- vx))
          :top    (duration-ms-for-distance (max 0 (- by (+ ball-y ball-size))) vy)
          :bottom (duration-ms-for-distance (max 0 (- ball-y (+ by bh))) (- vy))
          0)
        to-x
        (case normal
          :left  (- bx ball-size)
          :right (+ bx bw)
          (project-axis ball-x vx duration))
        to-y
        (case normal
          :top    (- by ball-size)
          :bottom (+ by bh)
          (project-axis ball-y vy duration))]
    {:collision {:hit-id (:id brick) :normal normal}
     :duration-ms duration
     :to-x to-x
     :to-y to-y}))

(defn- hit->chosen
  "Converts one sweep hit payload into a chosen segment target map."
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
  "Plans the next deterministic ball segment for play phase."
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
  "Applies life-loss or game-over transition for bottom-out."
  [state]
  (if (> (:ball-y state) playfield-height)
    (let [lives-left (- (:lives state) 1)
          events (conj (state-effects state) :sfx/life-lost)]
      (if (<= lives-left 0)
        (assoc state
               :phase :game-over
               :lives 0
               :effects (conj events :sfx/game-over)
               :ball-segment nil)
        (serve-ball (assoc state
                           :phase :serve
                           :lives lives-left
                           :ball-vx launch-speed-x
                           :ball-vy launch-speed-y
                           :effects events))))
    state))

(defn- anchor-ball
  "Anchors ball position immediately and clears active segment."
  [state ball-x ball-y]
  (assoc state
         :ball-x ball-x
         :ball-y ball-y
         :ball-segment nil))

(defn- anchor-ball-from-render
  "Anchors the ball from rendered position samples when available."
  [state rendered-ball]
  (if (and (map? rendered-ball)
           (number? (:x rendered-ball))
           (number? (:y rendered-ball)))
    (anchor-ball state (:x rendered-ball) (:y rendered-ball))
    state))

(defn- remove-brick-by-id
  "Removes one brick id and returns removed brick plus remaining bricks."
  [bricks brick-id]
  (let [bricks (normalize-bricks bricks)
        hit (get bricks brick-id)]
    {:hit hit :bricks (dissoc bricks brick-id)}))

(defn- finish-brick-hit
  "Applies level-clear or victory transitions after brick removal."
  [state remaining]
  (if (empty? remaining)
    (if (= (+ (:level-no state) 1) (state-level-count state))
      (-> state
          (clear-paddle-motion)
          (assoc :phase :victory
                 :ball-segment nil
                 :effects (conj (state-effects state) :sfx/victory)))
      (-> state
          (clear-paddle-motion)
          (assoc :phase :level-clear
                 :ball-segment nil
                 :effects (conj (state-effects state) :sfx/level-clear))))
    state))

(defn- bounce-off-wall
  "Applies wall bounce response and appends a wall-hit event."
  [state wall]
  (let [bounced (if (= wall :top)
                  (assoc state :ball-vy (- (:ball-vy state)))
                  (assoc state :ball-vx (- (:ball-vx state))))]
    (assoc bounced :effects (conj (state-effects bounced) :sfx/wall-hit))))

(defn- resolve-paddle-hit
  "Resolves one paddle hit into the next in-play ball state."
  [state event now-ms]
  (if (not= (:phase state) :play)
    (clear-effects state)
    (let [ball-x (:ball-x event)
          ball-y (:ball-y event)
          paddle-x (:paddle-x event)
          paddle-y (:paddle-y event)
          anchor-y (if (number? paddle-y) (- paddle-y ball-size) ball-y)]
      (if (and (number? ball-x)
               (number? anchor-y)
               (number? paddle-x))
        (let [next-state (-> state
                             (clear-effects)
                             (anchor-ball ball-x anchor-y)
                             (assoc :ball-vx (paddle-bounce-vx paddle-x ball-x)
                                    :ball-vy (- (max 2 (abs (:ball-vy state))))
                                    :effects [:sfx/paddle-hit]))]
          (plan-next-segment next-state now-ms))
        (clear-effects state)))))

(defn- resolve-segment-end
  "Resolves what happens when the active ball travel segment finishes."
  [state segment-id now-ms]
  (let [segment (:ball-segment state)
        result
        (if (or (nil? segment) (not= (:id segment) segment-id))
          (clear-effects state)
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
                                    (clear-effects)
                                    (anchor-ball (:to-x segment) (:to-y segment))
                                    (assoc :bricks   (:bricks removal)
                                           :ball-vx  (:vx vel)
                                           :ball-vy  (:vy vel)
                                           :score    (+ (:score state) (:points (:hit removal)))
                                           :effects  [:sfx/brick-hit]))
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
                                 (clear-effects)
                                 (anchor-ball (:to-x segment) (:to-y segment)))]
                (case wall
                  (:left :right :top) (plan-next-segment (bounce-off-wall anchored wall) resume-ms)
                  :bottom (apply-bottom-out anchored)
                  anchored)))))]
    result))

(defn- apply-player-input
  "Applies player intent (move/launch/pause) to the current game phase."
  [state input now-ms rendered-ball]
  (let [dx (let [v (get input :dx)] (if (number? v) v 0))
        launch? (or (= true (get input :launch))
                    (= true (get input :launch?)))
        pause? (or (= true (get input :pause))
                   (= true (get input :pause?)))
        next-paddle (max 0 (min (- playfield-width paddle-width)
                                (+ (:paddle-x state) (* dx paddle-speed))))
        moved (assoc (clear-effects state) :paddle-x next-paddle)]
    (case (:phase moved)
      :title
      (if launch?
        (launch-from-serve (fresh-game-state moved) now-ms)
        moved)

      :serve
      (if launch?
        (launch-from-serve moved now-ms)
        (serve-ball moved))

      :play
      (if pause?
        (-> moved
            (anchor-ball-from-render rendered-ball)
            (assoc :phase :pause))
        moved)

      :pause
      (if pause?
        (plan-next-segment (assoc moved :phase :play) now-ms)
        moved)

      :level-clear
      (if launch?
        (prepare-level moved (+ (:level-no moved) 1) :serve)
        moved)

      :game-over
      (if launch?
        (fresh-game-state moved)
        moved)

      :victory
      (if launch?
        (fresh-game-state moved)
        moved)

      moved)))

(defn step
  "Applies one breakout domain event and returns
  {:state next-state-without-effects :effects ordered-effect-cues}.
  Supported event types:
  - :game/input with :input and optional :rendered-ball
  - :game/segment-ended with :segment-id
  - :game/ball-hit-paddle with :ball-x/:ball-y/:paddle-x"
  [state event now-ms]
  (let [event-type (:type event)
        next-state (case event-type
                     :game/input
                     (apply-player-input state
                                         (:input event)
                                         now-ms
                                         (:rendered-ball event))

                     :game/segment-ended
                     (resolve-segment-end state (:segment-id event) now-ms)

                     :game/ball-hit-paddle
                     (resolve-paddle-hit state event now-ms)

                     (clear-effects state))
        effects (state-effects next-state)
        state-without-effects (clear-effects next-state)]
    {:state state-without-effects
     :effects effects}))

