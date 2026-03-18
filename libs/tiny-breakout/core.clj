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

(defn- clamp
  [v min-v max-v]
  (if (< v min-v)
    min-v
    (if (> v max-v)
      max-v
      v)))

(defn- abs-int
  [v]
  (if (< v 0) (- v) v))

(defn- rect-overlap?
  [ax ay aw ah bx by bw bh]
  (not (or (> ax (+ bx bw -1))
           (> bx (+ ax aw -1))
           (> ay (+ by bh -1))
           (> by (+ ay ah -1)))))

(defn- level-count
  [state]
  (count (:levels state)))

(defn- level-bricks
  [state level-index]
  (:bricks (nth (:levels state) level-index)))

(defn- serve-ball
  [state]
  (assoc state
         :ball-x (+ (:paddle-x state) (quot paddle-width 2))
         :ball-y (- paddle-y 6)))

(defn- prepare-level
  [state level-index phase]
  (let [state2 (assoc state
                      :phase phase
                      :level-index level-index
                      :bricks (level-bricks state level-index)
                      :events []
                      :ball-vx launch-speed-x
                      :ball-vy launch-speed-y)]
    (serve-ball state2)))

(defn- fresh-game-state
  [state]
  (prepare-level (assoc state :score 0 :lives default-lives) 0 :serve))

(defn init-state
  "Returns deterministic baseline game-state for one-screen breakout."
  []
  {:phase :title
   :score 0
   :lives default-lives
   :level-index 0
   :levels (levels/load-levels)
   :bricks []
   :events []
   :paddle-x 140
   :ball-x 160
   :ball-y 210
   :ball-vx launch-speed-x
   :ball-vy launch-speed-y
   :tick 0})

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

(defn- apply-wall-bounce
  [state]
  (let [x (:ball-x state)
        y (:ball-y state)
        vx (:ball-vx state)
        vy (:ball-vy state)
        hit-side? (or (< x 0) (> x (- playfield-width ball-size)))
        hit-top? (< y 0)]
    (assoc state
           :ball-x (cond
                     (< x 0) 0
                     (> x (- playfield-width ball-size)) (- playfield-width ball-size)
                     :else x)
           :ball-y (if hit-top? 0 y)
           :ball-vx (if hit-side? (- vx) vx)
           :ball-vy (if hit-top? (- vy) vy))))

(defn- apply-paddle-bounce
  [state]
  (let [hit? (and (> (:ball-vy state) 0)
                  (rect-overlap? (:ball-x state) (:ball-y state) ball-size ball-size
                                 (:paddle-x state) paddle-y paddle-width paddle-height))]
    (if hit?
      (assoc state
             :ball-y (- paddle-y ball-size)
             :ball-vx (paddle-bounce-vx (:paddle-x state) (:ball-x state))
             :ball-vy (- (max 2 (abs-int (:ball-vy state))))
             :events (conj (:events state) :paddle-hit))
      state)))

(defn- remove-first-hit-brick
  [bricks ball-x ball-y]
  (loop [rest-bricks bricks
         kept []
         hit nil]
    (if (empty? rest-bricks)
      {:hit hit :bricks kept}
      (let [brick (first rest-bricks)
            overlap? (rect-overlap? ball-x ball-y ball-size ball-size
                                    (:x brick) (:y brick) (:w brick) (:h brick))]
        (if (and overlap? (nil? hit))
          (recur (rest rest-bricks) kept brick)
          (recur (rest rest-bricks) (conj kept brick) hit))))))

(defn- apply-brick-collision
  [state]
  (let [result (remove-first-hit-brick (:bricks state) (:ball-x state) (:ball-y state))
        hit (:hit result)
        remaining (:bricks result)]
    (if hit
      (let [state2 (assoc state
                          :bricks remaining
                          :score (+ (:score state) (:points hit))
                          :ball-vy (- (:ball-vy state))
                          :events (conj (:events state) :brick-hit))]
        (if (empty? remaining)
          (if (= (+ (:level-index state) 1) (level-count state))
            (assoc state2 :phase :victory :events (conj (:events state2) :victory))
            (assoc state2 :phase :level-clear :events (conj (:events state2) :level-clear)))
          state2))
      state)))

(defn- apply-bottom-out
  [state]
  (if (> (:ball-y state) playfield-height)
    (let [lives-left (- (:lives state) 1)
          events (conj (:events state) :life-lost)]
      (if (<= lives-left 0)
        (assoc state
               :phase :game-over
               :lives 0
               :events (conj events :game-over))
        (serve-ball (assoc state
                           :phase :serve
                           :lives lives-left
                           :ball-vx launch-speed-x
                           :ball-vy launch-speed-y
                           :events events))))
    state))

(defn- simulate-play
  [state]
  (-> state
      (assoc :events [])
      (assoc :ball-x (+ (:ball-x state) (:ball-vx state))
             :ball-y (+ (:ball-y state) (:ball-vy state)))
      (apply-wall-bounce)
      (apply-paddle-bounce)
      (apply-brick-collision)
      (apply-bottom-out)))

(defn step-state
  "Pure update step.
Input contract: {:dx n :launch? bool :pause? bool}"
  [state input dt-ms]
  (let [dx (let [v (get input :dx)] (if (number? v) v 0))
        launch? (or (= true (get input :launch))
                    (= true (get input :launch?)))
        pause? (or (= true (get input :pause))
                   (= true (get input :pause?)))
        next-paddle (clamp (+ (:paddle-x state) (* dx paddle-speed)) 0 (- playfield-width paddle-width))
        moved (assoc state
                     :paddle-x next-paddle
                     :tick (+ (:tick state) (if (number? dt-ms) dt-ms 0))
                     :events [])]
    (cond
      (= (:phase moved) :title)
      (if launch?
        (fresh-game-state moved)
        moved)

      (= (:phase moved) :serve)
      (if launch?
        (assoc moved :phase :play :ball-vx launch-speed-x :ball-vy launch-speed-y)
        (serve-ball moved))

      (= (:phase moved) :play)
      (if pause?
        (assoc moved :phase :pause)
        (simulate-play moved))

      (= (:phase moved) :pause)
      (if pause?
        (assoc moved :phase :play)
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
